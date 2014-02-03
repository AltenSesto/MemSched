/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 */
/*
* memsched.c: Fixed-Priority Preemptive Hierarchical Scheduler 
* with memory throttling for multi-core architectures
*/

/************************IMPORTANT******************************************************************************
* 'NR_OF_SERVERS' (hsf-fp.c) and 'REL_Q_SIZE' (release-queue.c) are VERY important to set with correct values.
* 'RUN_JIFFIES' defines the number of jiffies (1 jiffie=4ms usually) that the scheduler should run.
* Server parameters can be set in function 'hsf_init'.
* 
* Central Timer: 
* A central timer is used for envoking server release and budget expiration for each core.
****************************************************************************************************************/
//#define MODULE
//#define LINUX
//#define __KERNEL_

#include <linux/smp.h> /* IPI calls */
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
//#include <linux/init.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/slab.h>

#include "/home/joris/Desktop/MemSched/core/core.h"
#include "/usr/src/kernels/3.6.0-rc4/include/resch/config.h"
	
#include <linux/version.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/smp.h> /* IPI calls */
#include <linux/hardirq.h>
#include <linux/perf_event.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/atomic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/irq_work.h>
#include <linux/spinlock.h>


/* Module information */
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Memory throttling on multicore");
MODULE_AUTHOR("Mikael Asberg, Memory throttling& multi core: MemSched group");
//MemSched group members:
//Joris Slatman
//Nesredin Mahmud
//Rafia Inam

#define _GNU_SOURCE
#define RUN_JIFFIES 300
#define INT_SIZE_BIT (sizeof(int)<<3) // Integer size in bits (size in bytes multiplied by 8).
#define USE_MEMSCHED	// enable|disable memory throttling 
#define TRACE_LOG
#define SYSTEM_TIMEOUT 10000
#define __USE_GNU

/* Bits used by our hierarchical scheduler in the hsf flag in RESCH task control block */
#define ACTIVATE 0
#define RESCH_PREVENT_RELEASE 1
#define PREV_RELEASE_SKIPPED 2
#define PREVENT_RELEASE_AT_INIT 3

#define EXPERIMENT -1 // DEFAULT = -1 (RELEVANT FOR TESTING NUMBER OF CACHE MISSES ON NORMAL, PROBLEM AND MPLAYER TASKS.)
			// CASE STUDY = 0, EXPERIMENT1 = 1, EXPERIMENT2 = 2
#if EXPERIMENT == 0
	#define NR_OF_SERVERS 3
#elif EXPERIMENT == 1
	#define NRT_OF_SERVERS 5
#elif EXPERIMENT == 2
	#define NR_OF_SERVERS 5
#else
	#define NR_OF_SERVERS 1
#endif

#define MAX_RELEASED_AT_SAME_TIME 20    /* Maximum nr of servers that can be released at the same time instance */
#define MAX_NR_OF_TASKS_IN_SERVER 10
#define TRUE 		1
#define FALSE 		0
#define SAMPLE_RATE 1

/* Control block for a server (We allow maximum 32 servers) */
typedef struct server_struct server_t;
struct server_struct {

	int id; 						// 0 to (INT_SIZE_BIT-1)
	int cpu; 						// CPU id where there server is going to be active
	int sid;						// server ID relative to the core it is assigned to.
	int priority;					// the smaller the number, the higher the server priority is, 0 is the highest
	char running; 					// TRUE if the server is running, otherwise FALSE
	resch_task_t *resch_task_list[MAX_NR_OF_TASKS_IN_SERVER]; // Maximum nr of tasks per server...	
	int nr_of_tasks;
	server_t *next;

	// CPU part
	int period;
	int budget;						//cpu budget assigned by user
	int remain_budget;				// initially allocated a full budget will be decremented to zero for every unit of tick(jiffies)
	unsigned long budget_expiration_time;
	unsigned long timestamp;
		
	// memory part 
	struct irq_work deferred_work; //needed for calling scheduler after deferred_memory_work is done
	int mem_budget;
	int rem_mem_budget;
	
	// optional: statistics,debugging
	int wrong_interrupts;
	    unsigned long total_mem_request;
	int	number_of_mem_overflows;
};

/* Include queue headers*/
#include "ready-queue.c" // Implementation of server ready queue
#include "release-queue.c" // Implementation of release queue
char *experiment_type = "experiment1";
module_param(experiment_type, charp, 0000);

//int NR_OF_SERVERS = 0;
server_t SERVERS[NR_OF_SERVERS]; // Here are the servers, they will also reside in the server ready queue
int node_map[NR_OF_SERVERS];
int node_map_index = 0;
int core_server_map[NR_RT_CPUS][NR_OF_SERVERS];
int in = 0;
/* error handle */
int timer_setup = 0;
int scheduler_called = 0;
long total_mem_request = 0;


/* multicore: Glabal per cpu - for each cpu a global structure is defined */
typedef struct per_cpu perCpu;
struct per_cpu{
	relPq SERVER_RELEASE_QUEUE;
	relNode RelNodes[NR_OF_SERVERS];
	pq SERVER_READY_QUEUE;
	struct timer_list timer;
	int FIRST_RUN;
	unsigned long start;
	server_t *highest_prio_server;
	int servers[NR_OF_SERVERS];
	int s_id;
	int firstime;		
	int prev_virt_time; //keep track of previous virtual time. Needed for memsched memory
};
perCpu per_cpu[NR_RT_CPUS];

/* function declaration */
void init_server(server_t *server, int s_id, int period_msecs, int budget_msecs, int prio,int cpu,unsigned int memory_budget);
void task_release(resch_task_t *rt);
void task_complete(resch_task_t *rt);
void server_release_handler(unsigned long __data);
void server_complete_handler(unsigned long __data);
void task_run(resch_task_t *rt);
int next_scheduling_event(int release, int budget);
extern resch_task_t * getCurrentTask(void);//this function is in resch.c and is needed to get the current running resch_task_t
static void deferred_memory_work(struct irq_work * entry); //memory
static int get_sid(int cpu, int relative_id);

#include "recorder.c" // Include tracing facilities...

//Overhead measurement
	struct timeval start,end;
	unsigned long srh_count = 0;
	unsigned long sch_count = 0;
	unsigned long moh_count = 0;	// memory_overflow_handler()	
	unsigned long dmw_count = 0;	// deferred_memory_work()
	unsigned long dm_count = 0;
	unsigned long tr_count = 0;
	unsigned long tc_count = 0;
	unsigned long mof_count = 0;
	struct timespec start_ts, stop_ts;
	int mof_timestamp_state = 0;
#define TV_MICS(tval) (1000000*tval.tv_sec + tval.tv_usec)

//trace log macro
#define DEBUG_TRACE_MEMSCHED	1
#define debug_trace_memsched(fmt, ...)\
        do { if (DEBUG_TRACE_MEMSCHED) printk(KERN_WARNING "TR " fmt,__VA_ARGS__);} while(0)

//overhead measures log macro
#define DEBUG_OVERHEAD_MEASURES	0
#define debug_overhead_measures(fmt, ...)\
	do { if (DEBUG_OVERHEAD_MEASURES) printk(KERN_WARNING "OV " fmt,__VA_ARGS__);} while(0)
//overhead measure of memory overflow
#define DEBUG_OVERHEAD_MOF 0
#define debug_mof(fmt, ...)\
	do {\
		if (DEBUG_OVERHEAD_MOF){ 				\
			if(++mof_count < 1000){ 			\
                        	printk("OV memory_overflow_handler over_flowed %ld\n", stop_ts.tv_nsec - start_ts.tv_nsec);\
                	}						\
		}\
	} while(0)
/** 
* Initialization of a server inlcuding CPU, Memory throttling and multicore parameters and others.
* Each server is assigne to respected cores and identified by local(with in a core) and global IDs (with in the system). 
* Both IDs are mapped using a function get_sid() which returns global ID given local ID.
*/
void init_server(server_t *server, int s_id, int period_msecs, int budget_msecs, int prio, int cpu,unsigned int memory_budget) {
	
	debug_overhead_measures("init_server start %d\n", 0);

	server->id = s_id;			// global server id given by the user and is unique in the multicore arch.
	server->sid = per_cpu[cpu].s_id;	// local server id to the core it is assigned to, starts from 0
	server->period = msecs_to_jiffies(period_msecs);
	server->running = FALSE;
	server->priority = prio;
	server->nr_of_tasks = 0;
	server->next = NULL;
	
	// cpu init
	server->budget = msecs_to_jiffies(budget_msecs);
	server->remain_budget = msecs_to_jiffies(budget_msecs);
	server->budget_expiration_time = 0;
	server->timestamp = 0;

	// memory throttling init
	server->mem_budget = memory_budget; //assign budget given by user
	server->rem_mem_budget = memory_budget; //initialize dynamic mem budget here
	server->number_of_mem_overflows = 0;	
	server->total_mem_request = 0;	
	init_irq_work(&SERVERS[s_id].deferred_work, deferred_memory_work);//used in memory_overflow_handler
	server->wrong_interrupts = 0;

	//multicore	
	server->cpu = cpu;
	per_cpu[cpu].s_id++;	//increase the percpu server count variable	
	per_cpu[cpu].servers[server->sid] = s_id; //multicore: map the local server id (server->sid) to global server id (server->id)
	
	debug_overhead_measures("init_server stop init %d\n", 0);
}


