/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
 * exec.c:	execute tasks in the given task set and measure success ratio.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <resch/api.h>
#include "schedbench.h"

/* the number of column in taskset files. */
#define NR_COLS 	4 
/* the default timeout (us). */
#define DEFAULT_TIMEOUT	500

/* macro to convert microseconds to timeval struct. */
#define msecs_to_timeval(ms, tv)					\
	do {											\
		tv.tv_sec = ms / 1000; 						\
		tv.tv_usec = (ms - tv.tv_sec*1000) * 1000; 	\
	} while (0);

typedef struct task_struct {
	pid_t pid;	/* linux process ID. */
	int tid;	/* task ID. */
	int C;		/* computation time */
	int T; 		/* period */
	int D;		/* relative deadline */
	int prio; 	/* priority */
} task_t;

/* task control blocks. */
task_t *tasks;
/* the number of tasks. */
int nr_tasks;

void exec_task(task_t *task, int loop_1ms, int time)
{
	int k; /* do not use i! */
	int nr_jobs = time / task->T;
	struct timeval tv_C, tv_T, tv_D, tv_timeout;
	int ret = RET_SUCCESS;

	msecs_to_timeval(task->C, tv_C);
	msecs_to_timeval(task->T, tv_T);
	msecs_to_timeval(task->D, tv_D);
	msecs_to_timeval(DEFAULT_TIMEOUT, tv_timeout);

	/* get own PID. */
	task->pid = getpid();

	/* initialization for using RESCH. */
	if (!rt_init()) {
		printf("Error: cannot begin!\n");
		ret = RET_MISS;
		goto out;
	} 

	rt_set_priority(task->prio);
	rt_set_wcet(&tv_C); 
	rt_set_period(&tv_T);		
	rt_set_deadline(&tv_D);	
	rt_run(&tv_timeout);

	/* busy loop. */
	for (k = 0; k < nr_jobs; k++) {
		LOOP(task->C * loop_1ms);
		if (!rt_wait_for_period()) {
			ret = RET_MISS;
			break;
		}
	}

	rt_exit();
 out:
	/* call _exit() but not exit(), since exit() may remove resources
	   that are shared with the parent and other child processes. */
	_exit(ret);

	/* no return. */
}

void init_task(task_t *t, int tid, ulong_t C, ulong_t T, ulong_t D)
{
	t->pid = 0;
	t->tid = tid;
	t->prio = 1; /* initial priority is 1. */
	t->C = C;
	t->T = T;
	t->D = D;
}

int read_taskset(FILE *fp)
{
	char line[MAX_BUF], s[MAX_BUF];
	char props[NR_COLS][MAX_BUF];
	ulong_t lcm;
	int tmp;
	int n; /* # of tasks */
	int i, j;
	char *token;
	ulong_t C, T, D;

	/* skip comments and empty lines */
	while(fgets(line, MAX_BUF, fp)) {
		if (line[0] != '\n'	&& line[0] != '#')
			break;
	}

	/* get the number of tasks */
	n = atoi(line);

	/* allocate the memory for the tasks */
	tasks = (task_t *) malloc(sizeof(task_t) * n);

	/* skip comments and empty lines */
	while(fgets(line, MAX_BUF, fp)) {
		if (line[0] != '\n'	&& line[0] != '#')
			break;
	}

	for (i = 0; i < n; i++) {
		strcpy(s, line);
		token = strtok(s, ",\t ");
		/* get task name, exec. time, period, and relative deadline. */
		for (j = 0; j < NR_COLS; j++) {
			if (!token) {
				printf("Error: invalid format: %s!\n", line);
				exit(1);
			}
			strncpy(props[j], token, MAX_BUF);
			token = strtok(NULL, ",\t ");
		}
		C = atoi(props[1]);
		T = atoi(props[2]);
		D = atoi(props[3]);
		init_task(&tasks[i], i, C, T, D);
		// printf("task%d: %d, %d, %d\n", i, C, T, D);

		/* get line for the next task.  */
		fgets(line, MAX_BUF, fp);
	}

	return n;
}

void deadline_monotonic_priority(void)
{
	int i, j;
	ulong_t prio;
	task_t task;

	/* bable sort such that tasks[j-1].D <= tasks[j].D. */
	for (i = 0; i < nr_tasks - 1; i++) {
		for (j = nr_tasks - 1; j > i; j--) {
			if (tasks[j-1].D > tasks[j].D) {
				task.tid 	= tasks[j].tid;
				task.C 		= tasks[j].C;
				task.T 		= tasks[j].T;
				task.D 		= tasks[j].D;

				tasks[j].tid 	= tasks[j-1].tid;
				tasks[j].C 		= tasks[j-1].C;
				tasks[j].T 		= tasks[j-1].T;
				tasks[j].D 		= tasks[j-1].D;

				tasks[j-1].tid 	= task.tid;
				tasks[j-1].C 	= task.C;
				tasks[j-1].T 	= task.T;
				tasks[j-1].D 	= task.D;
			}
		}
	}

	/* set priorities. */
	prio = RESCH_MAX_APP_PRIO;
	for (i = 0; i < nr_tasks; i++) {
		tasks[i].prio = prio;
		prio--;
	}
}

int schedule(FILE *fp, int m, int loop_1ms, int time)
{
	int i, j, ret, status;
	pid_t pid;

	/* task[] must be freed in the end. */
	nr_tasks = read_taskset(fp);
	deadline_monotonic_priority();

	/* all the global variables must be set before.
	   otherwise, the child processes cannot share the variables. */
	for (i = 0; i < nr_tasks; i++) {
		pid = fork();
		if (pid == 0) { /* the child process. */
			/* execute the task. 
			   note that the funtion never returns. */
			exec_task(&tasks[i], loop_1ms, time);
		}
	}

	/* default return value. */
	ret = TRUE;

	/* wait for the child processes. */
	for (i = 0; i < nr_tasks; i++) {
		pid = wait(&status);
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == RET_MISS) {
				ret = FALSE;
			}
		}
		else {
			printf("Anomaly exit.\n");
			ret = FALSE;
		}
	}

 out:
	free(tasks);

	return ret;
}
