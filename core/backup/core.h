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
	/* timing properties: */
	unsigned long wcet; 		/* by microseconds */
	unsigned long period; 		/* by jiffies */
	unsigned long deadline;		/* by jiffies */
	unsigned long release_time;	/* by jiffies */
	unsigned long exec_time;	/* by jiffies */
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
	int reserved1;			/* Needed by the Tracealyzer */
	char name[12];			/* Needed by the Tracealyzer */
#if !(LOAD_BALANCE_ENABLED)
	struct timer_list reservation_timer;
#endif
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
// EXTRA:
void taskIdListGet(int *idList, int size);
char *taskName(int rid);
resch_task_t *taskTcb(int rid);

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