/**
* This is the corresponding function to 'job_release_plugin' in resch KLM. Its main purpose is to prevent RESCH from releasing tasks
* during memsched startup, while its server is not active.
*/
void task_release(resch_task_t *rt) {

	server_t *highest_prio_server;

	debug_overhead_measures("task_release start %ld\n", ++tr_count);
	
	// This task is released before the system (servers) has started, prevent it from being released!!!
	if ( (rt->hsf_flags & SET_BIT(PREVENT_RELEASE_AT_INIT)) == SET_BIT(PREVENT_RELEASE_AT_INIT) ) {

		rt->hsf_flags |= SET_BIT(RESCH_PREVENT_RELEASE); // This will tell RESCH not to release it...
		rt->hsf_flags ^= SET_BIT(PREVENT_RELEASE_AT_INIT); // Reset the flag...		
		debug_overhead_measures("task_release stop release %ld\n", ++tr_count);

		return;
	}
	
	// Fetch the highest priority server (which is currently running)
	highest_prio_server = bitmap_get(&per_cpu[SERVERS[rt->server_id].cpu].SERVER_READY_QUEUE);

	// The system is idle, i.e., there is no server running...
	if (highest_prio_server == NULL) {

		rt->hsf_flags |= SET_BIT(RESCH_PREVENT_RELEASE); // This will tell RESCH not to release the task
		store_event(rt, rt, NULL, NULL, 2, jiffies, FALSE, TRUE); // Store task event (for tracing purposes)
		debug_overhead_measures("task_release stop release %ld\n", ++tr_count);

		return;
	}
	
	// If this task is released and its server is not active, then prevent it from being released!!!
	if (SERVERS[rt->server_id].id != highest_prio_server->id) 
	{
		rt->hsf_flags |= SET_BIT(RESCH_PREVENT_RELEASE); // This will tell RESCH not to release it...
		store_event(rt, rt, NULL, NULL, 2, jiffies, FALSE, TRUE); 
	}
	// Regular task release449: er
	else {
		task_release_event(rt); // Store task event (for tracing purposes)
	}
	debug_overhead_measures("task_release stop release %ld\n", ++tr_count);
}

/**
* This is the corresponding function to job_complete in the resch KLM.
*/
void task_complete(resch_task_t *rt) {

	server_t *highest_prio_server;
	
	debug_overhead_measures("task_complete start %ld\n", ++tc_count);


	if ( (rt->hsf_flags & SET_BIT(PREVENT_RELEASE_AT_INIT)) == SET_BIT(PREVENT_RELEASE_AT_INIT) ) {
#ifdef TRACE_LOG
		printk(KERN_WARNING "HSF (task_complete): Bit %d is set??? (%s) curr core %d\n", PREVENT_RELEASE_AT_INIT, rt->task->comm, smp_processor_id());
#endif
	rt->hsf_flags ^= SET_BIT(ACTIVATE);//nas
	debug_overhead_measures("task_complete stop complete %ld\n", ++tc_count);
		return;
	}
	
	// Fetch the highest priority server
	highest_prio_server = bitmap_get(&per_cpu[SERVERS[rt->server_id].cpu].SERVER_READY_QUEUE);

	if (highest_prio_server == NULL) {
#ifdef TRACE_LOG
		printk(KERN_WARNING "HSF (task_complete): Task %s has finished and no active server!!! (%lu) curr core %d\n", rt->task->comm, jiffies, smp_processor_id());
#endif
	}
	else if (SERVERS[rt->server_id].id != highest_prio_server->id) {
#ifdef TRACE_LOG
		printk(KERN_WARNING "HSF (task_complete): Task %s finished in wrong server!!! (%lu) curr core %d\n", rt->task->comm, jiffies, smp_processor_id());
#endif
	}
	rt->hsf_flags ^= SET_BIT(ACTIVATE);//nas
	task_complete_event(rt); // Store task event (for tracing purposes)
	
	debug_overhead_measures("task_complete stop complete %ld\n", ++tc_count);
}

