/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
 * core.h:	the macros and functions exported from the RESCH core.
 */

#ifndef __RESCH_COMMON_H__
#define __RESCH_COMMON_H__
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/perf_event.h> //needed for event in task struct 
#include "config.h"

/* max priority. */
#define RESCH_MAX_PRIO	(MAX_RT_PRIO - 1)
/* minimum priority. 
   note that it is still in priority to non-rt tasks. */
#define RESCH_MIN_PRIO	1
/* background priority. */
#define RESCH_BG_PRIO	(RESCH_MIN_PRIO + 1)

/* true iff task @rt is assigned to the given CPU. */
#define task_is_on_cpu(rt, cpu)	((rt)->cpu_id == cpu)

/* task information managed by the Resch module. */
typedef struct resch_task_struct {
	/* pointer to the container task. */
	struct task_struct *task; 
	/* CPU mask upon which the task is allowed to run. */
	cpumask_t cpumask;
	/* resch ID. */
	int rid;
	/* original linux PID. */
	int pid;
	/* real-time priority. */
	int prio; 
	/* reversed real-time priority used for prio array index. */
	int prio_index; 
	/* CPU ID upon which the task is currently running. */
	int cpu_id;
	/* This state is either 1|0, 1 = on the EDF ready-queue, 0 = not in the EDF ready-queue */
	int state;
	/* timing properties: */
	unsigned long wcet; 		/* by microseconds */
	unsigned long period; 		/* by jiffies */
	unsigned long deadline;		/* by jiffies */
	unsigned long release_time;	/* by jiffies */
	unsigned long exec_time;	/* by jiffies */
	
	//modified by MemSched group
	//this event is needed for MemSched to count cache misses for a task
	struct perf_event * event;           //defined in <linux/perf_event.h>
	
	/* we have two types of task lists. */
	struct list_head global_entry;
	struct list_head active_entry;
	/* preempting & preempted tasks. */
	struct resch_task_struct *preempter;
	struct resch_task_struct *preemptee;
	/* resource reservation properties. */
	int reserved;
	int xcpu;
	int prio_save; /* holds the original priority. */
	unsigned long reservation_time;		/* by jiffies */
	unsigned long timeout;				/* by jiffies */
	int server_id;			/* Needed by Hierarchical scheduler plugin */
	char hsf_flags;			/* Needed by Hierarchical scheduler plugin */
					/* Index 0: Task was ready */
					/* Index 1: Prevent 'job_release' from releasing task */
					/* Index 2: Previous task release was prevented */
					/* Index 3: Do not let RESCH release the task before the servers start */
#if !(LOAD_BALANCE_ENABLED)
	struct timer_list reservation_timer;
#endif
	unsigned long	total_mem_request;//nas
	unsigned long	periodic_mem_request;//nas
	char rt_name[20];//nas
	int activation_count;//nas, nov 23
	int deadlinemiss_count;//nas, nov 23
} resch_task_t;


/* exported functions. */
extern void install_scheduler(void (*)(resch_task_t *),
							  void (*)(resch_task_t *),
							  void (*)(resch_task_t *),
							  void (*)(resch_task_t *));
extern void uninstall_scheduler(void);
extern int set_priority(resch_task_t *, int);
extern unsigned long response_time_analysis(resch_task_t *, int);
extern resch_task_t *get_resch_task(struct task_struct *);
extern int active_tasks(int);
extern resch_task_t* active_highest_prio_task(int);
extern resch_task_t* active_next_prio_task(resch_task_t *);
extern resch_task_t* active_prev_prio_task(resch_task_t *);
extern resch_task_t* active_prio_task(int, int);
extern resch_task_t* active_number_task(int, int);
extern resch_task_t* global_highest_prio_task(void);
extern resch_task_t* global_next_prio_task(resch_task_t *);
extern resch_task_t* global_prev_prio_task(resch_task_t *);
extern resch_task_t* global_prio_task(int);
extern resch_task_t* global_number_task(int);
extern void active_queue_lock(int, unsigned long*);
extern void active_queue_unlock(int, unsigned long*);
extern void active_queue_double_lock(int, int, unsigned long*);
extern void active_queue_double_unlock(int, int, unsigned long*);
extern void global_list_down(void);
extern void global_list_up(void);
extern void migrate_task(resch_task_t *, int);
extern unsigned long sched_overhead_cpu(int, unsigned long);
unsigned long context_switch_cost(void);
unsigned long context_migration_cost(void);

extern void enqueue_task(resch_task_t *rt);
extern void dequeue_task(resch_task_t *rt);

extern resch_task_t *dequeue_edf(int reset);
extern void enqueue_edf(resch_task_t *rt);
extern void wake_up_process_edf(void);
extern void job_release_handler(unsigned long __data);

extern void request_change_prio_interrupt(resch_task_t *rt, int prio);

static inline unsigned long div_round_up(unsigned long x, unsigned long y)
{
	if (x % y == 0)
		return x / y;
	else
		return x / y + 1;
}

static inline unsigned long sched_overhead_task(resch_task_t *rt, 
												unsigned long interval)
{
	return div_round_up(interval, jiffies_to_usecs(rt->period)) * 
		context_switch_cost();
}

#endif
