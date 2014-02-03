/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 */
/*------------------------- Private Constants ---------------------*/

#define MICROSEC 1000000
#define MAX_NR_OF_EVENTS 20000

/*------------------------- Private Structs -----------------------*/

typedef struct data {
	char type; // 0=task event, 1=server event
	short next_pid;
	short prev_pid;
	unsigned long timestamp;
	char preempt; // 0=Regular switch, 1=Preemption, 2=Task release (no switch), 3=Preemption (no task release)
} event;

unsigned long mikaoverhead;

// If 'init=TRUE then initialize our linux timestamp function'
unsigned long linux_timestamp_microsec(int init) {

	struct timeval timestamp;
	static int time_zero = 0; // We set this one to 'tv_sec' in the beginning, this will decrease timestamp values.

	do_gettimeofday(&timestamp);

	if (init == 1) {
		time_zero = timestamp.tv_sec;
		return 0;
	}

	return ( ((timestamp.tv_sec-time_zero)*1000000)+timestamp.tv_usec );

}

void store_event(resch_task_t *prev, resch_task_t *next, server_t *prev_t, server_t *next_t, char preempt, unsigned long timestamp, int print, int measure) {

	struct timeval tv;
	static event stored_events[MAX_NR_OF_EVENTS];
	static int nr_of_events = 0;
	int i;
        /*
	unsigned long temp = 0;

	if (measure == TRUE)
		temp = linux_timestamp_microsec(0);
        */

	do_gettimeofday(&tv);
	timestamp = ((tv.tv_sec*MICROSEC)+tv.tv_usec);

	if (print == TRUE) {
		for (i = 0; i < nr_of_events; i++) {
			printk(KERN_WARNING "Tracealyzer: %d prev: %hi next: %hi %lu %d\n", stored_events[i].type, stored_events[i].prev_pid, stored_events[i].next_pid, stored_events[i].timestamp, stored_events[i].preempt);
		}
	}
	else {
		if (nr_of_events < MAX_NR_OF_EVENTS) { // Just store the event
			if (prev != NULL) { // This is a task event...
				stored_events[nr_of_events].type = 0;
				stored_events[nr_of_events].next_pid = (short) next->pid;
				stored_events[nr_of_events].prev_pid = (short) prev->pid;
				stored_events[nr_of_events].timestamp = timestamp;
				stored_events[nr_of_events].preempt = preempt;
				nr_of_events++;
			}
			else { // This is a server event
				stored_events[nr_of_events].type = 1;
				stored_events[nr_of_events].next_pid = (short) next_t->id;
				stored_events[nr_of_events].prev_pid = (short) prev_t->id;
				stored_events[nr_of_events].timestamp = timestamp;
				stored_events[nr_of_events].preempt = preempt;
				nr_of_events++;
			}
		}
		else
			;//printk(KERN_WARNING "Tracealyzer: Warning, cannot store more events!!!\n");
	}
	/*
	if (measure == TRUE)
		mikaoverhead += linux_timestamp_microsec(0)-temp;
	*/
}

void task_release_event(resch_task_t *rt) {

	unsigned long flags;
	unsigned long timestamp;
	resch_task_t *curr;
	resch_task_t temp;
	unsigned long tempa;

	tempa = linux_timestamp_microsec(0);

	if (rt == NULL) {
		printk(KERN_WARNING "(task_release) rt == NULL???\n");
		return;
	}

	timestamp = jiffies; //linux_timestamp_microsec(0);

	active_queue_lock(rt->cpu_id, &flags);
	curr = active_highest_prio_task(rt->cpu_id);
	active_queue_unlock(rt->cpu_id, &flags);

	if (curr != NULL) {
		if (rt->prio > curr->prio) { // This means context switch!
			store_event(curr, rt, NULL, NULL, 1, timestamp, FALSE, FALSE);
		}
		else { // Released task is not preempting, just register a task release (but no context switch)...
			store_event(rt, rt, NULL, NULL, 2, timestamp, FALSE, FALSE);
		}
	}
	else { // Ready queue was empty (besides the new task), means that idle task was running...
		temp.pid = 0;
		store_event(&temp, rt, NULL, NULL, 1, timestamp, FALSE, FALSE);
	}

	mikaoverhead += linux_timestamp_microsec(0)-tempa;

}

void task_complete_event(resch_task_t *rt) {

	unsigned long flags;
	unsigned long timestamp;
	resch_task_t *next;
	resch_task_t temp;
	unsigned long tempa;

	tempa = linux_timestamp_microsec(0);	

	if (rt == NULL) {
		printk(KERN_WARNING "(task_complete) rt == NULL???\n");
		return;
	}

	timestamp = jiffies; //linux_timestamp_microsec(0);

	active_queue_lock(rt->cpu_id, &flags);
	next = active_next_prio_task(rt);
	active_queue_unlock(rt->cpu_id, &flags);

	if (next != NULL) {
		store_event(rt, next, NULL, NULL, 0, timestamp, FALSE, FALSE);
	}
	else {
		temp.pid = 0;
		store_event(rt, &temp, NULL, NULL, 0, timestamp, FALSE, FALSE);
	}

	mikaoverhead += linux_timestamp_microsec(0)-tempa;

}