/* 
* A handler raised when a timer expires to release a server and taks belonginig to it, if any are active. 
* It controls CPU budget of servers.
*/
void server_release_handler(unsigned long __data) {
	
	
	server_t *released_server = (server_t *)__data;
	server_t *highest_prio_server = NULL, *server = NULL;
	server_t idle;
	relNode *node = NULL;
	unsigned long flags;
	resch_task_t *next = NULL, *prev = NULL, temp;
	int i, j, event, next_relative_event, special, cpu;
	relNode *Released[MAX_RELEASED_AT_SAME_TIME];

	cpu = released_server->cpu;
	//overhead measures: start
	debug_overhead_measures("server_release_handler start %ld\n", ++srh_count);

#ifdef TRACE_LOG
	debug_trace_memsched("serverreleasehandler: server%d released at_jiffies %lu at_core %d\n" , released_server->id , jiffies, cpu);
#endif
	//modified by MemSched group
#ifdef USE_MEMSCHED		
	per_cpu[released_server->cpu].prev_virt_time = per_cpu[released_server->cpu].SERVER_RELEASE_QUEUE.virtual_time;//keep track of this time because 
#endif
	

	//synchronize server and tasks their release time
	if (per_cpu[cpu].firstime == TRUE){
	        
		int server,task;
        	for (server = 0; server < per_cpu[cpu].s_id; ++server){
                	for (task = 0; task < SERVERS[get_sid(cpu,server)].nr_of_tasks; ++task){
				
				SERVERS[get_sid(cpu,server)].resch_task_list[task]->release_time = jiffies;                        
        	        }       
			
        	}
		per_cpu[cpu].firstime = FALSE;
	}

	special = 0;

	if (released_server == NULL) { // EXTRA
		printk(KERN_WARNING "(server_release_handler) server==NULL???\n"); // EXTRA
		return; // EXTRA
	}

	// Fetch the highest priority server
	highest_prio_server = bitmap_get(&per_cpu[cpu].SERVER_READY_QUEUE);
	if(per_cpu[cpu].firstime == TRUE){// incase control comes here
	//	highest_prio_server = NULL;
		per_cpu[cpu].firstime = FALSE;
	}
	
	// Refill budget
	released_server->remain_budget = released_server->budget;	//cpu budget
	
#ifdef USE_MEMSCHED
	released_server->rem_mem_budget = released_server->mem_budget;//memory budget
	released_server->wrong_interrupts = 0;
#endif
	
	// Insert server in ready queue...
	bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, released_server);

	// Deallocate the timer...
	//printk("Serverrelease handler: timer remove with exp time: %lu curr core %d \n", per_cpu[cpu].timer.expires, smp_processor_id());
	destroy_timer_on_stack(&per_cpu[cpu].timer);

	// Update the release queue...
	if (per_cpu[cpu].FIRST_RUN == FALSE) {
		event = 0;
		relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
		node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);

		if (event < 0) {
			printk(KERN_WARNING "(server_release_handler) event < 0 ???\n");
			return;
		}

		relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &i); // Check the value of the second element in the release queue
		if (i != event) { // Only one server to release...
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event+SERVERS[get_sid(cpu, node->index)].period, node);
			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
		}
		else { // More than one server has to be released...
			next_relative_event = event;
			Released[0] = node;
			i = 1;
			while (TRUE) {
				event = 0;
				relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				Released[i] = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				// Refill budget
				SERVERS[get_sid(cpu,Released[i]->index)].remain_budget = SERVERS[get_sid(cpu,Released[i]->index)].budget;//cpu
				
#ifdef USE_MEMSCHED
                SERVERS[get_sid(cpu,Released[i]->index)].rem_mem_budget = SERVERS[get_sid(cpu,Released[i]->index)].mem_budget;//mem
#endif
				//Insert the server in the ready queue
				debug_trace_memsched("serverreleasehandler: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,Released[i]->index)].id , jiffies, SERVERS[get_sid(cpu,Released[i]->index)].cpu);
				bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,Released[i]->index)]));
				
				i++;
				relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				if (event != next_relative_event || event < 0)
					break;
			}
			server = bitmap_get(&per_cpu[cpu].SERVER_READY_QUEUE); // Now lets see who has highest prio...

			for (j = 0; j < i; j++) {
				if (server->sid == Released[j]->index) { // multicore: Ok, one of the newbies is highest! use the local server id!
					released_server = server;
					special = 2;
				}
				else { // Record that this server has been released, i.e., refill capacity...
					store_event(NULL, NULL, &(SERVERS[get_sid(cpu,Released[j]->index)]), &(SERVERS[get_sid(cpu, Released[j]->index)]), 2, jiffies, FALSE, TRUE);
					if (released_server->sid == Released[j]->index)
						special++; // If 'released_server' has stored event here and no-one enter
				}			   // the 'if' statement above, then dont store an event at the
			}
			// Now update and insert all elements in the release queue...
			for (j = 0; j < i; j++) {
				relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, next_relative_event+SERVERS[get_sid(cpu,Released[j]->index)].period, Released[j]);
			}

			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
		}
	}
	else { // Special-case: The first scheduling event does not need to actually update the release queue...
	       // ...just get the next event (in relative time)
		per_cpu[cpu].FIRST_RUN = FALSE;
		per_cpu[cpu].start = jiffies;
		next_relative_event = 0;
		relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &next_relative_event);
		node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &next_relative_event); // We need the node later but...
		relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, next_relative_event, node);     // ... it must stay in the queue...
	}

	//printk(KERN_WARNING "S jiffies: %lu\tsid: %d\tSTARTED \tat_core: %d\n", jiffies, released_server->id, cpu);
	if (highest_prio_server == NULL) { /* It was idle, just release this server... */
		debug_trace_memsched("IDLE server was running------------------------%d\n",0);

		// Store server event (for tracing purposes)
		idle.id = 1000;
		store_event(NULL, NULL, &idle, released_server, 1, jiffies, FALSE, TRUE);

		smp_mb();	
		for (i = 0; i < released_server->nr_of_tasks; i++) {
			// If task was ready before, then make it ready now
			if ( (released_server->resch_task_list[i]->hsf_flags & SET_BIT(ACTIVATE)) == SET_BIT(ACTIVATE) ) {
				if (wake_up_process(released_server->resch_task_list[i]->task) == 1){
				printk("server_release_handler: task_id %d is enqued at_jiffies %ld at_core %d\n", released_server->resch_task_list[i]->task->pid, jiffies, cpu);
				enqueue_task(released_server->resch_task_list[i]);
				}
				else{
					printk("server_release_handler: cannot wakeup task. Task already running\n");	
				}
			}
		}
		released_server->budget_expiration_time = jiffies+released_server->budget;
		released_server->timestamp = jiffies; // Set timestamp in case of preemption (budget accounting)
		released_server->running = TRUE;
		// Start next expiration timer...
		next_relative_event = next_scheduling_event(next_relative_event, released_server->budget);

		if (next_relative_event == released_server->budget) {
			
#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
 
			setup_timer_on_stack(&per_cpu[cpu].timer, server_complete_handler, (unsigned long)released_server);
			mod_timer(&per_cpu[cpu].timer, released_server->budget_expiration_time);
		}
		else {

#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
			setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[get_sid(cpu,node->index)]);
			mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		}
		
		// At the next scheduling event, the virtual time will be equal to our next scheduling event...
	#ifdef USE_MEMSCHEDx	
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;//this is needed for memsched to adjust virtual time if deplete on memory happens
	#endif
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;

		// Store task event (for tracing purposes)
		active_queue_lock(0, &flags);
		next = active_highest_prio_task(0);
		active_queue_unlock(0, &flags);
		temp.pid = 0;
		if (next != NULL) // Task switch without prev. task completing...
			store_event(&temp, next, NULL, NULL, 3, jiffies, FALSE, TRUE);

		
		//Overhead measures: end
		debug_overhead_measures("server_release_handler stop idle %ld\n", srh_count);
		
		return;


	} /* This means that there should be a server preemption!!! */
	else if (released_server->priority < highest_prio_server->priority) {
	
		debug_trace_memsched("PREEMPT-------------------------------%d\n", 0);
//return;

		// Store server event (for tracing purposes)
		store_event(NULL, NULL, highest_prio_server, released_server, 1, jiffies, FALSE, TRUE);

		// Store task event (for tracing purposes)
		active_queue_lock(0, &flags);
		prev = active_highest_prio_task(0);
		active_queue_unlock(0, &flags);

		// Remove previous running servers tasks
		for (i = 0; i < highest_prio_server->nr_of_tasks; i++) {

			if (highest_prio_server->resch_task_list[i]->task->state == TASK_RUNNING) { // It was running...TASK_RUNNING
		//		printk("task %s is dequeuend\n", highest_prio_server->resch_task_list[i]->task->comm);
				highest_prio_server->resch_task_list[i]->hsf_flags |= SET_BIT(ACTIVATE);
				dequeue_task(highest_prio_server->resch_task_list[i]);
				highest_prio_server->resch_task_list[i]->task->state = TASK_UNINTERRUPTIBLE;
				set_tsk_need_resched(highest_prio_server->resch_task_list[i]->task);
			}
		}
		smp_mb();

		// Special case: server has never been executed if running == FALSE
		if (highest_prio_server->running == TRUE) {
			// Decrease the servers remaining budget
			highest_prio_server->remain_budget -= (jiffies-highest_prio_server->timestamp);
		}

		highest_prio_server->running = FALSE;

		smp_mb();
		// Release the preempting servers tasks
		for (i = 0; i < released_server->nr_of_tasks; i++) {

			// If task was ready before, then make it ready now
			//if (0) {
			if ( (released_server->resch_task_list[i]->hsf_flags & SET_BIT(ACTIVATE)) == SET_BIT(ACTIVATE)) {
debug_trace_memsched("serverreleasehandler task_id %d enqueued at_jiffies %lu at_core %d\n", released_server->resch_task_list[i]->task->pid, jiffies, cpu);
				if (wake_up_process(released_server->resch_task_list[i]->task) == 1){
                                //released_server->resch_task_list[i]->exec_time = 0;
                                //released_server->resch_task_list[i]->task->utime = 0;
                                enqueue_task(released_server->resch_task_list[i]);
                                }//endof wakeup
                                else{
                                        printk("server_release_handler: cannot wakeup task. Task already running\n");
                                }

			}
		}

		// Store task event (for tracing purposes)
		active_queue_lock(0, &flags);
		next = active_highest_prio_task(0);
		active_queue_unlock(0, &flags);
		temp.pid = 0;
		if (prev != NULL && next != NULL)
			store_event(prev, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
		else if (prev == NULL && next != NULL)
			store_event(&temp, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
		else if (prev != NULL && next == NULL)
			store_event(prev, &temp, NULL, NULL, 3, jiffies, FALSE, TRUE);

		released_server->budget_expiration_time = jiffies+released_server->budget;
		released_server->timestamp = jiffies; // Set timestamp in case of preemption (budget accounting)
		released_server->running = TRUE;

		// Start next expiration timer...
		next_relative_event = next_scheduling_event(next_relative_event, released_server->budget);

		if (next_relative_event == released_server->budget) {
#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
			setup_timer_on_stack(&per_cpu[cpu].timer, server_complete_handler, (unsigned long)released_server);
			mod_timer(&per_cpu[cpu].timer, released_server->budget_expiration_time);
		}
		else {
#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
			setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[get_sid(cpu,node->index)]);
			mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		}
		// At the next scheduling event, the virtual time will be equal to our next scheduling event...
#ifdef USE_MEMSCHED
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;//this is needed for memsched to adjust virtual time if deplete on memory happens
#endif
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;

		//Overhead measures: end
		debug_overhead_measures("server_release_handler stop preemption %ld\n", srh_count);

	}
	else { // No re-scheduling...just set a next scheduling event...
		debug_trace_memsched("no re-scheduling.....%d\n", 0);
		if (special != 1) { // Record that this server has been released, i.e., refill capacity...
			store_event(NULL, NULL, released_server, released_server, 2, jiffies, FALSE, TRUE);
		}

		i = highest_prio_server->budget_expiration_time - jiffies; // Time left for this server... 

		// Start next expiration timer...
		next_relative_event = next_scheduling_event(next_relative_event, i);

		if (next_relative_event == i) {
#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
			setup_timer_on_stack(&per_cpu[cpu].timer, server_complete_handler, (unsigned long)highest_prio_server);
			mod_timer(&per_cpu[cpu].timer, highest_prio_server->budget_expiration_time);
		}
		else {
#ifdef USE_MEMSCHED	
			del_timer(&per_cpu[cpu].timer);//make the timer inactive so that it can be changed. 
			destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
			setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[get_sid(cpu,node->index)]);
			mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		}
		// At the next scheduling event, the virtual time will be equal to our next scheduling event...
#ifdef USE_MEMSCHED
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;//this is needed for memsched to adjust virtual time if deplete on memory happens
#endif
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;
		
		//Overhead measures: end
		debug_overhead_measures("server_release_handler stop no_resched %ld\n", srh_count);
	}
}


