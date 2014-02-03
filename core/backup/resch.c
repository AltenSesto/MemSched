/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
 * resch.c: The RESCH core module for preemptive real-time scheduling.
 *
 * The module must be able to access the following kernel functions:
 * - schedule()
 * - sched_setscheduler()
 * - wake_up_process()
 * - setup_timer_on_stack()
 * - mod_timer()
 * - del_timer_sync()
 * - destroy_timer_on_stack()
 * - send_sig()
 */

#include <linux/string.h>
#include <linux/cdev.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/resource.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include "api.h"
#include "bitops.h"
#include "config.h"
#include "core.h"
#include "tvops.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("RESCH");
MODULE_AUTHOR("Shinpei Kato");

#if LOAD_BALANCE_ENABLED
#define exec_time task->utime
#endif

#define MODULE_NAME	"resch"
#define MAX_BUFLEN	64

/* the offset of process IDs, from which RESCH can use. */
#define PID_OFFSET	(PID_MAX + 1)

/* the RESCH ID of task @p. */
#define rid(p)				((p)->pid - PID_OFFSET)
/* return pointer to the resch task descriptor. */
#define resch_task_ptr(p)	(&resch_task[rid(p)])
/* return the reversed value of the given priority. 
   this is used by priority arrays. */
#define reverse_prio(prio)	(RESCH_MAX_PRIO - prio)

/* verify if the task is managed by RESCH. */
#define task_is_managed(p) ((p)->pid >= PID_OFFSET)
/* verify if the task is running in the Linux kernel. */
#define task_is_running(rt) ((rt)->task->state == TASK_RUNNING)
/* verify if the task is in the active queue. */
#define task_is_active(rt) (!list_empty(&(rt)->active_entry))
/* verify if the task has been submitted in RESCH. */
#define task_is_submitted(rt) ((rt)->release_time > 0)
/* verify if the task made a resource reservation. */
#define reservation_is_requested(rt) ((rt)->reservation_time > 0)
/* verify if the task is still in reservation. */
#define resource_is_reserved(rt) (rt)->reserved

/* device number. */
static dev_t dev_id;
/* char device structure. */
static struct cdev c_dev;

/**
 * the task descriptor used in the RESCH core. 
 */
resch_task_t resch_task[NR_RT_TASKS];

/**
 * context switch and migration costs by microseconds (usecs).
 */
unsigned long switch_cost = 0;
unsigned long migration_cost = 0;

/**
 * a bitmap for the process IDs used in RESCH (RESCH IDs).
 * those PIDs range in [0, NR_RT_TASKS-1], which are different from 
 * the original PIDs used in Linux (Linux PIDs) but are somewhat 
 * linked (not explained in detail here).
 * each process has a RESCH PID within the range of [0, NR_RT_TASKS-1].
 * if the k-th bit is set, the ID of k has been used for some process.
 */
#define PID_MAP_LONG BITS_TO_LONGS(NR_RT_TASKS)
struct pid_map_struct {
	unsigned long bitmap[PID_MAP_LONG];
	spinlock_t lock;
};

/**
 * a priority-ordered double-linked list, which includes all the submitted
 * tasks regardless of its running status.
 * this global list is often accessed before tasks are submitted, like
 * for response time analysis, and needs not to provide a timing-critical 
 * implementation. so we use a semaphore to lock for synchronization.
 */
struct task_list_struct {
	struct semaphore sem;
	struct list_head head;
};

/**
 * a priority array which includes only active (ready) tasks.
 * each element is composed of a double-linked list.
 * since this active list is often accessed when tasks are released and
 * complete, it is timing-critical. so we use a priority array that is
 * also implemented in the Linux scheduler that provides O(1) queuing.
 * we also use a spinlock for synchironization.
 */
#define RESCH_PRIO_LONG BITS_TO_LONGS(MAX_RT_PRIO)
struct prio_array {
	int nr_tasks;
	spinlock_t lock;
	unsigned long bitmap[RESCH_PRIO_LONG];
	struct list_head queue[RESCH_MAX_PRIO];
};

/**
 * tick information that holds the latest scheduling point.
 * this is used to trace the worst-case execution time at runtime.
 * this is also used by job-level dynamic-priority scheduling algorithms.
 */
struct tick_struct {
	unsigned long last_tick;
	spinlock_t lock;
};

/**
 * a kernel thread that calls sched_setscheduler() exported from the Linux
 * kernel to change the scheduling policy and the priority of the tasks.
 * this kernel thread is necessary because the Linux kernel does not allow
 * a user process with no root permissions to change its priority to the
 * real-time one.
 * however, we may need to take into accout security matters...
 */
struct setscheduler_thread_struct {
	struct task_struct *task;
	struct list_head list;
	spinlock_t lock;
};

/**
 * this is used to put tasks to the waiting list for setscheduler_thread.
 */
struct setscheduler_req {
	int prio;
	struct resch_task_struct *rt;
	struct list_head list;
};

/**
 * the global object. 
 */
struct global_object {
	struct pid_map_struct pids;
	struct task_list_struct task_list;
} go;

/**
 * the local object for each CPU .
 */
struct local_object {
	struct prio_array active;
	struct tick_struct tick;
	struct resch_task_struct *current_task;
	struct setscheduler_thread_struct setscheduler_thread;
} lo[NR_RT_CPUS];

/**
 * scheduler plugins:
 * - task_run is called just before the first job is released.
 * - task_exit is called when it returns to a normal task.
 * - job_release is called at the end of job release.
 * - job_complete is called at the begging job completion.
 */ 
struct plugin_struct {
	void (*task_run)(resch_task_t*);
	void (*task_exit)(resch_task_t*);
	void (*job_release)(resch_task_t*);
	void (*job_complete)(resch_task_t*);
} resch_plugins;

/**
 * this is used to measure scheduling overheads.
 */
struct test_struct {
	struct timeval switch_begin;
	struct timeval switch_end;	
	struct timeval migration_begin;
	struct timeval migration_end;	
} test;

/**
 * Needed by Tracealyzer recorder.
 */
void taskIdListGet(int *idList, int size) {

	resch_task_t *task;
	int i;

	i = 0;

	list_for_each_entry(task, &go.task_list.head, global_entry) {
		idList[i] = task->rid;
		i++;
		if (i >= size) {
			printk(KERN_WARNING "RESCH: taskIdListGet cannot fill idList!!!\n");
			return;
		}
	}

}
EXPORT_SYMBOL(taskIdListGet);

/**
 * Needed by Tracealyzer recorder.
 */
char *taskName(int rid) {

	return resch_task[rid].name;

}
EXPORT_SYMBOL(taskName);

resch_task_t *taskTcb(int rid) {

	return &resch_task[rid];

}
EXPORT_SYMBOL(taskTcb);

/**
 * initialize the RESCH task descriptor members.
 */
static inline void resch_task_init(resch_task_t *rt)
{
	int cpu;

	rt->task = current;
	cpus_clear(rt->cpumask);
	for (cpu = 0; cpu < NR_RT_CPUS; cpu++) {
		cpu_set(cpu, rt->cpumask);
	}
	cpumask_copy(&rt->task->cpus_allowed, &rt->cpumask);

	rt->prio = RESCH_MIN_PRIO;
	rt->prio_index = reverse_prio(RESCH_MIN_PRIO);
	rt->cpu_id = smp_processor_id();
	rt->wcet = 0;
	rt->period = 0;
	rt->deadline = 0;
	rt->release_time = 0;
	rt->exec_time = 0;
	rt->preempter = NULL;
	rt->preemptee = NULL;
	rt->reserved = false;
	rt->xcpu = false;
	rt->reservation_time = 0;
	INIT_LIST_HEAD(&rt->global_entry);
	INIT_LIST_HEAD(&rt->active_entry);
}

