/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
 * test_overhead.c:	test the scheduling overhead.
 */

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <resch/api.h>
#include <resch/tvops.h>

#define DEFAULT_TIMEOUT 500

/* macro to convert microseconds to timeval struct. */
#define msecs_to_timeval(ms, tv)					\
	do {											\
		tv.tv_sec = ms / 1000; 						\
		tv.tv_usec = (ms - tv.tv_sec*1000) * 1000; 	\
	} while (0);

void test_switch_begin(void)
{
	cpu_set_t cpuset;
	struct timeval tv_period, tv_timeout;
	struct timeval tv1, tv2, tv3;

	msecs_to_timeval(DEFAULT_TIMEOUT, tv_timeout);
	msecs_to_timeval(DEFAULT_TIMEOUT + 1000, tv_period);

	/* initialization for using RESCH. */
	if (!rt_init()) {
		printf("Error: cannot begin!\n");
		goto out;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
		printf("Failed to migrate the process\n");
		rt_exit();
		goto out;
	}

	rt_set_priority(RESCH_MAX_APP_PRIO);
	rt_set_period(&tv_period);
	rt_run(&tv_timeout);

	/* busy loop for a second.
	   during this loop, the other process that measures the end of 
	   context switching will wake up. */
	gettimeofday(&tv1, NULL);
	for (;;) {
		gettimeofday(&tv2, NULL);
		tvsub(&tv2, &tv1, &tv3);
		if (tv3.tv_sec >= 1) {
			break;
		}
	}

	rt_test_switch_begin();
	rt_wait_for_period();
	rt_exit();
 out:
	_exit(0);
}

void test_switch_end(void)
{
	cpu_set_t cpuset;
	struct timeval tv_period, tv_timeout;
	struct timeval tv1, tv2, tv3;

	/* set the timeout so that this process will wake up during the
	   busy loop of the other process that measures the beginning of
	   context switching. */
	msecs_to_timeval(1000, tv_timeout);
	msecs_to_timeval(RESCH_PERIOD_INFINITY, tv_period);

	/* initialization for using RESCH. */
	if (!rt_init()) {
		printf("Error: cannot begin!\n");
		goto out;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
		printf("Failed to migrate the process\n");
		rt_exit();
		goto out;
	}

	rt_set_priority(RESCH_MAX_APP_PRIO - 1);
	rt_set_period(&tv_period);
	rt_run(&tv_timeout);

	/* just let the RESCH core know the context is switched. */
	rt_test_switch_end();
	rt_exit();
 out:
	_exit(0);
}

void test_migration(void)
{
	struct timeval tv_period, tv_timeout;

	msecs_to_timeval(DEFAULT_TIMEOUT, tv_timeout);
	msecs_to_timeval(RESCH_PERIOD_INFINITY, tv_period);

	/* initialization for using RESCH. */
	if (!rt_init()) {
		printf("Error: cannot begin!\n");
		return ;
	}

	rt_set_priority(RESCH_MAX_APP_PRIO);
	rt_set_period(&tv_period);
	rt_run(&tv_timeout);

	rt_test_migration();

	rt_exit();
}

int main(void)
{
	int i;
	int status;
	pid_t pid;

	printf("Context switch test:");
	fflush(stdout);
	for (i = 0; i < 10; i++) {
		putchar('*');
		fflush(stdout);
		/* a process to measure the time at which context switch begins. */
		pid = fork();
		if (pid == 0) {
			/* this function never returns. */
			test_switch_begin();
		}

		/* a process to measure the time at which context switch ends. */
		pid = fork();
		if (pid == 0) {
			/* this function never returns. */
			test_switch_end();
		}

		/* wait for the child processes. */
		wait(&status);
		wait(&status);
	}
	printf("[Done]\n");

	printf("Migration test:");
	fflush(stdout);
	for (i = 0; i < 10; i++) {
		putchar('*');
		fflush(stdout);

		test_migration();
	}
	printf("[Done]\n");

	return 0;
}