/**
* This function is a timer interrupt handler for CPU budget control of servers. 
* The kernel timer tracks a CPU budget, when depletes, this handler is triggered.
* The following activities are performed in this handler:
* 1. Destroy the timer which triggered the handler from the timer stack.
* 2. Dequeue all active tasks of the current server from resch queue and indicate each task were active and are TASK_UNINTERRUPTIBLE.
* 3. Check if any server are to be released at this time, if so initialize them.
* 4. fetch the highest priority server, if any, enqueue its active tasks into resch queue and change server status to running.
*    if not, setup a timer for next release of a server and return.
* 5. After enqueueing highest priority server taksks, setup timers for next server release and highest priority server budget complete.
*/
void server_complete_handler(unsigned long __data) {

	server_t *completed_server = (server_t *)__data;
	server_t *highest_prio_server = NULL;
	server_t idle;
	relNode *node =  NULL;
	unsigned long flags;
	resch_task_t *next = NULL, *prev = NULL, temp;
	int i, j, event, next_relative_event, cpu, wrap_value;
	relNode *Released[MAX_RELEASED_AT_SAME_TIME];
	
        cpu = completed_server->cpu;//;smp_processor_id();
	debug_trace_memsched("servercompletehandler: server%d completed at_jiffies %lu with_mem %d at_core %d\n", completed_server->id , jiffies, completed_server->mem_budget - completed_server->rem_mem_budget, cpu);
	debug_overhead_measures("server_complete_handler start %ld\n", ++sch_count);

#ifdef USE_MEMSCHED	
	//keep track of this time because very fast after this time there can be a server memory depletion
	per_cpu[completed_server->cpu].prev_virt_time = per_cpu[completed_server->cpu].SERVER_RELEASE_QUEUE.virtual_time;
#endif
	
	// Remove this server from the ready queue
	highest_prio_server = bitmap_retrieve(&per_cpu[cpu].SERVER_READY_QUEUE);
	if (highest_prio_server == NULL) {
		printk(KERN_WARNING "HSF (server_complete_handler): 1Server ready queue empty!!! (%lu)\n", jiffies);
		return;
	}
	if (highest_prio_server->id != completed_server->id) {
		printk(KERN_WARNING "HSF (server_complete_handler): Wrong expired server!!! (id:%d,%d) (%lu)curr core %d\n", highest_prio_server->id, completed_server->id, jiffies, smp_processor_id());
		destroy_timer_on_stack(&per_cpu[cpu].timer);
		return;
	}

	// Deallocate the timer in this completed server...
	destroy_timer_on_stack(&per_cpu[cpu].timer);

	// Store task event (for tracing purposes)
	active_queue_lock(0, &flags);
	prev = active_highest_prio_task(0);
	active_queue_unlock(0, &flags);

	for (i = 0; i < completed_server->nr_of_tasks; i++) {
		if (completed_server->resch_task_list[i]->task->state == TASK_RUNNING) { // It was running...TASK_RUNNING
	debug_trace_memsched("servercompletehandler: task_id %d dequeued at_jiffies %lu at_core %d\n", completed_server->resch_task_list[i]->task->pid , jiffies, cpu);
                       completed_server->resch_task_list[i]->hsf_flags |= SET_BIT(ACTIVATE);
                        dequeue_task(completed_server->resch_task_list[i]);
                        completed_server->resch_task_list[i]->task->state = TASK_UNINTERRUPTIBLE;
                        set_tsk_need_resched(completed_server->resch_task_list[i]->task);
                }
	}
	smp_mb();	
	completed_server->running = FALSE;

	event = 0;
	relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
	node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
	if (per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time == event) { // Check if any servers should be released...

		relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &i); // Check the value of the second element in the release queue

		if (i != event) { // Only one server to release...
		 //printk("Event is %d curr core %d \n", event, smp_processor_id());
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event+SERVERS[get_sid(cpu,node->index)].period, node);

			// Refill budget
			SERVERS[get_sid(cpu,node->index)].remain_budget = SERVERS[get_sid(cpu,node->index)].budget;//cpu
			
#ifdef USE_MEMSCHED
            SERVERS[get_sid(cpu,node->index)].rem_mem_budget = SERVERS[get_sid(cpu,node->index)].mem_budget;//mem budget
#endif			
			// Insert the server in the ready queue
			debug_trace_memsched("servercompletehandler: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,node->index)].id , jiffies, SERVERS[get_sid(cpu,node->index)].cpu);
			bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,node->index)]));
			store_event(NULL, NULL, &(SERVERS[per_cpu[cpu].servers[node->index]]), &(SERVERS[per_cpu[cpu].servers[node->index]]), 2, jiffies, FALSE, TRUE);

			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
		}
		else { // More than one server has to be released...
			next_relative_event = event;
			Released[0] = node;

			// Refill budget
			SERVERS[get_sid(cpu,Released[0]->index)].remain_budget = SERVERS[get_sid(cpu,Released[0]->index)].budget;//cpu
			
			//modified by MemSched group
			//memory
#ifdef USE_MEMSCHED
			SERVERS[get_sid(cpu,Released[0]->index)].rem_mem_budget = SERVERS[get_sid(cpu,Released[0]->index)].mem_budget;// mem budget			
#endif			
			// Insert the server in the ready queue
			debug_trace_memsched("servercompletehandler: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,Released[0]->index)].id , jiffies, SERVERS[get_sid(cpu,Released[0]->index)].cpu);
			bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,Released[0]->index)]));

			store_event(NULL, NULL, &(SERVERS[get_sid(cpu,Released[0]->index)]), &(SERVERS[get_sid(cpu,Released[0]->index)]), 2, jiffies, FALSE, TRUE);

			i = 1;
			while (TRUE) {
				event = 0;
				relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				Released[i] = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);

				// Refill budget
				SERVERS[get_sid(cpu,Released[i]->index)].remain_budget = SERVERS[get_sid(cpu,Released[i]->index)].budget;//cpu
				
#ifdef USE_MEMSCHED
                SERVERS[get_sid(cpu,Released[i]->index)].rem_mem_budget = SERVERS[get_sid(cpu,Released[i]->index)].mem_budget;// mem budget