/**
 * update the given CPU's tick, and also trace the execution time of 
 * the current task on the CPU, if any.
 */
static inline void update_tick(int cpu)
{
	unsigned long flags;
	resch_task_t *curr = lo[cpu].current_task;
	struct tick_struct *tick = &lo[cpu].tick;

	spin_lock_irqsave(&tick->lock, flags);
	if (curr) {
		/* the RESCH core traces the execution time by itself.
		   this is because the execution times traced in the Linux
		   kernel, such as utime and se.sum_exec_runtime, are not
		   precise when user applications are I/O intensive, due
		   to loss of scheduler_tick() invocations. */
		curr->exec_time += jiffies - tick->last_tick;
	}
	tick->last_tick = jiffies;
	spin_unlock_irqrestore(&tick->lock, flags);
}

/**
 * update the given CPU's tick associated with the given task, and also 
 * trace the execution time of the given task.
 * @rt must be a valid reference.
 */
static inline void update_task_tick(resch_task_t *rt)
{
	unsigned long flags;
	struct tick_struct *tick = &lo[rt->cpu_id].tick;

	spin_lock_irqsave(&tick->lock, flags);
	if (rt) {
		/* the RESCH core traces the execution time by itself.
		   this is because the execution times traced in the Linux
		   kernel, such as utime and se.sum_exec_runtime, are not
		   precise when user applications are I/O intensive, due
		   to loss of scheduler_tick() invocations. */
		rt->exec_time += jiffies - tick->last_tick;
	}
	tick->last_tick = jiffies;
	spin_unlock_irqrestore(&tick->lock, flags);
}

/**
 * request the setscheduler thread to change the priority of @rt. 
 * the caller will sleep until the priority is changed.
 */
static inline void request_change_prio(resch_task_t *rt, int prio)
{
	struct setscheduler_req req;
	int cpu = smp_processor_id();

	INIT_LIST_HEAD(&req.list);
	req.rt = rt;
	req.prio = prio;

	/* insert the task to the waiting list for sched_setscheduler(). */
	spin_lock_irq(&lo[cpu].setscheduler_thread.lock);
	list_add_tail(&req.list, &lo[cpu].setscheduler_thread.list);
	spin_unlock(&lo[cpu].setscheduler_thread.lock);

	/* wake up the migration thread. */
	wake_up_process(lo[cpu].setscheduler_thread.task);
	local_irq_enable();

	/* sleep here. it must be uninterruptible. */
	rt->task->state = TASK_UNINTERRUPTIBLE;
	schedule();
}

/**
 * request the setscheduler thread to change the priority of @rt from the
 * interrupt contexts. 
 * the caller will sleep until the priority is changed.
 */
static inline void request_change_prio_interrupt(resch_task_t *rt, int prio)
{
	struct setscheduler_req req;
	int cpu = smp_processor_id();
	unsigned long flags;

	INIT_LIST_HEAD(&req.list);
	req.rt = rt;
	req.prio = prio;

	/* insert the task to the waiting list for sched_setscheduler(). */
	spin_lock_irqsave(&lo[cpu].setscheduler_thread.lock, flags);
	list_add_tail(&req.list, &lo[cpu].setscheduler_thread.list);
	spin_unlock(&lo[cpu].setscheduler_thread.lock);

	/* wake up the migration thread. */
	wake_up_process(lo[cpu].setscheduler_thread.task);
	local_irq_restore(flags);

	/* the task is going to sleep. */
	rt->task->state = TASK_UNINTERRUPTIBLE;
	set_tsk_need_resched(rt->task);
}

#if !(LOAD_BALANCE_ENABLED)
/**
 * called when a task pointed to by @__data exhausts its budget in a
 * resource reservation mode. 
 * note that there is no need to check out the resource here.
 */
static void reservation_handler(unsigned long __data)
{
	resch_task_t *rt = (resch_task_t *)__data;

	if (rt->xcpu) {
		send_sig(SIGXCPU, rt->task, 0);
	}
	else {
		/* schedule the task in background. 
		   note that the CPU tick and task preemptions are traced when
		   the priority is changed in change_prio(). */
		request_change_prio_interrupt(rt, RESCH_BG_PRIO);
	}
}
#endif

/**
 * check in the resource reserved for the given task.
 */
static inline void check_in_resource(resch_task_t *rt)
{
	if (rt->exec_time <= rt->timeout) {
#if LOAD_BALANCE_ENABLED
		rt->task->rt.timeout = 0;
		rt->task->signal->rlim[RLIMIT_RTTIME].rlim_cur = 
			jiffies_to_usecs(rt->timeout - rt->exec_time);
#else
		setup_timer_on_stack(&rt->reservation_timer, 
							 reservation_handler, 
							 (unsigned long)rt);
		mod_timer(&rt->reservation_timer, 
				  jiffies + rt->timeout - rt->exec_time);	
#endif
		rt->reserved = true;
	}
}

/**
 * check out the CPU resource reserved for the given task.
 */
static inline void check_out_resource(resch_task_t *rt)
{
#if LOAD_BALANCE_ENABLED
	/* detach from the watchdog. */
	rt->task->signal->rlim[RLIMIT_RTTIME].rlim_cur = RLIM_INFINITY;
#else
	/* delete the timer. */
	del_timer_sync(&rt->reservation_timer);
	destroy_timer_on_stack(&rt->reservation_timer);
#endif
	rt->reserved = false;
}

/**
 * trace task preemptions when the current task is preempted by @rt.
 */
static inline void preempt_curr(resch_task_t *rt)
{
	update_tick(rt->cpu_id);
	/* we must compare the priorities because a higher-priority task
	   may be temporarily preempted by a lower one when ioctl() returns. */
	if (lo[rt->cpu_id].current_task && 
		lo[rt->cpu_id].current_task->prio < rt->prio) {
		rt->preemptee = lo[rt->cpu_id].current_task;
		rt->preemptee->preempter = rt;

		/* check out the resource temporarily when preempted. */
		if (resource_is_reserved(rt->preemptee)) {
			check_out_resource(rt->preemptee);
		}
		lo[rt->cpu_id].current_task = rt;
	}	
	else if (!lo[rt->cpu_id].current_task) {
		rt->preemptee = NULL;
		lo[rt->cpu_id].current_task = rt;
	}
}

/**
 * trace task preemptions when @rt finishes job execution.
 */
static inline void preempt_switch(resch_task_t *rt)
{
	update_task_tick(rt);

	/* note that rt->preemptee will be not necessarily scheduled next, 
	   if some higher-priority tasks have released jobs. in that case, 
	   this task will be preempted again. */
	lo[rt->cpu_id].current_task = rt->preemptee;
	if (lo[rt->cpu_id].current_task) {
		lo[rt->cpu_id].current_task->preempter = NULL;

		/* check in the resource for the current task, if requested. */
		if (reservation_is_requested(lo[rt->cpu_id].current_task)) {
			check_in_resource(lo[rt->cpu_id].current_task);
		}
	}
	rt->preemptee = NULL;
}

/**
 * trace task preemptions when @rt is migrated out a CPU.
 * @rt must be a valid reference.
 */
static inline void preempt_out(resch_task_t *rt)
{
	unsigned long flags;

	if (lo[rt->cpu_id].current_task == rt) {
		update_tick(rt->cpu_id);
		lo[rt->cpu_id].current_task = rt->preemptee;
	}

	if (rt->preempter) {
		rt->preempter->preemptee = rt->preemptee;
	}
	if (rt->preemptee) {
		rt->preemptee->preempter = rt->preempter;
	}
	rt->preemptee = NULL;
	rt->preempter = NULL;

	/* check out the reserved resource. */
	if (reservation_is_requested(rt)) {
		local_irq_save(flags);
		if (resource_is_reserved(rt)) {
			check_out_resource(rt);
		}
		local_irq_restore(flags);
	}
}

