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
#include <time.h>

#define MSEC_UNIT      85706//(MDH)//980 this is the number of nodes that will be created in the list to loop through for 1 millisecond
//#define MSEC_UNIT      140000//(MDH)//980 this is the number of nodes that will be created in the list to loop through for 1 millisecond

#define msecs_to_timeval(ms, tv)					\
	do {											\
		tv.tv_sec = ms / 1000; 						\
		tv.tv_usec = (ms - tv.tv_sec*1000) * 1000; 	\
	} while (0);

struct ListItem {
	 unsigned long data;
	 struct ListItem * next;
};
typedef struct ListItem item, item2;

int deadlinemiss_count = 0;
int main(int argc, char* argv[])
{
	item *curr , * head;
//	item *curr2 , * head2 = NULL;
	head = NULL;
	
	if (argc != 8){
		printf("Number of arguments is not correct!\n");
		printf("[prio] [period] [wcet] [timeout] [taskname, e.g., 'T1'] [server it belongs to, e.g.,0 for S0]\n");		
		exit(EXIT_SUCCESS);	
	}

	int i, j, runtime = 60, k;
	unsigned long prio, exec;
	struct timeval period, wcet, timeout;
	struct timeval tv;	
	char task_name[1024];
	double elapsed_time;

	printf("%s:%d\n", argv[0], getpid() );

	exec = atoi(argv[3]);

	//exec = (2*atoi(argv[3])/3)*MSEC_UNIT;//the task executes 2/3 of wcet in a period
	prio = atoi(argv[1]);				/* priority. */
	msecs_to_timeval(atoi(argv[2]), period);	/* period. */
	msecs_to_timeval(atoi(argv[3]), wcet);		/* wcet. */
	msecs_to_timeval(9000, timeout);		/* timeout. */
	runtime = atoi(argv[4]);
	strcpy(task_name,argv[5]);

	/* bannar. */
	printf("sample program %s\n", argv[0]);

	rt_init(); 
	rt_set_priority(prio);
	rt_set_period(&period);
	rt_set_wcet(&wcet);
        rt_set_server(atoi(argv[6]));
//	rt_name(task_name);
	//create the linked list
	for(j = 0; j < MSEC_UNIT; j++){
		curr = (item *)malloc(sizeof(item));		
//		curr2 = (item2 *)malloc(sizeof(item2));		
		curr->next = head;
//		curr2->next = head2;
		head = curr;
//		head2 = curr2;
	}
	
	//start initial timeout
	rt_run(&timeout);
	time_t start = time(NULL);
	int count = 0;
	while(1){
		//loop through the whole list and change all node value ones
		for (k = 0; k < exec; ++k){
		curr = head;	
		j = 0;
		while(curr){
			curr->data = j;  
			curr = curr->next;
			j++;		
		}
		}
	
		//check end of task execution
		if ((elapsed_time = difftime(time(NULL), start)) >= runtime){
			//printf("%s is done in %G seconds!\n", task_name, elapsed_time);
			printf("%s Task missed counts =  %d out of %d in elapsed time= %f\n", task_name, deadlinemiss_count, count, elapsed_time);
			rt_exit();
			exit(EXIT_SUCCESS);		
		}
		++count;
		if (!rt_wait_for_period()) {
		//	printf("%s missed deadline !\n", task_name);			
			++deadlinemiss_count;
		}
	}
	
	return 0;
}