#endif
				// Insert the server in the ready queue
				bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,Released[i]->index)]));
				store_event(NULL, NULL, &(SERVERS[get_sid(cpu,Released[i]->index)]), &(SERVERS[get_sid(cpu,Released[i]->index)]), 2, jiffies, FALSE, TRUE);

				i++;
				relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				if (event != next_relative_event || event < 0)
					break;
			}
			
			// Now update and insert all elements in the release queue...
			for (j = 0; j < i; j++) {
				//printk("_____EVEVNT is %d curr core %d \n", next_relative_event, smp_processor_id());
				relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, next_relative_event+SERVERS[get_sid(cpu,Released[j]->index)].period, Released[j]);
			}

			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
		}
	}
	else { // No servers to release...
		
		relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
#ifdef USE_MEMSCHED		

	
		//when virtual time is 27 and next event is release at 8(after swap at 32) the following must be  done:
		//event becomes later: the time between virtual time and 32(time before swap) + the event(time after swap)
		if(event < per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time){
			

			 wrap_value = per_cpu[cpu].SERVER_RELEASE_QUEUE.bitmap_size * INT_SIZE_BIT;//the value where to wrap
			
			 //printk("wrap value is %d \n", wrap_value);
			 next_relative_event = ((wrap_value-per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time) + event);
			 
			printk("server complete handler 1: event is : event %d\n", event);
			printk("server complete handler 1: virt time  is: %d\n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);
			printk("server complete handler 1: wrap value  is :  %d\n", wrap_value);
			printk("server complete handler 1: next rel event is:  %d\n",next_relative_event);
		}
		else next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;//else do normal calculation
#else
		next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;//else do normal calculation
#endif		
		
	}

	/* Fetch the highest priority server */
	highest_prio_server = bitmap_get(&per_cpu[cpu].SERVER_READY_QUEUE);

	if (highest_prio_server == NULL) { // No ready server, then we idle...
		// Store server event (for tracing purposes)
		idle.id = 1000;
		store_event(NULL, NULL, completed_server, &idle, 0, jiffies, FALSE, TRUE);

		// Store task event (for tracing purposes)
		temp.pid = 0;
		if (prev != NULL)
			store_event(prev, &temp, NULL, NULL, 3, jiffies, FALSE, TRUE);
			
#ifdef USE_MEMSCHED
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif

		setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[per_cpu[cpu].servers[node->index]]);
		mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		
#ifdef USE_MEMSCHED		
		//this is needed to adjust virtual time when mem budget depletes earlier than timer
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;
#endif		
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;
		
#ifdef USE_MEMSCHED		
		wrap_value = per_cpu[cpu].SERVER_RELEASE_QUEUE.bitmap_size * INT_SIZE_BIT;
		//when virtual becomes for example 72 with the new rel event added and swap is on 64 the new virtual time becomes 72-64
		if(per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time > wrap_value){
			printk("server complete handler 2: virt time  is: %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);
			printk("server complete handler 2: wrap value  is :  %d \n", wrap_value);		
			//in case of swapping this values must also swap just like the 	 release queue see: No servers to release above ....	
			per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time -= wrap_value;
		}
#endif		
			
		debug_overhead_measures("server_complete_handler stop idling %ld\n", sch_count);
		return;
	}
	smp_mb();
	// Release the highest prio servers tasks
	for (i = 0; i < highest_prio_server->nr_of_tasks; i++) {

		// If task was ready before, then make it ready now
		if ( (highest_prio_server->resch_task_list[i]->hsf_flags & SET_BIT(ACTIVATE)) == SET_BIT(ACTIVATE)) {
debug_trace_memsched("servercompletehandler: task_pid%d enqueued at_jiffies %lu at_core %d\n", highest_prio_server->resch_task_list[i]->task->pid , jiffies,highest_prio_server->cpu);
			wake_up_process(highest_prio_server->resch_task_list[i]->task);
			enqueue_task(highest_prio_server->resch_task_list[i]);
		}
	}

	// Store task event (for tracing purposes)
	active_queue_lock(0, &flags);
	next = active_highest_prio_task(0);
	active_queue_unlock(0, &flags);
	temp.pid = 0;
	if (prev != NULL && next != NULL)
		store_event(prev, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
	else if (prev == NULL && next != NULL)
		store_event(&temp, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
	else if (prev != NULL && next == NULL)
		store_event(prev, &temp, NULL, NULL, 3, jiffies, FALSE, TRUE);

	// Store server event (for tracing purposes)
	store_event(NULL, NULL, completed_server, highest_prio_server, 0, jiffies, FALSE, TRUE);

	highest_prio_server->budget_expiration_time = jiffies+highest_prio_server->remain_budget;
	highest_prio_server->timestamp = jiffies; // Set timestamp in case of preemption (budget accounting)
	highest_prio_server->running = TRUE;

	// Start next expiration timer...
	next_relative_event = next_scheduling_event(next_relative_event, highest_prio_server->remain_budget);

	if (next_relative_event == highest_prio_server->remain_budget) {
		
#ifdef USE_MEMSCHED
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif
		setup_timer_on_stack(&per_cpu[cpu].timer, server_complete_handler, (unsigned long)highest_prio_server);

		mod_timer(&per_cpu[cpu].timer, highest_prio_server->budget_expiration_time);
	}
	else {
		
#ifdef USE_MEMSCHED
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
#endif		
		setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[per_cpu[cpu].servers[node->index]]);
		mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
	}
#ifdef USE_MEMSCHED		
		//this is needed to adjust virtual time when mem budget depletes earlier than timer
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;
#endif	
	per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;
	
	debug_overhead_measures("server_complete_handler stop new_server %ld\n", sch_count);
	
}


/**
* This function is envoked from memory_overflow_handler() when memory budget deplete for a server due to tasks' CACHE MISSES.
* It implements similarity to server_complete_handler. 
*/
static void deferred_memory_work(struct irq_work * entry){
	//this function is called from memory_interrupt_handler. It is needed because after returning from this function the scheduler is called.
	//this function is doing the server_complete_handler work for servers depleted based on mem, this means a lot of code duplication
	//This function is made seperate because it contains some extra calculations for mem budget depletion.
		
	server_t *highest_prio_server = NULL;
	server_t idle;
	relNode *node =  NULL;
	unsigned long flags;
	resch_task_t *next = NULL, *prev = NULL, temp;
	int i, j, event, next_relative_event, cpu,wrap_value,ret1, ret2, ret3;
	relNode *Released[MAX_RELEASED_AT_SAME_TIME];
	resch_task_t *rt = getCurrentTask();//get current running task
	server_t *completed_server = &SERVERS[rt->server_id];//and its current running server
	
    	cpu = completed_server->cpu;//;smp_processor_id();
	debug_overhead_measures("deferred_memory_work start %ld\n", dmw_count);
	
	//printk("deferred_memory_work timer.expires value is %lu curr core %d \n ", per_cpu[completed_server->cpu].timer.expires, smp_processor_id());
	debug_trace_memsched("deferred_memory_work: server%d completed at_jiffies %lu with_mem %d at_core %d\n ", completed_server->id , jiffies, completed_server->mem_budget - completed_server->rem_mem_budget, cpu);
	
	// Remove this server from the ready queue
	//printk("deferred_memory_work:: server%d dequeued at jiffie %lu curr core %d \n ", completed_server->id , jiffies, smp_processor_id());
	highest_prio_server = bitmap_retrieve(&per_cpu[cpu].SERVER_READY_QUEUE);
	if (highest_prio_server == NULL) {
		printk(KERN_WARNING "HSF (deferred_memory_work:): 1Server ready queue empty!!! (%lu) curr core %d\n", jiffies, smp_processor_id());
		return;
	}
	if (highest_prio_server->id != completed_server->id) {
		printk(KERN_WARNING "HSF (deferred_memory_work:): Wrong expired server!!! (id:%d,%d) (%lu) curr core %d\n", highest_prio_server->id, completed_server->id, jiffies, smp_processor_id());
		destroy_timer_on_stack(&per_cpu[cpu].timer);
		return;
	}
	
	//below adjust the virtual time set in the future, to now of the release queue, because of mem budget depletion...	
	debug_trace_memsched("deferred_memory_work: virtual_time for core %d changed from %d\n", highest_prio_server->cpu ,per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);//print old vir time		
	per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = jiffies - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set;//take the difference btween previous set and current time.
	//printk(" 1:%d ",per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);
	per_cpu[cpu].prev_virt_time += per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;//add the difference to the total virtual time
	//printk(" 2:%d ",per_cpu[cpu].prev_virt_time);
	//below a check if prev is greater than 32. virtual time is never greater than 32 so after 32 start again at zero.
	wrap_value = per_cpu[cpu].SERVER_RELEASE_QUEUE.bitmap_size * INT_SIZE_BIT;//the value where to wrap		
	//nas: printk("wrap value is %d \n", wrap_value);
	if(per_cpu[cpu].prev_virt_time > wrap_value) per_cpu[cpu].prev_virt_time = per_cpu[cpu].prev_virt_time - wrap_value;//wrap the value
	per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].prev_virt_time;//store the new total vir time in virtual time		
	//nas: printk(" to %d curr core %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time, smp_processor_id());//print new vir time just calculated
	
			
	//printk("deferred_memory_work: timer removed with exp time: %lu curr core %d \n",per_cpu[completed_server->cpu].timer.expires , smp_processor_id());  
	// Deallocate the timer in this completed server...
	destroy_timer_on_stack(&per_cpu[cpu].timer);

	// Store task event (for tracing purposes)
	active_queue_lock(0, &flags);
	prev = active_highest_prio_task(0);
	active_queue_unlock(0, &flags);

	for (i = 0; i < completed_server->nr_of_tasks; i++) {
		if (completed_server->resch_task_list[i]->task->state != TASK_UNINTERRUPTIBLE) { // It was running...TASK_RUNNING
						//printk("deferred_memory_work: task%s dequeued at jiffie %lu curr core %d \n", completed_server->resch_task_list[i]->task->comm , jiffies, smp_processor_id());
                        completed_server->resch_task_list[i]->hsf_flags |= SET_BIT(ACTIVATE);
                        dequeue_task(completed_server->resch_task_list[i]);
                        completed_server->resch_task_list[i]->task->state = TASK_UNINTERRUPTIBLE;
                        set_tsk_need_resched(completed_server->resch_task_list[i]->task);
                }

	}smp_mb();
	completed_server->running = FALSE;

	event = 0;
	relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
	node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
	if (per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time == event) { // Check if any servers should be released...

		relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &i); // Check the value of the second element in the release queue

		if (i != event) { // Only one server to release...
		// printk("Event is %d curr core %d \n", event, smp_processor_id());
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event+SERVERS[get_sid(cpu,node->index)].period, node);

			// Refill budget			
			SERVERS[get_sid(cpu,node->index)].remain_budget = SERVERS[get_sid(cpu,node->index)].budget;
			
			//modified by MemSched group
			//memory
            SERVERS[get_sid(cpu,node->index)].rem_mem_budget = SERVERS[get_sid(cpu,node->index)].mem_budget;//mem budget
			
			// Insert the server in the ready queue
			debug_trace_memsched("deferred_memory_work:: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,node->index)].id , jiffies, SERVERS[get_sid(cpu,node->index)].cpu);
			bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,node->index)]));
			//printk(KERN_WARNING "S jiffies: %lu\tsid: %d\tRELEASED_\tat_core: %d\n", jiffies, SERVERS[get_sid(cpu,node->index)].id, SERVERS[get_sid(cpu,node->index)].cpu);	
			store_event(NULL, NULL, &(SERVERS[per_cpu[cpu].servers[node->index]]), &(SERVERS[per_cpu[cpu].servers[node->index]]), 2, jiffies, FALSE, TRUE);

			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			//printk("deferred_memory_work: event is %d curr core %d \n", event, smp_processor_id());
			//printk("deferred_memory_work: virtual time  is %d curr core %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time, smp_processor_id());
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
			//printk("deferred_memory_work: next relative event is %d curr core %d \n", next_relative_event, smp_processor_id());
		}
		else { // More than one server has to be released...
			next_relative_event = event;
			Released[0] = node;

			// Refill budget
			SERVERS[get_sid(cpu,Released[0]->index)].remain_budget = SERVERS[get_sid(cpu,Released[0]->index)].budget;
			
			//modified by MemSched group
			//memory
			SERVERS[get_sid(cpu,Released[0]->index)].rem_mem_budget = SERVERS[get_sid(cpu,Released[0]->index)].mem_budget;// mem budget			

			// Insert the server in the ready queue
			debug_trace_memsched("deferred_memory_work: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,Released[0]->index)].id , jiffies, SERVERS[get_sid(cpu,Released[0]->index)].cpu);
			bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,Released[0]->index)]));

			store_event(NULL, NULL, &(SERVERS[get_sid(cpu,Released[0]->index)]), &(SERVERS[get_sid(cpu,Released[0]->index)]), 2, jiffies, FALSE, TRUE);

			i = 1;
			while (TRUE) {
				event = 0;
				relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				Released[i] = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);

				// Refill budget
				SERVERS[get_sid(cpu,Released[i]->index)].remain_budget = SERVERS[get_sid(cpu,Released[i]->index)].budget;
				
				//modified by MemSched group
				//memory
                SERVERS[get_sid(cpu,Released[i]->index)].rem_mem_budget = SERVERS[get_sid(cpu,Released[i]->index)].mem_budget;// mem budget

				debug_trace_memsched("deferred_memory_work: server%d released at_jiffies %lu at_core %d\n", SERVERS[get_sid(cpu,Released[i]->index)].id , jiffies, SERVERS[get_sid(cpu,Released[i]->index)].cpu);
				// Insert the server in the ready queue
				bitmap_insert(&per_cpu[cpu].SERVER_READY_QUEUE, &(SERVERS[get_sid(cpu,Released[i]->index)]));
				//printk(KERN_WARNING "S jiffies: %lu\tsid: %d\tRELEASED_\tat_core: %d\n", jiffies, SERVERS[get_sid(cpu,Released[i]->index)].id, SERVERS[get_sid(cpu,Released[i]->index)].cpu);	
				store_event(NULL, NULL, &(SERVERS[get_sid(cpu,Released[i]->index)]), &(SERVERS[get_sid(cpu,Released[i]->index)]), 2, jiffies, FALSE, TRUE);

				i++;
				relPq_peek(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
				if (event != next_relative_event || event < 0)
					break;
			}
			
			// Now update and insert all elements in the release queue...
			for (j = 0; j < i; j++) {
				//printk("_____EVEVNT is %d curr core %d \n", next_relative_event, smp_processor_id());
				relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, next_relative_event+SERVERS[get_sid(cpu,Released[j]->index)].period, Released[j]);
			}

			// Fetch next release event...
			event = 0;
			relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			node = relPq_retrieve(&per_cpu[cpu].SERVER_RELEASE_QUEUE, &event);
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
			next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;
		}
	}
	else { // No servers to release...
		relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, event, node);
		//printk("Event is %d curr core is %d \n", event, smp_processor_id());
		//printk("SERVER_RELEASE_QUEUE.virtual_time is %d curr core %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time, smp_processor_id());
	
		//when virtual time is 52 and next event is release at 8(after swap at 64) the following must be  done:
		//next rel event becomes: the time between virtual time and swap + the event(time after swap)= 12 + 8 = 20
		//so the timer will be set 20 jiffies later
		//modified by MemSched
		//memory group------------------------------------------------------------------------------------------------------------------
		//nas: printk("deferred mem work 1: event is %d \n", event);	
		//nas: printk("deferred mem work 1: virtual time is %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);		
		if(event < per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time){
			wrap_value = per_cpu[cpu].SERVER_RELEASE_QUEUE.bitmap_size * INT_SIZE_BIT;//the value where to wrap	
			//printk("deferred mem work 1: wrap value is %d \n", wrap_value);	
			//printk("wrap value is %d \n", wrap_value);
			 next_relative_event = ((wrap_value-per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time) + event);
			//printk("deferred mem work 1: next_relative_event is %d \n", next_relative_event);	
		}
		else next_relative_event = event - per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time;//else do normal calculation	
	}

	/* Fetch the highest priority server */
	highest_prio_server = bitmap_get(&per_cpu[cpu].SERVER_READY_QUEUE);

	if (highest_prio_server == NULL) { // No ready server, then we idle...
		//printk("deferred_memory_work: Highest prior server is null ... now idle curr core %d \n ", smp_processor_id());
		// Store server event (for tracing purposes)
		idle.id = 1000;
		store_event(NULL, NULL, completed_server, &idle, 0, jiffies, FALSE, TRUE);
		//printk(KERN_WARNING "server_complete: %lu %d COMPLETED %d\n", jiffies, completed_server->id,completed_server->cpu);

		// Store task event (for tracing purposes)
		temp.pid = 0;
		if (prev != NULL)
			store_event(prev, &temp, NULL, NULL, 3, jiffies, FALSE, TRUE);
			
		//printk("deferred_memory_work:: timer set1 with exp time: %lu curr core %d \n",(jiffies+next_relative_event), smp_processor_id());

		//modified by MemSched group
		//memory
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
		
		setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[per_cpu[cpu].servers[node->index]]);
		
		//modified by MemSched group
		//memory
		ret1 = 0;
		ret1 = mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		if(ret1)printk("ERROR IN MOD TIMER_______________________________-\n");
		
		//modified by MemSched
		//memory group------------------------------------------------------------------------------------------------------------------------------------------------------------
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;//this is needed to adjust virtual time when mem budget depletes earlier than timer
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;
		wrap_value = per_cpu[cpu].SERVER_RELEASE_QUEUE.bitmap_size * INT_SIZE_BIT;//the value where to wrap	
		//printk("wrap value is %d \n", wrap_value);
		//when virtual becomes for example 72 with the new rel event added and swap is on 64 the new virtual time becomes 72-64
		if(per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time > wrap_value){
			//nas:printk("deferred mem work 2: virtual time is %d \n", per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time);	
			//nas:printk("deferred mem work 2: wrap value is %d \n", wrap_value);	
		per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time -= wrap_value;//in case of swapping this values must also swap just like the release queue see: No servers to release above ....	
		}

		smp_mb();
		debug_overhead_measures("deferred_memory_work stop idling %ld\n", dmw_count);
		//printk("deferred_memory_work: timer set11 with exp time: %lu curr core %d \n",per_cpu[cpu].timer.expires, smp_processor_id());
		return;
	}
	//printk(KERN_WARNING "S jiffies: %lu\tsid: %d\tSTARTED_\tat_core: %d\n", jiffies, highest_prio_server->id, highest_prio_server->cpu);
	//printk("deferred_memory_work: New Highest prior server is NOT null... tasks now enqueuing curr core %d \n ", smp_processor_id());
	// Release the highest prio servers tasks
	
	smp_mb();
	for (i = 0; i < highest_prio_server->nr_of_tasks; i++) {

		// If task was ready before, then make it ready now
		if ( (highest_prio_server->resch_task_list[i]->hsf_flags & SET_BIT(ACTIVATE)) == SET_BIT(ACTIVATE)) {
			//printk("deferred_memory_work:: task%s enqueued at jiffies %lu curr core %d \n", highest_prio_server->resch_task_list[i]->task->comm , jiffies, smp_processor_id());
			wake_up_process(highest_prio_server->resch_task_list[i]->task);
			highest_prio_server->resch_task_list[i]->exec_time = 0;
			highest_prio_server->resch_task_list[i]->task->utime = 0;
			enqueue_task(highest_prio_server->resch_task_list[i]);
		}
	}

	// Store task event (for tracing purposes)
	active_queue_lock(0, &flags);
	next = active_highest_prio_task(0);
	active_queue_unlock(0, &flags);
	temp.pid = 0;
	if (prev != NULL && next != NULL)
		store_event(prev, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
	else if (prev == NULL && next != NULL)
		store_event(&temp, next, NULL, NULL, 3, jiffies, FALSE, TRUE);
	else if (prev != NULL && next == NULL)
		store_event(prev, &temp, NULL, NULL, 3, jiffies, FALSE, TRUE);

	// Store server event (for tracing purposes)
	store_event(NULL, NULL, completed_server, highest_prio_server, 0, jiffies, FALSE, TRUE);

	highest_prio_server->budget_expiration_time = jiffies+highest_prio_server->remain_budget;
	highest_prio_server->timestamp = jiffies; // Set timestamp in case of preemption (budget accounting)
	highest_prio_server->running = TRUE;

	// Start next expiration timer...
	next_relative_event = next_scheduling_event(next_relative_event, highest_prio_server->remain_budget);

	if (next_relative_event == highest_prio_server->remain_budget) {
		//printk("deferred_memory_work:: timer set2 with exp time: %lu curr core %d \n",highest_prio_server->budget_expiration_time, smp_processor_id());
		//modified by MemSched group
		//memory
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
		setup_timer_on_stack(&per_cpu[cpu].timer, server_complete_handler, (unsigned long)highest_prio_server);
		ret2 = 0;
		ret2 = mod_timer(&per_cpu[cpu].timer, highest_prio_server->budget_expiration_time);
		if(ret2)printk("ERROR IN MOD TIMER_______________________________-\n");
		smp_mb();
	}
	else {
		//printk("deferred_memory_work: timer set3 with exp time: %lu curr core %d \n",(jiffies+next_relative_event), smp_processor_id());
		//modified by MemSched group
		//memory
		del_timer(&per_cpu[cpu].timer);
		destroy_timer_on_stack(&per_cpu[cpu].timer);
		
		setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)&SERVERS[per_cpu[cpu].servers[node->index]]);

		ret3 = 0;
		ret3 = mod_timer(&per_cpu[cpu].timer, (jiffies+next_relative_event));
		if(ret3)printk("ERROR IN MOD TIMER_______________________________-\n");
		smp_mb();
	}
	//modified by MemSched group
	//memory
	per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time_set = jiffies;//this is needed to adjust virtual time when mem budget depletes earlier than timer
	per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time = per_cpu[cpu].SERVER_RELEASE_QUEUE.virtual_time + next_relative_event;
	//printk("deferred_memory_work: scheduler called?????????????? cur core %d  \n", smp_processor_id());
	debug_overhead_measures("deferred_memory_work stop new_server %ld\n", dmw_count);
}