/**
 * trace task preemptions when @rt is migrated in a CPU.
 * @rt must be a valid reference.
 * the active queue must be locked.
 */
static inline void preempt_in(resch_task_t *rt)
{
	if ((lo[rt->cpu_id].current_task && 
		 lo[rt->cpu_id].current_task->prio < rt->prio) ||
		!lo[rt->cpu_id].current_task) {
		preempt_curr(rt);
	}
	else {
		/* the previous task in the active queue must be valid. */
		rt->preempter = active_prev_prio_task(rt);
		rt->preemptee = rt->preempter->preemptee;
		rt->preempter->preemptee = rt;
		if (rt->preemptee) {
			rt->preemptee->preempter = rt;
		}
	}
}

/**
 * attach the given task to RESCH.
 * note that Linux assigns a PID to each process in range of 
 * [1, pid_max], where pid_max is defined in /cpu/sys/pid_max.
 * in order to explicit that a process is managed by RESCH, we 
 * reassign a PID to the process in range of 
 * [pid_max + 1, pid_max + NR_RT_TASKS + 1].
 * we can link the process with PID=p to the resch_task[k] descripter, 
 * by k = p - (pid_max + 1).
 */
static inline int attach_resch(struct task_struct *p)
{
	int rid; /* resch id. */

	/* get the available RESCH ID. */
	if ((rid = resch_ffz(go.pids.bitmap, PID_MAP_LONG)) < 0) {
		return false;
	}

	/* set the bit for the corresponding RESCH ID. */
	__set_bit(rid, go.pids.bitmap);
	resch_task[rid].rid = rid;

	/* new PID is assigned to this process. note that this PID is 
	   in the range that is never used by the Linux kernel.
	   so we can assign sequential PIDs so that they can be mapped
	   to go.pids.bitmap[k]. 
	   remember we cannot use resch_task_ptr(p)->pid yet here! */
	resch_task[rid].pid = p->pid;
	p->pid = rid + PID_OFFSET;

	return true;
}

/**
 * detach the given task from the RESCH module. 
 */
static inline void detach_resch(struct task_struct *p)
{
	/* clear the bit for the corresponding RESCH ID. */
	__clear_bit(rid(p), go.pids.bitmap);

	/* restore the original linux pid. 
	   after restored, we cannot use rid(p) macro. */
	p->pid = resch_task_ptr(p)->pid;
}

/**
 * insert the given task into the active queue without lock.
 */
static inline void __enqueue_task(resch_task_t *rt)
{
	int idx = rt->prio_index;
	struct prio_array *active = &lo[rt->cpu_id].active;
	struct list_head *queue = &active->queue[idx];

	list_add_tail(&rt->active_entry, queue);
	__set_bit(idx, active->bitmap);
	active->nr_tasks++;
}

/**
 * remove the given task from the active queue without lock.
 */
static inline void __dequeue_task(resch_task_t *rt)
{
	int idx = rt->prio_index;
	struct prio_array *active = &lo[rt->cpu_id].active;
	struct list_head *queue = &active->queue[idx];

	list_del_init(&rt->active_entry);
	if (list_empty(queue)) {
		__clear_bit(idx, active->bitmap);
	}
	active->nr_tasks--;
}

/**
 * insert the given task into the active queue, safely with lock.
 */
static inline void enqueue_task(resch_task_t *rt)
{
	unsigned long flags;

	active_queue_lock(rt->cpu_id, &flags);
	__enqueue_task(rt);
	active_queue_unlock(rt->cpu_id, &flags);
}

/**
 * remove the given task from the active queue, safely with lock.
 */
static inline void dequeue_task(resch_task_t *rt)
{
	unsigned long flags;

	active_queue_lock(rt->cpu_id, &flags);
	__dequeue_task(rt);
	active_queue_unlock(rt->cpu_id, &flags);
}

/**
 * reinsert the given task into the active queue, safely with lock.
 */
static inline void requeue_task(resch_task_t *rt)
{
	unsigned long flags;

	active_queue_lock(rt->cpu_id, &flags);
#if !(LOAD_BALANCE_ENABLED)
	preempt_out(rt);
#endif
	__dequeue_task(rt);
	__enqueue_task(rt);
#if !(LOAD_BALANCE_ENABLED)
	/* we don't have to trace task preemptions, if the job has not been 
	   started yet, since they are precisely traced in job_start() . */
	if (rt->exec_time > 0) {
		preempt_in(rt);
	}
#endif
	active_queue_unlock(rt->cpu_id, &flags);
}

/**
 * insert @rt into the global list in order of priority.
 * the global list must be locked. 
 */
static inline void __global_list_insert(resch_task_t *rt)
{
	resch_task_t *p;

	/* we here insert @rt into the global list. */
	if (list_empty(&rt->global_entry)) {
		list_for_each_entry(p, &go.task_list.head, global_entry) {
			if (p->prio < rt->prio) {
				/* insert @rt before @p. */
				list_add_tail(&rt->global_entry, &p->global_entry);
				return; /* MUST be returned here.*/
			}
		}
		/* if the global list is empty or @rt is the lowest-priority task, 
		   just insert it to the tail of the global list. */
		list_add_tail(&rt->global_entry, &go.task_list.head);
	}
	else {
		printk(KERN_WARNING 
			   "RESCH: process#%d has been in the global list.\n", 
			   rt->pid);
	}
}

/**
 * remove @rt from the global list.
 * the global list must be locked.
 */
static inline void __global_list_remove(resch_task_t *rt)
{
	if (!list_empty(&rt->global_entry)) {
		list_del_init(&rt->global_entry);
	}
	else {
		printk(KERN_WARNING 
			   "RESCH: process#%d has not been in the global list.\n", 
			   rt->pid);
	}
}

/**
 * insert @rt into the global list safely with spin lock.
 */
static inline void global_list_insert(resch_task_t *rt)
{
	global_list_down();
	__global_list_insert(rt);
	global_list_up();
}

/**
 * remove @rt from the global list safely with spin lock.
 */
static inline void global_list_remove(resch_task_t *rt)
{
	global_list_down();
	__global_list_remove(rt);
	global_list_up();
}

/**
 * reinsert @rt into the global list safely with spin lock.
 */
static inline void global_list_reinsert(resch_task_t *rt)
{
	global_list_down();
	__global_list_remove(rt);
	__global_list_insert(rt);
	global_list_up();
}

/**
 * return the first entry of the background tasks, without semaphore lock.
 * this is useful for resource reservation.
 */
static inline resch_task_t* get_background_task(int cpu)
{
	resch_task_t *bg;
	unsigned long flags;
	
	active_queue_lock(cpu, &flags);
	bg = active_prio_task(cpu, RESCH_BG_PRIO);
	active_queue_unlock(cpu, &flags);

	return bg;
}

/**
 * change the scheduling policy and p->rt_priority in the internal of
 * the Linux kernel, using sched_setscheduler(). 
 */
static inline int __change_prio(struct task_struct *p, int prio)
{
	struct sched_param sp;
	sp.sched_priority = prio;
	if (sched_setscheduler(p, SCHED_FIFO, &sp) < 0) {
		printk(KERN_WARNING 
			   "RESCH: failed to change priority %d.\n", prio);
		return false;
	}
	return true;
}

/**
 * change the priority of the given task, and save it to @rt->prio. 
 */
