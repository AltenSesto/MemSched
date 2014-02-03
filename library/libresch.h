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

//void (*user_xcpu_handler)(void) = NULL;

static void xcpu_handler(int signum);
static void kill_handler(int signum);

/**
 * internal function for APIs, using ioctl() system call.
 */
static inline int __api(unsigned long cmd, unsigned long val);

/**
 * internal function for tests, using write() system call.
 */
static inline int __test(unsigned long cmd, 
						 unsigned long val1, unsigned long val2);

static inline int __name(char *data);

/*****************************************************
 *                  API functions                    *
 *****************************************************/

int rt_set_name(char *thename);

int rt_name(char *name);

int rt_init(void);

int rt_exit(void);

int rt_run(struct timeval *tv);

int rt_wait_for_period(void);

//NEW
// Note that a previous call to 'rt_reg_task' will affect which task 'rt_set_server' registers to a server
int rt_set_server(unsigned long server_id);

//NEW
int rt_reg_task(unsigned long task_id);

//NEW
int rt_reg_run(struct timeval *tv);

int rt_set_period(struct timeval *tv);

int rt_set_deadline(struct timeval *tv);

int rt_set_wcet(struct timeval *tv);

int rt_set_priority(unsigned long priority);

int rt_reserve_cpu(struct timeval *tv, void (*func)(void));

int rt_background(void);

/*****************************************************
 *                 test functions                    *
 *****************************************************/

int rt_test_switch_begin(void);

int rt_test_switch_end(void);

int rt_test_migration(void);