/** 
* This is an interrupt handler for CACHE MISSES caused by PMU of CPU hardware system. It is triggered every SAMPLE_RATE(default 1). 
* The handler overhead is avg. 60 nanosecond. If sampled for SAMPLE_RATE = 1, it could produce an overhead of 6% for a 1ms execution and
* 1000 CACHE MISSES. And this is more than server_complete/release_handler and deferred_memory_work. Therefore, depending on how much tasks are creating
* CACHE MISSES, the SAMPLE_RATE need to be set appropriate value.
*/
void memory_overflow_handler(struct perf_event * event,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
                                    int nmi,
#endif
                                    struct perf_sample_data * data,
                                    struct pt_regs * regs
                                    )
{
    	resch_task_t *rt;
	mof_timestamp_state = 0;
    	rt = getCurrentTask();//this function is an extern one. It is in resch.c

	if(SERVERS[rt->server_id].running == TRUE ){

		SERVERS[rt->server_id].rem_mem_budget -= SAMPLE_RATE;
		SERVERS[rt->server_id].total_mem_request += SAMPLE_RATE;
		rt->total_mem_request += SAMPLE_RATE;
		rt->periodic_mem_request += SAMPLE_RATE;
		getnstimeofday(&start_ts);
	 		
	}else{
		++SERVERS[rt->server_id].wrong_interrupts;	
//		printk("Mem Overflow handler: Interrupt generated by task%s and server %d at jiffie %lu server not running cur core %d wrong_interrupts %d ?????\n", rt->task->comm, SERVERS[rt->server_id].id,  jiffies, smp_processor_id(), SERVERS[rt->server_id].wrong_interrupts);	
	}

#ifdef USE_MEMSCHEDx
	if(SERVERS[rt->server_id].rem_mem_budget <= 0){
		printk("____Memory overflow handler: server%d expired at_jiffie %lu at_core %d\n", SERVERS[rt->server_id].id, jiffies, smp_processor_id());	
		++SERVERS[rt->server_id].number_of_mem_overflows;
		//shrink complete server handler for the server the task belongs to 
		irq_work_queue(&SERVERS[rt->server_id].deferred_work);//when this function is not called the scheduler will not be called automatically	
	
		getnstimeofday(&stop_ts);
		debug_mof("Memory_overflow_handler including_deferred_work %lu %lu", start_ts, stop_ts);

	}else
	{
		debug_mof("Memory_overflow_handler only_memory_overflow_handler %lu %lu", start_ts, stop_ts);

	}	
#endif
}
	