static inline int change_prio(resch_task_t *rt, int prio)
{
	if (!__change_prio(rt->task, prio)) {
		return false;
	}

	rt->prio = prio;
	rt->prio_index = reverse_prio(prio);

	/* reinsert the task into the global list, ONLY IF it has been
	   already submitted to RESCH. */
	if (task_is_submitted(rt)) {
		global_list_reinsert(rt);
	}

	/* requeue the task into the active queue, ONLY IF it is running. */
	if (task_is_active(rt)) {
		requeue_task(rt);
	}

	return true;
}

/**
 * if there are dead tasks, clear corresponding PID bitmap. 
 */
static inline void clear_dead_tasks(void)
{
	int i;
	struct task_struct *p;

	for (i = 0; i < NR_RT_TASKS; i++) {
		p = resch_task[i].task;
		if (p && p->state == TASK_STOPPED) {
			/* clear the bit for the corresponding RESCH ID. */
			__clear_bit(i, go.pids.bitmap);
		}
	}
}

/**
 * apportion the remaining CPU resource of the given task to other tasks 
 * which have been scheduled in background due to out of resource. 
 */
static inline void apportion_resource(resch_task_t *rt)
{
	resch_task_t *bg = get_background_task(rt->cpu_id);
	if (bg) {
		/* succeeds the remaining timeout and the priority of @rt. */
		bg->timeout += rt->timeout - rt->exec_time;
		set_priority(bg, rt->prio);
	}
}

/**
 * the following two functions are used to temporarily disable the load 
 * balancing function supported by the Linux scheduler.
 * this decreases runtime schedulability as well as fairness, but instead 
 * we can trace the execution time of each task, since migration never
 * occurs during a task is executing, and as a result, we know exactly 
 * CPU ticks. this also improves predictability.
 */
static inline void enable_load_balance(resch_task_t *rt)
{
	/* restore the CPU mask. */
	cpumask_copy(&rt->task->cpus_allowed, &rt->cpumask);
}

static inline void disable_load_balance(resch_task_t *rt)
{
	cpumask_t mask;

	local_irq_disable();
	/* save the CPU mask. */
	cpumask_copy(&rt->cpumask, &rt->task->cpus_allowed);
	/* temporarily disable migration. */
	cpus_clear(mask);
	cpu_set(smp_processor_id(), mask);
	cpumask_copy(&rt->task->cpus_allowed, &mask);
	local_irq_enable();
}

/**
 * start a new job of the given task.
 */
static inline void job_start(resch_task_t *rt)
{
#if !(LOAD_BALANCE_ENABLED)
	int cpu_now;
	int cpu_old;
	unsigned long flags;

	disable_load_balance(rt);
	cpu_now = smp_processor_id();
	cpu_old = rt->cpu_id;
	if (cpu_now != cpu_old) {
		/* move to a correct active queue. 
		   note that this task has no preemptee and preempter, since
		   its job has not been started yet. */
		active_queue_double_lock(cpu_now, cpu_old, &flags);
		__dequeue_task(rt);
		rt->cpu_id = cpu_now;
		__enqueue_task(rt);
		active_queue_double_unlock(cpu_now, cpu_old, &flags);
	}

	/* save the CPU ID. this must be before preempt_curr(). */
	rt->cpu_id = cpu_now;

	/* trace task preemptions on this CPU. */
	preempt_curr(rt);
#endif

	/* reserve CPU resource if requested. */
	if (reservation_is_requested(rt)) {
		/* timeout may be changed later by resource apportion. */
		rt->timeout = rt->reservation_time;
		rt->prio_save = rt->prio;
		check_in_resource(rt);
	}
}

/**
 * release a new job of the given task.
 * the release time of the new job must have been set before. 
 * we must remember this function is an interrupt context! 
 */
static inline void job_release(resch_task_t *rt)
{
	rt->exec_time = 0;
	rt->task->utime = 0;

	/* insert the task into the active queue. */
	enqueue_task(rt);

	/* call the plugin function. */
	resch_plugins.job_release(rt);
}

/**
 * called when a task pointed to by @__data begins a new period. 
 */
static void job_release_handler(unsigned long __data)
{
	resch_task_t *rt = (resch_task_t *)__data;

	wake_up_process(rt->task);
	job_release(rt); /* should be after wake_up_process()! */
}

/**
 * the given task will sleep until the the next period. 
 * note that we do not support the case in which mod_timer may wake
 * up job_release_handler() before @rt->release_time. 
 */
static inline void wait_next_period(resch_task_t *rt)
{
	struct timer_list timer;

	setup_timer_on_stack(&timer, job_release_handler, (unsigned long)rt);
	if (rt->release_time > jiffies) {
		rt->task->state = TASK_UNINTERRUPTIBLE;
		mod_timer(&timer, rt->release_time);
	}

	/* the current job ends here. */
	schedule();

	/* the new job starts here, when it is scheduled again. 
	   note that job_release_handler() has been executed before. */
	del_timer_sync(&timer);
	destroy_timer_on_stack(&timer);
}

/**
 * complete the current job of the given task.
 * the function calls wait_next_period() to wait for the next period. 
 */
static inline void job_complete(resch_task_t *rt)
{
#if !(LOAD_BALANCE_ENABLED)
	/* trace task preemptions. */
	preempt_switch(rt);

	/* renew the WCET if necessary. */
	if (rt->exec_time > usecs_to_jiffies(rt->wcet)) {
		rt->wcet = jiffies_to_usecs(rt->exec_time);
	}
#endif

	/* call the plugin function. */
	resch_plugins.job_complete(rt);

	/* remove the task from the active queue. */
	dequeue_task(rt);

	/* finalize resource reservation. */
	if (reservation_is_requested(rt)) {
		/* if the reserved resource is still remaining, check out it and
		   also apportion it to background tasks.
		   otherwise, put back the original priority, since it may have 
		   been changed in resource reservation. */
		local_irq_disable();
		if (resource_is_reserved(rt)) {
			/* check out the reserved resource. disable interrupts 
			   so that the handler is not called during the check out. */
			check_out_resource(rt);
			local_irq_enable();

			/* apportion the resource for background tasks. 
			   under implementation. */
			//apportion_resource(rt);
		}
		else {
			local_irq_enable();
			set_priority(rt, rt->prio_save);
		}
		/* reset timeout. */
		rt->timeout = 0;
	}

#if !(LOAD_BALANCE_ENABLED)
	/* finally, enable load balancing for this task. */
	enable_load_balance(rt);
#endif

	/* set the next release time. */
	if (rt->period != RESCH_PERIOD_INFINITY) {
		rt->release_time += rt->period;
		/* then, wait for the next period! */
		if (rt->release_time > jiffies) {
			wait_next_period(rt);
		}
		else {
			job_release(rt);
		}
	}

	/* here, the new job starts execution. */
	job_start(rt);
}

/**
 * the default deadline miss handler just sets the release time. 
 */
static inline void default_deadline_miss_handler(resch_task_t *rt)
{
	int cpu = smp_processor_id();

	printk(KERN_WARNING 
		   "RESCH: process#%d (task#%d) missed a deadline %lu ",
		   rt->pid, rt->rid, rt->release_time + rt->deadline);
	printk("on CPU#%d at time (jiffies) %lu.\n", cpu, jiffies);

	rt->release_time = jiffies;
}

/**
 * the dummy plugin function. 
 */
static inline void no_plugin(resch_task_t *rt)
{
	/* should be empty. */
}

/**
 * install the given scheduler plugins. 
 */
