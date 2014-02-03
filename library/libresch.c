/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
 *  libresch.c: the RESCH library program
 * 	
 * 	Application programming interface (API) functions for RESCH.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../core/api.h"

void (*user_xcpu_handler)(void) = NULL;

static void xcpu_handler(int signum)
{
	if (user_xcpu_handler) {
		user_xcpu_handler();
	}
	else {
		rt_background();
	}
}

static void kill_handler(int signum)
{
	rt_exit();
	exit(0);
}

/**
 * internal function for APIs, using ioctl() system call.
 */
static inline int __api(unsigned long cmd, unsigned long val)
{
	int fd, ret;

	fd = open(RESCH_DEVNAME, O_RDWR);
	if (fd < 0) {
		printf("Error: failed to access the module!\n");
		return RES_FAULT;
	}
	ret = ioctl(fd, cmd, &val);
	if (ret == -1){
		printf("Error: failed ioctl: cmd %lu, val: %lu\n", cmd, val);
	}
	close(fd);

	return ret;
}

/**
 * internal function for tests, using write() system call.
 */
static inline int __test(unsigned long cmd, 
						 unsigned long val1, unsigned long val2)
{
	int fd, ret;
	unsigned long buf[3];

	fd = open(RESCH_DEVNAME, O_RDWR);
	if (fd < 0) {
		printf("Error: failed to access the module!\n");
		return RES_FAULT;
	}
	buf[0] = cmd;
	buf[1] = val1;
	buf[2] = val2;
	ret = write(fd, buf, sizeof(long)*3);
	close(fd);

	return ret;
}

static inline int __name(char *data) {

	int fd, ret;

	if (strlen(data) > 11) {
		printf("(__name) Error: max nr of allowed characters are 12!\n");
		return RES_FAULT;
	}

	fd = open(RESCH_DEVNAME, O_RDWR);
	if (fd < 0) {
		printf("(__name) Error: failed to access the module!\n");
		return RES_FAULT;
	}

	ret = write(fd, data, sizeof(char)*strlen(data));
	close(fd);

	return ret;

}

/*****************************************************
 *                  API functions                    *
 *****************************************************/

int rt_set_name(char *thename) {
	return __name(thename);
}

int rt_name(char *name){
	return __name(name);
	//return (__api(API_NAME, name) == RES_FAULT) ? 0 : 1;
}

int rt_init(void)
{
	struct sigaction sa_kill;

	/* register the KILL signal. */
	memset(&sa_kill, 0, sizeof(sa_kill));
	sigemptyset(&sa_kill.sa_mask);
	sa_kill.sa_handler = kill_handler;
	sa_kill.sa_flags = 0;
	sigaction(SIGINT, &sa_kill, NULL);
	sigaction(SIGTERM, &sa_kill, NULL);
	
	return (__api(API_INIT, 0) == RES_FAULT) ? 0 : 1;
}

int rt_exit(void)
{
	return (__api(API_EXIT, 0) == RES_FAULT) ? 0 : 1;

}

int rt_run(struct timeval *tv)
{
	unsigned long ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	return (__api(API_RUN, ms) == RES_FAULT) ? 0 : 1;
}

int rt_wait_for_period(void)
{
	return (__api(API_WAIT, 0) == RES_MISS) ? 0 : 1;
}

//NEW
// Note that a previous call to 'rt_reg_task' will affect which task 'rt_set_server' registers to a server
int rt_set_server(unsigned long server_id)
{
	return (__api(API_SERVER, server_id) == RES_FAULT) ? 0 : 1;
}

//NEW
int rt_reg_task(unsigned long task_id)
{
	return (__api(API_VIRTUALIZATION, task_id) == RES_FAULT) ? 0 : 1;
}

//NEW
int rt_reg_run(struct timeval *tv)
{
	unsigned long ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	return (__api(API_RUN_VIRTUAL, ms) == RES_FAULT) ? 0 : 1;
}

int rt_set_period(struct timeval *tv)
{
	unsigned long ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	return (__api(API_PERIOD, ms) == RES_FAULT) ? 0 : 1;
}

int rt_set_deadline(struct timeval *tv)
{
	unsigned long ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	return (__api(API_DEADLINE, ms) == RES_FAULT) ? 0 : 1;
}

int rt_set_wcet(struct timeval *tv)
{
	/* be carefull of overflow. */
	unsigned long us = tv->tv_sec * 1000000 + tv->tv_usec;
	return (__api(API_WCET, us) == RES_FAULT) ? 0 : 1;
}

int rt_set_priority(unsigned long priority)
{
	return (__api(API_PRIORITY, priority) == RES_FAULT) ? 0 : 1;
}

int rt_reserve_cpu(struct timeval *tv, void (*func)(void))
{
	unsigned long ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	struct sigaction sa_xcpu;

	if (func) {
		/* register the XCPU signal. */
		memset(&sa_xcpu, 0, sizeof(sa_xcpu));
		sigemptyset(&sa_xcpu.sa_mask);
		sa_xcpu.sa_handler = xcpu_handler;
		sa_xcpu.sa_flags = 0;
		sigaction(SIGXCPU, &sa_xcpu, NULL);
		/* set a user handler. */
		user_xcpu_handler = func;
		return (__api(API_RESERVE_XCPU, ms) == RES_FAULT) ? 0 : 1;
	}
	else {
		return (__api(API_RESERVE, ms) == RES_FAULT) ? 0 : 1;
	}
}

int rt_background(void)
{
	return (__api(API_BACKGROUND, 0) == RES_FAULT) ? 0 : 1;
}

/*****************************************************
 *                 test functions                    *
 *****************************************************/

int rt_test_switch_begin(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return __test(TEST_SWITCH_BEGIN, tv.tv_sec, tv.tv_usec);
}

int rt_test_switch_end(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return __test(TEST_SWITCH_END, tv.tv_sec, tv.tv_usec);
}

int rt_test_migration(void)
{
	return __test(TEST_MIGRATION, 0, 0);
}