/**
* this method is needed for memsched to create a counter event for a task
* modified by MemSched group
*/
static struct perf_event *init_counter(struct task_struct * task , int cpu)
{
	//printk("init_counter: New counter created for task%s and cpu%d \n", task->comm , cpu);
  struct perf_event *event = NULL;
  struct perf_event_attr sched_perf_hw_attr = {
          .type           = PERF_TYPE_HARDWARE,
          .config         = PERF_COUNT_HW_CACHE_MISSES,
          .size           = sizeof(struct perf_event_attr),
//          .pinned         = 1,
          .disabled       = 0,
          .exclude_kernel = 1,
          .pinned         = 1,
  };
  debug_overhead_measures("init_counter start %d\n", 0);

  sched_perf_hw_attr.sample_period  = SAMPLE_RATE;//this means call memory_overflow_handler at each cacha miss

  /* Try to register using hardware perf events */
  event = perf_event_create_kernel_counter(
          &sched_perf_hw_attr,
          cpu, task,
          memory_overflow_handler
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0) 
          , NULL
#endif
          );

          if(!event || IS_ERR(event))
          {
            printk(KERN_ERR "INIT_COUNTER   Failed to create perf event for cpu %d \n", cpu);
		debug_overhead_measures("init_counter stop counter %d\n", 0);
            return NULL;
          }			
	  
          smp_mb();//make sure all writes to memory operations are done at this point 
	debug_overhead_measures("init_counter stop counter %d\n", 0);

  return event;
}

void task_run(resch_task_t *rt) {
	debug_overhead_measures("task_run start %d\n", 0);
	//printk("task_run plugin:  curr core %d \n",  smp_processor_id());
	//printk(KERN_WARNING "T jiffies: %lu pid: %d CREATED at_sid: %d at_core: %d", jiffies, rt->task->pid, SERVERS[rt->server_id].id, SERVERS[rt->server_id].cpu);
	migrate_task(rt, SERVERS[rt->server_id].cpu); // Migrate all tasks to CPU:0 for now...

	// Needed by the trace recorder...
	debug_trace_memsched("task_create: %d %s %d %d %d %d\n", rt->pid, rt->task->comm, rt->prio, rt->cpu_id, rt->server_id, SERVERS[rt->server_id].cpu);

	//printk(KERN_INFO "HSF: TASK_RUN!\n");

	// Insert the task in its server
	if (rt->server_id >= 0 && rt->server_id < NR_OF_SERVERS) {
		SERVERS[rt->server_id].resch_task_list[SERVERS[rt->server_id].nr_of_tasks] = rt;
		SERVERS[rt->server_id].nr_of_tasks++;
	}
	else
		printk(KERN_WARNING "HSF (task_run): Task has no valid server ID!!!\n");
		
		//modified by MemSched group
		//memory
#ifdef USE_MEMSCHED
        preempt_disable();
        rt->event = init_counter(rt->task , SERVERS[rt->server_id].cpu);//create the counting event for this task in the cpu's PMU	
        smp_mb();//commit all memory operating before this point
        preempt_enable();		
#endif

	rt->hsf_flags = 0; // Reset all the flags!!!
	rt->hsf_flags |= SET_BIT(PREVENT_RELEASE_AT_INIT); // Setting this bit will prevent the task from being released after 'api_run'
	
	debug_overhead_measures("task_run stop run %d\n", 0);

}

/********** Maps: server ID per_core to server ID in the global, SERVERS for example: server indexed at 0 could be 3 at SERVERS index****/
//modified by MemSched group
//multicore
static int get_sid(int cpu, int relative_id){

	return (per_cpu[cpu].servers[relative_id]);
}

//modified by MemSched group
//multicore
/*******test handler */
void testHandler(unsigned long data){
server_t *server = (server_t *)data;
	printk(KERN_WARNING "hptr: %p hperiod: %d\n", per_cpu[0].highest_prio_server, per_cpu[0].highest_prio_server->period);
	printk(KERN_WARNING "server period: %d\n", server->period);
printk(KERN_WARNING "TEST HANDLER:\n");
}

static int __init memsched_init(void) {

	printk("MEMSCHED: HELLO !!");
	
	int i;
	int cpu;
	int ret;
	int periodx;
	
	/* multicore: initialize the cores */
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++){
	
		per_cpu[cpu].s_id = 0;
	}
	
	printk(KERN_WARNING "NR OF RT CPUS =  %d\n", NR_RT_CPUS);
	/*multicore: for active cpus */
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++){
	
		init_pq(&per_cpu[cpu].SERVER_READY_QUEUE);
		per_cpu[cpu].start = 0;
		per_cpu[cpu].FIRST_RUN = TRUE;
		per_cpu[cpu].firstime = TRUE;
	}

	mikaoverhead = 0; // Needed for tracer overhead measurements...
	linux_timestamp_microsec(1); // Needed for tracer overhead measurements...
	
	/**************** Initialize the servers ('SERVERS' is a global variable) ****************/
	//mem budget calculation: for one loop through 85706 nodes = 1 job = 1 exec time 1 = 200 misses
	//so runtime * 200 is total cache misses per server

#if EXPERIMENT == 0
	printk("EXPERIMENT: CASE STUDY!!");
	// core-0 	
	init_server(&SERVERS[0], 0, 15, 10 , 0, 0, 2000000); // id,period(ms),budget(ms),prio,core,budget(UNBOUNDED mem)*/
	// core-1	
	init_server(&SERVERS[1], 1, 60, 10 , 1, 1, 500); // memory bandwidth throttled to 200
	init_server(&SERVERS[2], 1, 80, 12 , 2, 1, 500); // memory bandwidth throttled to 200

#elif EXPERIMENT == 1
	printk("EXPERIMENT: 1 (all tasks are normal tasks)!!");
	//core-0 
	init_server(&SERVERS[0], 0, 24, 8 , 0, 0,650); // id,period(ms),budget(ms),prio,core,budget(mem)
	init_server(&SERVERS[1], 1, 40, 16, 1, 0,750); 	
	// core-1 
	init_server(&SERVERS[2], 2, 60, 8 , 2, 1,900); 
	init_server(&SERVERS[3], 3, 80, 12, 0, 1,1100); 
	init_server(&SERVERS[4], 4, 80, 12, 3, 1,1100); 
	