void install_scheduler(void (*task_run_plugin)(resch_task_t*),
					   void (*task_exit_plugin)(resch_task_t*),
					   void (*job_release_plugin)(resch_task_t*),
					   void (*job_complete_plugin)(resch_task_t*))
{
	if (task_run_plugin)
		resch_plugins.task_run = task_run_plugin;
	else
		resch_plugins.task_run = no_plugin;

	if (task_exit_plugin)
		resch_plugins.task_exit = task_exit_plugin;
	else
		resch_plugins.task_exit = no_plugin;

	if (job_release_plugin)
		resch_plugins.job_release = job_release_plugin;
	else
		resch_plugins.job_release = no_plugin;

	if (job_complete_plugin)
		resch_plugins.job_complete = job_complete_plugin;
	else
		resch_plugins.job_complete = no_plugin;
}
EXPORT_SYMBOL(install_scheduler);

/**
 * uninstall the scheduler plugins. 
 */
void uninstall_scheduler(void)
{
	resch_plugins.task_run = no_plugin;
	resch_plugins.task_exit = no_plugin;
	resch_plugins.job_release = no_plugin;
	resch_plugins.job_complete = no_plugin;
}
EXPORT_SYMBOL(uninstall_scheduler);

/**
 * compute the worst-case response time of @rt on the given CPU.
 * since the worst-case response time may exceed the range of size
 * unsigned long, we use struct timeval as a return value. 
 * the global list must be locked. 
 */
void response_time_analysis_timeval(resch_task_t *rt, 
									int cpu, struct timeval *ret)
{
	int F;
	struct timeval period_hp, wcet_hp, wcet_p, L;
	struct timeval tv_tmp1, tv_tmp2;
	resch_task_t *hp;

	tvclear(ret);
	tvjiffies(rt->deadline, &L);

	list_for_each_entry(hp, &go.task_list.head, global_entry) {
		if (hp->prio < rt->prio) {
			break;
		}
		if (hp != rt && task_is_on_cpu(hp, cpu)) {
			F = rt->deadline / hp->period;
			tvjiffies(hp->period, &period_hp);
			tvus(hp->wcet, &wcet_hp);

			/* if L >= period_hp * F + wcet_hp */
			tvmul(&period_hp, F, &tv_tmp1);
			tvadd(&tv_tmp1, &wcet_hp, &tv_tmp2);
			if (tvge(&L, &tv_tmp2)) {
				/* ret += wcet_hp * (F + 1). */
				tvmul(&wcet_hp, F + 1, &tv_tmp1);
				tvadd(ret, &tv_tmp1, ret);
			}
			else {
				/* ret += L - F * (period_hp - wcet_hp). */
				tvsub(&period_hp, &wcet_hp, &tv_tmp1);
				tvmul(&tv_tmp1, F, &tv_tmp2);
				tvsub(&L, &tv_tmp2, &tv_tmp1);
				tvadd(ret, &tv_tmp1, ret);
			}
		}
	}

	/* ret = sum_{hp} ret + wcet_p. */
	tvus(rt->wcet, &wcet_p);
	tvadd(ret, &wcet_p, ret);
}

/**
 * compute the worst-case response time of @rt on the given CPU. 
 * it is undefined if the worst-case response time exceeds the range 
 * of size unsigned long...
 * the global list must be locked.
 */
unsigned long response_time_analysis(resch_task_t *rt, int cpu) 
{
	int F;
	unsigned long ret;
	unsigned long Pk, Ck;
	unsigned long L;
	resch_task_t *hp;

	ret = 0;
	L = jiffies_to_usecs(rt->deadline);
	list_for_each_entry(hp, &go.task_list.head, global_entry) {
		if (hp->prio < rt->prio) {
			break;
		}
		if (hp != rt && task_is_on_cpu(hp, cpu)) {
			F = rt->deadline / hp->period;
			Pk = jiffies_to_usecs(hp->period);
			Ck = hp->wcet;
			if (L >= Pk * F + Ck) {
				ret += Ck * (F + 1);
			}
			else {
				ret += L - F * (Pk - Ck);
			}
		}
	}

	return ret + rt->wcet + 
		sched_overhead_cpu(cpu, L) +  sched_overhead_task(rt, L);
}
EXPORT_SYMBOL(response_time_analysis);

/**
 * return the pointer to the resch task descriptor associated with @p. 
 */
resch_task_t *get_resch_task(struct task_struct *p)
{
	return resch_task_ptr(p);
}
EXPORT_SYMBOL(get_resch_task);

/**
 * set the new priority to @rt. 
 */
int set_priority(resch_task_t *rt, int prio)
{
	if (capable(CAP_SYS_NICE)) {
		if (!change_prio(rt, prio)) {
			return false;
		}
	}
	else {
		request_change_prio(rt, prio);
	}
	return true;
}
EXPORT_SYMBOL(set_priority);

/**
 * return the number of active tasks on the given CPU.
 * the active queue must be locked.
 */
int active_tasks(int cpu)
{
	return 	lo[cpu].active.nr_tasks;
}
EXPORT_SYMBOL(active_tasks);

/**
 * return the highest-priority task actively running on the given CPU.
 * return NULL if the CPU has no ready tasks managed by Resch. 
 * the active queue must be locked.
 */
resch_task_t* active_highest_prio_task(int cpu)
{
	int idx;
	struct prio_array *active = &lo[cpu].active;

	if ((idx = resch_ffs(active->bitmap, RESCH_PRIO_LONG)) < 0) {
		return NULL;
	}
#ifdef DEBUG
	if (list_empty(&active->queue[idx])) {
		printk(KERN_WARNING 
			   "RESCH: active queue may be broken on CPU#%d.\n", cpu);
		return NULL;
	}
#endif
	/* the first entry must be a valid reference due to non-negative idx. */
	return list_first_entry(&active->queue[idx], 
							resch_task_t, 
							active_entry);
}
EXPORT_SYMBOL(active_highest_prio_task);

/**
 * return the task positioned at the next of @rt in the active queue.
 * return NULL if there is no next task.
 * the active queue must be locked. 
 */	
resch_task_t* active_next_prio_task(resch_task_t *rt)
{
	int idx = rt->prio_index;
	struct prio_array *active = &lo[rt->cpu_id].active;

	if (rt->active_entry.next == &(active->queue[idx])) {
		/* find next set bit with offset of idx. */
		if ((idx = resch_fns(active->bitmap, ++idx, RESCH_PRIO_LONG)) < 0) {
			return NULL;
		}
		return list_first_entry(&active->queue[idx], 
								resch_task_t, 
								active_entry);
	}
	return list_entry(rt->active_entry.next, 
					  resch_task_t, 
					  active_entry);
}
EXPORT_SYMBOL(active_next_prio_task);

/**
 * return the task positioned at the previous of @rt in the active queue.
 * return NULL if there is no previous task.
 * the active queue must be locked. 
 */	
resch_task_t* active_prev_prio_task(resch_task_t *rt)
{
	int idx = rt->prio_index;
	struct prio_array *active = &lo[rt->cpu_id].active;

	if (rt->active_entry.prev == &(active->queue[idx])) {
		/* find previous set bit with offset of idx. */
		if ((idx = resch_fps(active->bitmap, --idx, RESCH_PRIO_LONG)) < 0) {
			return NULL;
		}
		/* imitate list_last_entry(). */
		return list_entry(active->queue[idx].prev, 
						  resch_task_t, 
						  active_entry);
	}
	return list_entry(rt->active_entry.prev, 
					  resch_task_t, 
					  active_entry);
}
EXPORT_SYMBOL(active_prev_prio_task);

/**
 * return the first task which has the given priority.
 * return NULL if the CPU has no tasks with the priority.
 * the active queue must be locked. 
 */	
