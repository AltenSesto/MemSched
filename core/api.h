/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
#ifndef __API_H__
#define __API_H__

/* device name. */
#define RESCH_DEVNAME	"/dev/resch"

/* available priorities. */
#define RESCH_MAX_APP_PRIO	96
#define RESCH_MIN_APP_PRIO	4

/* period value for non-perioid tasks. */
#define RESCH_PERIOD_INFINITY	0

/* API command numbers. */
#define API_INIT	 		1
#define API_EXIT			2
#define API_RUN 			3
#define API_WAIT 			4
#define API_PERIOD 			5
#define API_DEADLINE 		6
#define API_WCET			7
#define API_PRIORITY		8
#define API_RESERVE			9
#define API_RESERVE_XCPU	10
#define API_BACKGROUND		11
#define API_SERVER		12
#define API_VIRTUALIZATION	13
#define API_RUN_VIRTUAL		14

/* test command numbers. */
#define TEST_SWITCH_BEGIN	101
#define TEST_SWITCH_END		102
#define TEST_MIGRATION		103

/* result values. they should be non-negative! */
#define RES_SUCCESS	0	/* sane result. */
#define RES_FAULT	1	/* insane result. */
#define RES_ILLEGAL	2	/* illegal operations. */
#define RES_MISS	3 	/* deadline miss. */

#endif