#elif EXPERIMENT == 2 
	printk("EXPERIMENT: 2 (all tasks are 'normal tasks' except T3 which is 'problem task')!!");
	//core-0 
	init_server(&SERVERS[0], 0, 24, 8 , 0, 0,650); // id,period(ms),budget(ms),prio,core,budget(mem)
	init_server(&SERVERS[1], 1, 40, 16, 1, 0,100); 
	// core-1 
	init_server(&SERVERS[2], 2, 60, 8 , 2, 1,900); 
	init_server(&SERVERS[3], 3, 80, 12, 0, 1,1100); 
	init_server(&SERVERS[4], 4, 80, 12, 3, 1,1100); 
#else
	printk("DEFAULT EXPERIMENT!!");
        //core-0 
        //init_server(&SERVERS[0], 0, 15, 10 , 0, 0,650); // FOR MPLAYER CACHE MISS COUNT
        init_server(&SERVERS[0], 0, 12, 8, 0, 0,650); // FOR NORMAL AND PROBLEM TASK CACHE MISS COUNT

        // core-1 
//        init_server(&SERVERS[1], 1, 60, 8 , 1, 1,900);

#endif	

	printk(KERN_WARNING "s_id/max: %d\n", per_cpu[0].s_id);
	
	// Store server event (for tracing purposes)
	for (i = 0; i < NR_OF_SERVERS; i++) {
		printk(KERN_WARNING "server_create: %d Server%d core %d budget %d period:%d\n", i,i,SERVERS[i].cpu,SERVERS[i].budget*4000, SERVERS[i].period);
	}
	
	// Store task event (for tracing purposes)
	printk(KERN_WARNING "task_create: 0 idle 0 0\n");

	/**************** Put the servers in the server ready queue ****************/
	for (i = 0; i < NR_OF_SERVERS; i++) {
		/* multicore: put server in ther ready queue */
		bitmap_insert(&per_cpu[SERVERS[i].cpu].SERVER_READY_QUEUE, &SERVERS[i]);
	}
	// Now remove the highest prio ready server (it will be inserted by the 'server_release_handler')
	/* multicore: remove highest prio from the ready queue */
	for(cpu = 0; cpu < NR_RT_CPUS; ++cpu){
		if ( (per_cpu[cpu].highest_prio_server = bitmap_retrieve(&per_cpu[cpu].SERVER_READY_QUEUE)) == NULL) {


			printk(KERN_WARNING "core: %d,HSF (hsf_init): Server ready queue empty!!!\n", cpu);
			return 0;
		}
		printk(KERN_WARNING "HP SERVER: %d\n", per_cpu[cpu].highest_prio_server->period);
	}
	// Initialize nodes that will reside in the release queue structure...
	/* multicore: init nodes that will reside in the release queue */
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++) {

		printk(KERN_WARNING "core: %d nr_servers: %d\n", cpu, per_cpu[cpu].s_id);
		for (i = 0; i < per_cpu[cpu].s_id; i++) {

			per_cpu[cpu].RelNodes[i].index = i;
			per_cpu[cpu].RelNodes[i].next = NULL;
			node_map[node_map_index++] = i;
			printk(KERN_WARNING "cpu: %d node: %d\n", cpu, per_cpu[cpu].RelNodes[i].index);
		}
	}

	// Initialize release queue
	/* multicore: init release queue */
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++){		
		periodx = find_largest_period(SERVERS, NR_OF_SERVERS, cpu); 
		printk(KERN_WARNING "Largest period: %d\n", periodx);
		if ( (relPq_init(&per_cpu[cpu].SERVER_RELEASE_QUEUE, periodx)) < 0) {
			printk(KERN_WARNING "core: %d, 'relPq_init' failed!!!\n",cpu);
			return 0;
		}
	}

	// Insert the release queue nodes..
	/* multicore: insert release queue nodes */
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++){
		for (i = 0; i < per_cpu[cpu].s_id; i++) {
			printk(KERN_WARNING "SID TO BE INSERTED: %d\n", get_sid(cpu,i));
			relPq_insert(&per_cpu[cpu].SERVER_RELEASE_QUEUE, SERVERS[get_sid(cpu,i)].period, &per_cpu[cpu].RelNodes[i]);
		}
	}
	
	for(cpu = 0; cpu < NR_RT_CPUS; ++cpu)
	{
		setup_timer_on_stack(&per_cpu[cpu].timer, server_release_handler, (unsigned long)(per_cpu[cpu].highest_prio_server));
		ret = mod_timer(&per_cpu[cpu].timer, (jiffies+msecs_to_jiffies(SYSTEM_TIMEOUT))); // Start in 10 seconds...
		if (ret){
			printk(KERN_WARNING "timer %d install problem.", cpu);
			return 0;
		}
		printk(KERN_WARNING "timer is set for core = %d\n", cpu);
	}
	timer_setup = 1;
	scheduler_called = 1;
	//printk("before scheduler!\n");
	printk("___________________END OF INIT MODULE! curr core %d \n", smp_processor_id());
	// Install our plugins...
	install_scheduler(task_run, NULL, task_release, task_complete);
	
	return 0;
}

static void __exit memsched_exit(void) {

	int cpu;
	int i, j;
	spinlock_t wakeuplock;

	spin_lock_init(&wakeuplock);

	
#ifdef USE_MEMSCHED
	//destroy the counter events of memsched for each and all tasks otherwise the system will crash when unloading the module
	for(i = 0; i < NR_OF_SERVERS; i++){//loop through all servers
		int j;		
		for(j = 0; j < SERVERS[i].nr_of_tasks; j++){//loop through all server tasks
			//stop perf event counters and release it...
			BUG_ON(!SERVERS[i].resch_task_list[j]->event);
			SERVERS[i].resch_task_list[j]->event->pmu->stop(SERVERS[i].resch_task_list[j]->event, PERF_EF_UPDATE);
			smp_mb();
			SERVERS[i].resch_task_list[j]->event->destroy(SERVERS[i].resch_task_list[j]->event);
			smp_mb();
			perf_event_release_kernel(SERVERS[i].resch_task_list[j]->event);
			smp_mb();
		}
	}
#endif

	spin_lock(&wakeuplock);
	printk("exit_hsf(): Delete Core timers that raise server_release_handler and server_complete_handler.\n");

	for (cpu = 0; cpu < NR_RT_CPUS; ++cpu)
	{

		del_timer_sync(&per_cpu[cpu].timer);

	}


	for ( i = 0; i < NR_OF_SERVERS; ++i ){

		for (j = 0; j < SERVERS[i].nr_of_tasks; ++j){	

		printk("memsched_exit(): task %d state %ld\n", SERVERS[i].resch_task_list[j]->task->pid, SERVERS[i].resch_task_list[j]->task->state);
				if (wake_up_process(SERVERS[i].resch_task_list[j]->task) == 1)
				{
                                	enqueue_task(SERVERS[i].resch_task_list[j]);
		                }//endof wakeup
                		else{
		                        printk("memsched_exit(): cannot wakeup task. Task already running\n");
                		}
		}

	}
	smp_mb();
	spin_unlock(&wakeuplock);

	
	printk(KERN_ERR "MODULE REMOVING NOW________________________________________________________\n");
	// Get the number of cache misses from each server and tasks 
	for (i = 0; i < NR_OF_SERVERS; ++i)
	{
		printk("SUMMARY NMR = %ld S%d number of tasks = %d number of mem overflows = %d\n", 
			SERVERS[i].total_mem_request, i, SERVERS[i].nr_of_tasks, SERVERS[i].number_of_mem_overflows);	
		for (j = 0;j < SERVERS[i].nr_of_tasks; ++j)
		{
			printk("SUMMARY: task name: %s pid: %d has missed %d deadlines in %d of activations and generated NMR=%ld\n", 
				SERVERS[i].resch_task_list[j]->task->comm, SERVERS[i].resch_task_list[j]->pid, 
				SERVERS[i].resch_task_list[j]->deadlinemiss_count, SERVERS[i].resch_task_list[j]->activation_count,
				SERVERS[i].resch_task_list[j]->total_mem_request);
		}
	}


	printk(KERN_ALERT "(HSF) overhead: %lu\n", mikaoverhead);
	  // Deallocate the release queue...
        /* multicore: destroy release queue */
        for (cpu = 0; cpu < NR_RT_CPUS; cpu++){
                relPq_destroy(&per_cpu[cpu].SERVER_RELEASE_QUEUE);
        }

	printk(KERN_INFO "Memsched: GOODBYE! (%lu)\n", jiffies);

	/* uninstall the plugins. */
	if (scheduler_called){
	uninstall_scheduler();
	}else printk(KERN_WARNING "Scheduler is not installed and exception is handled.");	

	// Print trace to log-file...
	store_event(NULL, NULL, NULL, NULL, 0, 0, TRUE, FALSE);
}

int next_scheduling_event(int release, int budget) {
	if (budget <= release)
		return budget;
	return release;

}

module_init(memsched_init);
module_exit(memsched_exit);