resch_task_t* active_prio_task(int cpu, int prio)
{
	int idx = reverse_prio(prio);
	struct prio_array *active = &lo[cpu].active;

	/* find next set bit with offset of idx. */
	if ((idx = resch_fns(active->bitmap, idx, RESCH_PRIO_LONG)) < 0) {
		return NULL;
	}
#ifdef DEBUG
	if (list_empty(&active->queue[idx])) {
		printk(KERN_WARNING 
			   "RESCH: active queue may be broken on CPU#%d.\n", cpu);
		return NULL;
	}
#endif
	return list_first_entry(&active->queue[idx], 
							resch_task_t, 
							active_entry);
}
EXPORT_SYMBOL(active_prio_task);

/**
 * return the task running on the given CPU, which has the num-th priority. 
 * return NULL if the CPU has no ready tasks managed by Resch. 
 * the active queue must be locked. 
 */	
resch_task_t* active_number_task(int cpu, int num)
{
	int i;
	int idx;
	resch_task_t *rt;
	struct prio_array *active = &lo[cpu].active;

	i = idx = 0;
	while (i < num) {
		/* find next set bit with offset of idx. */
		if ((idx = resch_fns(active->bitmap, idx, RESCH_PRIO_LONG)) < 0) {
			return NULL;
		}
		/* should not empty! */
		list_for_each_entry(rt, &active->queue[idx], active_entry) {
			if (++i == num) {
				return rt;
			}
		}
		/* set the index for the next search. 
		   this also prevents an infinite loop. */
		idx++;
	}
	/* should not reach here... */
	printk(KERN_WARNING
		   "RESCH: active queue may be broken on CPU#%d .\n", cpu);
	return NULL;
}
EXPORT_SYMBOL(active_number_task);

/**
 * return the highest-priority task submitted to RESCH.
 * the global list must be locked. 
 */
resch_task_t* global_highest_prio_task(void)
{
	if (list_empty(&go.task_list.head)) {
		return NULL;
	}
	return list_first_entry(&go.task_list.head, 
							resch_task_t, 
							global_entry);
}
EXPORT_SYMBOL(global_highest_prio_task);

/**
 * return the task positioned at the next of @rt in the global list. 
 * the global list must be locked. 
 */
resch_task_t* global_next_prio_task(resch_task_t *rt)
{
	if (rt->global_entry.next == &(go.task_list.head)) {
		return NULL;
	}
	return list_entry(rt->global_entry.next, 
					  resch_task_t, 
					  global_entry);
}
EXPORT_SYMBOL(global_next_prio_task);

/**
 * return the task positioned at the previous of @rt in the global list. 
 * the global list must be locked. 
 */
resch_task_t* global_prev_prio_task(resch_task_t *rt)
{
	if (rt->global_entry.prev == &(go.task_list.head)) {
		return NULL;
	}
	return list_entry(rt->global_entry.prev, 
					  resch_task_t, 
					  global_entry);
}
EXPORT_SYMBOL(global_prev_prio_task);

/**
 * return the first task submitted to RESCH, which has the given priority. 
 * the global list must be locked.
 */
