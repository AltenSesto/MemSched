/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 */
/*
 * A sample program for RESCH.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>

#define MSEC_UNIT      85706//(MDH)//980 this is the number of nodes that will be created in the list to loop through for 1 millisecond

#define msecs_to_timeval(ms, tv)					\
	do {											\
		tv.tv_sec = ms / 1000; 						\
		tv.tv_usec = (ms - tv.tv_sec*1000) * 1000; 	\
	} while (0);

struct ListItem {
	 unsigned long data;
	 struct ListItem * next;
};
typedef struct ListItem item;


int main(int argc, char* argv[])
{
	item *curr , * head;
	head = NULL;

	int i, j;
	unsigned long prio, exec;
	struct timeval period, wcet, timeout;
	struct timeval tv;	
	printf("%s:%d\n", argv[0], getpid() );

	exec = atoi(argv[3])*MSEC_UNIT;
	prio = atoi(argv[1]);				/* priority. */
	msecs_to_timeval(atoi(argv[2]), period);	/* period. */
	msecs_to_timeval(atoi(argv[3]), wcet);		/* wcet. */
	msecs_to_timeval(9000, timeout);		/* timeout. */

	/* bannar. */
	printf("sample program %s\n", argv[0]);

	rt_init(); 
	rt_set_priority(prio);
	rt_set_period(&period);
	rt_set_wcet(&wcet);
        rt_set_server(0);

	//create the linked list
	for(j = 0; j < exec; j++){
		curr = (item *)malloc(sizeof(item));		
		curr->next = head;
		head = curr;
	}
	
	//start initial timeout
	rt_run(&timeout);

	for (i = 0; i < 300; i++) {
		printf("T1S\n");
		//loop through the whole list and change all node value ones
		curr = head;	
		j = 0;
		while(curr){
			curr->data = j;			
			curr = curr->next;
			j++;		
		}

		printf("T1C\n");
		if (!rt_wait_for_period()) {
			printf("T1	deadline is missed!\n");			
		}
	}
	printf("(%s) done!\n", argv[0]);
	rt_exit();
	
	return 0;
}