resch_task_t* global_prio_task(int prio)
{
	resch_task_t *rt;
	list_for_each_entry(rt, &go.task_list.head, global_entry) {
		if (rt->prio >= prio) {
			return rt;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(global_prio_task);

/**
 * return the task positioned at the num-th in the global list.
 * the global list must be locked.
 */
resch_task_t* global_number_task(int num)
{
	int i = 0;
	resch_task_t *rt;
	list_for_each_entry(rt, &go.task_list.head, global_entry) {
		if (++i == num) {
			return rt;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(global_number_task);

/**
 * lock the active queue on the given CPU. 
 */
void active_queue_lock(int cpu, unsigned long *flags)
{
	spin_lock_irqsave(&lo[cpu].active.lock, *flags);
}
EXPORT_SYMBOL(active_queue_lock);

/**
 * unlock the active queue on the given CPU. 
 */
void active_queue_unlock(int cpu, unsigned long *flags)
{
	spin_unlock_irqrestore(&lo[cpu].active.lock, *flags);
}
EXPORT_SYMBOL(active_queue_unlock);

/**
 * lock the two active queues on the given CPUs at the same time. 
 * this function may be useful on task migrations.
 */
void active_queue_double_lock(int cpu1, int cpu2, unsigned long *flags)
{
	spin_lock_irqsave(&lo[cpu1].active.lock, *flags);
	spin_lock(&lo[cpu2].active.lock);
}
EXPORT_SYMBOL(active_queue_double_lock);

/**
 * unlock the two active queues on the given CPUs at the same time. 
 * this function may be useful on task migrations.
 */
void active_queue_double_unlock(int cpu1, int cpu2, unsigned long *flags)
{
	spin_unlock(&lo[cpu2].active.lock);
	spin_unlock_irqrestore(&lo[cpu1].active.lock, *flags);
}
EXPORT_SYMBOL(active_queue_double_unlock);

/**
 * down the mutex to lock the global list. 
 */
void global_list_down(void)
{
	down(&go.task_list.sem);
}
EXPORT_SYMBOL(global_list_down);

/**
 * up the mutex to unlock the global list. 
 */
void global_list_up(void)
{
	up(&go.task_list.sem);
}
EXPORT_SYMBOL(global_list_up);

/**
 * migrate the given task to the given CPU. 
 */
void migrate_task(resch_task_t *rt, int cpu_dst)
{
	int cpu_src = rt->cpu_id;
	unsigned long flags;

	/* double check if the source and destination CPUs are different. */
	if (cpu_src != cpu_dst) {
		/* only if the task is submitted, touch the lists and ticks. */
		if (task_is_active(rt)) {
			/* move the task to the active queue on the destination CPU. */
			active_queue_double_lock(cpu_src, cpu_dst, &flags);
#if !(LOAD_BALANCE_ENABLED)
			/* trace task preemptions on the source CPU. */
			preempt_out(rt);
#endif
			__dequeue_task(rt);
			rt->cpu_id = cpu_dst; 
			__enqueue_task(rt);
#if !(LOAD_BALANCE_ENABLED)
			/* trace task preemptions on the destination CPU, only if
			   the job has already been started. */
			if (rt->exec_time > 0) {
				preempt_in(rt);
			}
#endif
			active_queue_double_unlock(cpu_src, cpu_dst, &flags);
		}
		else {
			/* just save the CPU ID in its own member. */
			rt->cpu_id = cpu_dst;
		}
	}

	/* task migration occurs here. */
	cpus_clear(rt->cpumask);
	cpu_set(cpu_dst, rt->cpumask);
	set_cpus_allowed_ptr(rt->task, &rt->cpumask);
}
EXPORT_SYMBOL(migrate_task);

/**
 * return the scheduling overhead in the given interval by microseconds. 
 * since we dont know the single scheduler tick cost, we pessimistically
 * assume that context switches may happen at every scheduler tick.
 * hence, the total number of context switches assumed is the sum of 
 * the number of job completions and the number of scheduler ticks.
 * to reduce pessimism, we also assume that the single scheduler tick
 * cost is as half as the single context switch cost, given that a kernel
 * timer interrupt does not store and load all registers but does less 
 * than half of registers.
 * we believe this assumption is still pessimistic.
 */
unsigned long sched_overhead_cpu(int cpu, unsigned long interval)
{
	resch_task_t *rt;
	unsigned long nr_jobs = 0;
	list_for_each_entry(rt, &go.task_list.head, global_entry) {
		if (task_is_on_cpu(rt, cpu)) {
			nr_jobs += div_round_up(interval, 
									jiffies_to_usecs(rt->period));
		}
	}
	return 2 * nr_jobs * context_switch_cost() + 
		(usecs_to_jiffies(interval) - nr_jobs) * context_switch_cost() / 2;
}
EXPORT_SYMBOL(sched_overhead_cpu);

/**
 * return the single context switch cost. 
 */
unsigned long context_switch_cost(void)
{
	return switch_cost;
}
EXPORT_SYMBOL(context_switch_cost);

/**
 * return the single context migration cost. 
 */
unsigned long context_migration_cost(void)
{
	return migration_cost;
}
EXPORT_SYMBOL(context_migration_cost);

/* API: initialize the current task into real-time mode. */
static inline int api_init(void)
{
	int res = RES_SUCCESS;
	resch_task_t *rt;

	/* attach the current task to the RESCH module. */
	spin_lock_irq(&go.pids.lock);
	if (!attach_resch(current)) {
		/* error print. */
		printk(KERN_WARNING
			   "RESCH: failed to attach the module.\n");
		/* clear all dead PIDs. */
		clear_dead_tasks();
		res = RES_FAULT;
		spin_unlock_irq(&go.pids.lock);
		goto out;
	}
	spin_unlock_irq(&go.pids.lock);

	/* get the resch task descriptor. */
	rt = resch_task_ptr(current);

	/* init the resch task discriptor. */ 
	resch_task_init(rt);

 out:
	return res;
}

/* API: exit the current task from real-time mode. */
static inline int api_exit(void)
{
	int res = RES_SUCCESS;
	struct sched_param sp;
	resch_task_t *rt = resch_task_ptr(current);

#if !(LOAD_BALANCE_ENABLED)	
	preempt_switch(rt);
#endif

	/* make sure to check out the resource. */
	if (reservation_is_requested(rt)) {
		local_irq_disable();
		if (resource_is_reserved(rt)) {
			check_out_resource(rt);
		}
		local_irq_enable();
	}

	/* call a plugin function for task exit. */
	resch_plugins.task_exit(rt);

	/* make sure to remove the task from the active queue. */
	if (task_is_active(rt)) {
		dequeue_task(rt);
	}

	/* make sure to remove the task from the task list. */
	if (task_is_submitted(rt)) {
		global_list_remove(rt);
	}

	/* put back the scheduling policy and priority. */
	sp.sched_priority = 0;
	if (sched_setscheduler(current, SCHED_NORMAL, &sp) < 0) {
		printk(KERN_WARNING 
			   "RESCH: failed to put back scheduling policy.\n");
		res = RES_FAULT;
	}

	/* detach the current task from the RESCH module. */
	spin_lock_irq(&go.pids.lock);
	detach_resch(current);
	spin_unlock_irq(&go.pids.lock);

	return res;
}

/* API: set (change) the period of the current task. */
static inline int api_set_name(const char *name)
{
	int i;
	resch_task_t *rt = resch_task_ptr(current);

	for (i = 0; i < strlen(name); i++) {
		rt->name[i] = name[i];
	}
	rt->name[i] = '\0';
	
	return RES_SUCCESS;
}

/* API: set (change) the period of the current task. */
static inline int api_set_period(unsigned long period)
{
	resch_task_t *rt = resch_task_ptr(current);
	rt->period = msecs_to_jiffies(period);
	/* if the deadline has not been set yet, assign the period. */
	if (rt->deadline == 0) {
		rt->deadline = rt->period;
	}
	return RES_SUCCESS;
}

/* API: set (change) the relative deadline of the current task. */
static inline int api_set_deadline(unsigned long deadline)
{
	resch_task_t *rt = resch_task_ptr(current);
	rt->deadline = msecs_to_jiffies(deadline);
	return RES_SUCCESS;
}

/* API: set (change) the WCET of the current task. 
   @wcet is given by microseconds. */
static inline int api_set_wcet(unsigned long wcet)
{
	resch_task_t *rt = resch_task_ptr(current);
	rt->wcet = wcet;
	return RES_SUCCESS;
}

/* API: set (change) the priority of the current task. */
static inline int api_set_priority(unsigned long prio)
{
	resch_task_t *rt = resch_task_ptr(current);
	if (!set_priority(rt, prio)) {
		return RES_FAULT;
	}
	return RES_SUCCESS;	
}

/* API: reserve CPU resource for the current task. 
   if @time is zero, reservation will be cancelled. */
static inline int api_reserve_cpu(unsigned long cputime, int xcpu)
{
	resch_task_t *rt = resch_task_ptr(current);

#if LOAD_BALANCE_ENABLED
	current->signal->rlim[RLIMIT_RTTIME].rlim_cur = RLIM_INFINITY;
	current->signal->rlim[RLIMIT_RTTIME].rlim_max = RLIM_INFINITY;
#endif
	rt->reservation_time = msecs_to_jiffies(cputime);
	rt->xcpu = xcpu;

	return RES_SUCCESS;
}

/* API: put task in background. */
static inline int api_background(void)
{
	request_change_prio(resch_task_ptr(current), RESCH_BG_PRIO);
	return RES_SUCCESS;
}

/* API: run the current task.
   the first job will be released when @timeout (ms) elapses. */
static inline int api_run(unsigned long timeout)
{
	resch_task_t *rt = resch_task_ptr(current);

	/* call a plugin function for task running. */
	resch_plugins.task_run(rt);

	/* insert the task into the global and local resch lists.
	   every time the priority is changed, the task is reinserted. */
	global_list_insert(rt);

	/* the first release time. */
	rt->release_time = jiffies + msecs_to_jiffies(timeout);

	/* wait for the first release time.
	   if the first release time has been already passed,
	   release the first job immediately.*/
	if (rt->release_time > jiffies) {
		wait_next_period(rt);
	}
	else {
		job_release(rt);
	}

	/* here, the new job starts execution. */
	job_start(rt);

	return RES_SUCCESS;
}

/* API: let the current task wait for the next period. */
static inline int api_wait_for_period(void)
{
	int res = RES_SUCCESS;
	resch_task_t *rt = resch_task_ptr(current);

	/* verify a deadline miss. */
	if (rt->release_time + rt->deadline < jiffies) {
		/* we should notify a deadline miss to user program. */
		res = RES_MISS;
		/* call a deadline miss handler. */
		default_deadline_miss_handler(rt);
	}

	/************************
	 * job complete 
	 ************************/
	job_complete(rt);

	return res;
}

static inline void test_switch_begin(unsigned long val1, unsigned long val2)
{
	test.switch_begin.tv_sec = val1;
	test.switch_begin.tv_usec = val2;
}

static inline void test_switch_end(unsigned long val1, unsigned long val2)
{
	struct timeval result;

	test.switch_end.tv_sec = val1;
	test.switch_end.tv_usec = val2;
	tvsub(&test.switch_end, &test.switch_begin, &result);

	if (switch_cost < result.tv_usec) {
		switch_cost = result.tv_usec;
		printk(KERN_INFO "RESCH: context switch cost %lu(us)\n", switch_cost);
	}
}

static inline void test_migration(void)
{
	int cpu_now = smp_processor_id();
	int cpu_dst = (cpu_now + 1) % NR_RT_CPUS;
	resch_task_t *rt = resch_task_ptr(current);
	struct timeval result;

	do_gettimeofday(&test.migration_begin);
	migrate_task(rt, cpu_dst);
	do_gettimeofday(&test.migration_end);
	tvsub(&test.migration_end, &test.migration_begin, &result);
	if (migration_cost < result.tv_usec) {
		migration_cost = result.tv_usec;
		printk(KERN_INFO "RESCH: migration cost %lu(us)\n", migration_cost);
	}
}

/* dummy function. */
static int resch_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/* dummy function. */
static int resch_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * tests are done through this function.
 * @cmd holds the test command number.
 * @buf holds the values.
 * See api.h for details.
 */
static ssize_t resch_write(struct file *file, const char *buf, 
						   size_t count, loff_t *offset)
{
	char kbuf[sizeof(long) * 3];
	char mbuf[12];
	unsigned long cmd, val1, val2;
	int res = RES_SUCCESS;

	// Handle calls from 'rt_set_name'
	if (count < 12) {
		if (copy_from_user(mbuf, buf, count)) {
			printk(KERN_WARNING "RESCH: failed to copy data.\n");
			return -EFAULT;
		}
		api_set_name(buf);
		return RES_SUCCESS;
	}

	/* copy data to kernel buffer. */
	if (copy_from_user(kbuf, buf, count)) {
		printk(KERN_WARNING "RESCH: failed to copy data.\n");
		return -EFAULT;
	}
	
	cmd = ((unsigned long*)kbuf)[0];
	val1 = ((unsigned long*)kbuf)[1];
	val2 = ((unsigned long*)kbuf)[2];

	switch (cmd) {
	case TEST_SWITCH_BEGIN:
		test_switch_begin(val1, val2);
		break;
	
	case TEST_SWITCH_END:
		test_switch_end(val1, val2);
		break;

	case TEST_MIGRATION:
		test_migration();
		break;

	default:
		res = RES_ILLEGAL;
		printk(KERN_WARNING "RESCH: illegal test command.\n");
		break;
	}
	
	return RES_SUCCESS;
}

/**
 * Timing properties are set through this function.
 * @cmd holds the API number.
 * @arg holds the value.
 * See api.h for details.
 */
static int resch_ioctl(struct inode *inode,
					   struct file *file,
					   unsigned int cmd, 
					   unsigned long arg)
{
	ssize_t res;
	unsigned long val;
	
	/* copy data to kernel buffer. */
	if (copy_from_user(&val, (long *)arg, sizeof(long))) {
		printk(KERN_WARNING "RESCH: failed to copy data.\n");
		return -EFAULT;
	}

	/* verify if the caller of this function is managed by RESCH. 
	   only when the command is API_INIT, the task is has not been
	   yet managed by RESCH. */
	if (cmd != API_INIT && !task_is_managed(current)) {
		return -EFAULT;
	}

	/* which API is called? */
	switch (cmd) {
	case API_INIT:
		res = api_init();
		break;
	
	case API_EXIT:
		res = api_exit();
		break;

	case API_RUN:
		res = api_run(val);
		break;

	case API_WAIT: /* most often used. */
		res = api_wait_for_period();
		break;

	case API_PERIOD:
		res = api_set_period(val);
		break;

	case API_DEADLINE:
		res = api_set_deadline(val);
		break;

	case API_WCET:
		res = api_set_wcet(val);
		break;

	case API_PRIORITY:
		res = api_set_priority(val);
		break;

	case API_RESERVE:
		res = api_reserve_cpu(val, false);
		break;

	case API_RESERVE_XCPU:
		res = api_reserve_cpu(val, true);
		break;

	case API_BACKGROUND:
		res = api_background();
		break;

	default: /* illegal api identifier. */
		res = RES_ILLEGAL;
		printk(KERN_WARNING "RESCH: illegal API.\n");
		break;
	}

	return res;
}

static struct file_operations resch_fops = {
	.owner = THIS_MODULE,
	.open = resch_open, /* do nothing but must exist. */
	.release = resch_release, /* do nothing but must exist. */
	.read = NULL,
	.write = resch_write,
	.ioctl = resch_ioctl,
};

/**
 * a kernel thread function to alternatively call sched_setscheduler() to
 * change the scheduling policy and the priority of real-time tasks.
 */
static void setscheduler(void)
{
	struct setscheduler_req *req;
	struct setscheduler_thread_struct *thread = 
		&lo[smp_processor_id()].setscheduler_thread;

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		spin_lock_irq(&thread->lock);
		if (list_empty(&thread->list)) {
			spin_unlock_irq(&thread->lock);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			continue;
		}

		/* get a task in the list by fifo. */
		req = list_first_entry(&thread->list, struct setscheduler_req, list);
		/* remove this task from the list. */
		list_del_init(&req->list);
		spin_unlock(&thread->lock);

		/* alternatively change the priority of the task. */
		change_prio(req->rt, req->prio);
		wake_up_process(req->rt->task);
		local_irq_enable();
	}
}	

static void global_object_init(void)
{
	int i;

	/* the PID map. */
	for (i = 0; i < PID_MAP_LONG; i++) {
		go.pids.bitmap[i] = 0;
	}

	/* the task list that contains all the submitted tasks. */
	init_MUTEX(&go.task_list.sem);
	INIT_LIST_HEAD(&go.task_list.head);
}

static void global_object_exit(void)
{
}

static void local_object_init(void)
{
	int i;
	int cpu;
	struct sched_param sp = { .sched_priority = RESCH_MAX_PRIO };

	for (cpu = 0; cpu < NR_RT_CPUS; cpu++) {
		/* active task queue. */
		lo[cpu].active.nr_tasks = 0;
		for (i = 0; i < RESCH_PRIO_LONG; i++) {
			lo[cpu].active.bitmap[i] = 0;
		}
		for (i = 0; i < RESCH_MAX_PRIO; i++) {
			INIT_LIST_HEAD(lo[cpu].active.queue + i);
		}
		/* CPU tick. */
		lo[cpu].tick.last_tick = jiffies;
		/* current task. */
		lo[cpu].current_task = NULL;

		/* the setscheduler thread. */
		INIT_LIST_HEAD(&lo[cpu].setscheduler_thread.list);
		lo[cpu].setscheduler_thread.task = 
			kthread_create((void*)setscheduler, NULL, "resch-kthread");
		if (lo[cpu].setscheduler_thread.task != ERR_PTR(-ENOMEM)) {
			kthread_bind(lo[cpu].setscheduler_thread.task, cpu);
			sched_setscheduler(lo[cpu].setscheduler_thread.task, 
							   SCHED_FIFO, 
							   &sp);
			wake_up_process(lo[cpu].setscheduler_thread.task);
		}
		else {
			lo[cpu].setscheduler_thread.task = NULL;
		}
	}
}

static void local_object_exit(void)
{
	int cpu;

	for (cpu = 0; cpu < NR_RT_CPUS; cpu++) {
		if (lo[cpu].setscheduler_thread.task) {
			kthread_stop(lo[cpu].setscheduler_thread.task);
		}
	}
}

static int __init resch_init(void)
{
	int ret;

	printk(KERN_INFO "RESCH: HELLO!\n");

	/* get the device number of a char device. */
	ret = alloc_chrdev_region(&dev_id, 0, 1, MODULE_NAME);
	if (ret < 0) {
		printk(KERN_WARNING "RESCH: failed to allocate device.\n");
		return ret;
	}

	/* initialize the char device. */
	cdev_init(&c_dev, &resch_fops);

	/* register the char device. */
	ret = cdev_add(&c_dev, dev_id, 1);
	if (ret < 0) {
		printk(KERN_WARNING "RESCH: failed to register device.\n");
		return ret;
	}

	/* disable plugin functions by default. */
	uninstall_scheduler();

	/* initialize the RESCH objects. */
	global_object_init();
	local_object_init();

	return 0;
}

static void __exit resch_exit(void)
{
	printk(KERN_INFO "RESCH: GOODBYE!\n");

	/* exit the RESCH objects. */
	local_object_exit();
	global_object_exit();

	/* delete the char device. */
	cdev_del(&c_dev);
	/* return back the device number. */
	unregister_chrdev_region(dev_id, 1);
}

module_init(resch_init);
module_exit(resch_exit);
