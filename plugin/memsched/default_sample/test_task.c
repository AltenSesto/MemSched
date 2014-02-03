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

//#define MSEC_UNIT      85706//(MDH)//980 this is the number of nodes that will be created in the list to loop through for 1 millisecond
#define MSEC_UNIT      140000//(MDH)//980 this is the number of nodes that will be created in the list to loop through for 1 millisecond
#define L2_CACHE_LENGTH 524288 //1048576

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
item *curr , * head;
//head = NULL;
unsigned long exec;
int j,k;

inline void readLinkedList()
{}

inline void writeLinkedList()
{
	for (k = 0; k < exec; ++k){
        	curr = head;
                j = 0;
                while(curr){
                	curr->data = j;
                        curr = curr->next;
                        j++;
                }
        }

}

inline void createLinkedList()
{
	head = NULL;
        for(j = 0; j < MSEC_UNIT; j++){
                curr = (item *)malloc(sizeof(item));
                curr->next = head;
                head = curr;
        }
	
	/* free the linked list
	curr = head;
	while (curr){
		free(curr);
		curr = curr->next;
	}*/
}

inline void clear_l2_cache(int *C){

	int i;
	for (i = 0; i < L2_CACHE_LENGTH; ++i){
		
		C[i] = i;
	}
}

inline void read_Half_array(int *A){

        int i, j=0, x;
        for (i = 0; i < L2_CACHE_LENGTH/2; ++i){

                x = A[i];
        }
        ++j;
        printf("i %d j %d", i, j);
}

inline void read_array(int *A){

	int i, j=0, x;
	for (i = 0; i < L2_CACHE_LENGTH; ++i){
		
		x = A[i];
//		x = B[i];
	}
	++j;
	printf("i %d j %d", i, j);
}
int main(int argc, char* argv[])
{
//printf("H\n");exit(1);
if (argc != 8){
		printf("Number of arguments is not correct!\n");
		printf("[prio] [period] [wcet] [timeout] [taskname, e.g., 'T1'] [server it belongs to, e.g.,0 for S0]\n");		
		exit(EXIT_SUCCESS);	
	}

	int runtime = 60;
	unsigned long prio;
	struct timeval period, wcet, timeout;
	struct timeval tv;
	struct timespec start, end;	
	char task_name[1024];
	double elapsed_time;
	printf("%s:%d\n", argv[0], getpid() );
	int A[L2_CACHE_LENGTH], C[L2_CACHE_LENGTH],D[L2_CACHE_LENGTH] ;
	int i;
	for(i=0;i<L2_CACHE_LENGTH;++i){A[i]=i;}

//	for(i=0;i<L2_CACHE_LENGTH;++i){B[i]=0;}
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

	// clear cache
	clear_l2_cache(C);
	clear_l2_cache(D);

	// create linked list
//	createLinkedList();

	
	//start initial timeout
	rt_run(&timeout);
	//time_t start = time(NULL);
	clock_gettime(CLOCK_MONOTONIC, &start);		
	int count = 0;
	while(1){
  		     //for(i=0;i<L2_CACHE_LENGTH;++i){A[i]=i;}
		read_array(A);
		 read_Half_array(C);
		printf("  H  ");
		clock_gettime(CLOCK_MONOTONIC, &end);		
		//elapsed_time = difftime(time(NULL), start);
		printf("elapsed time : %ld\n", end.tv_nsec - start.tv_nsec);
		
//check end of task execution
	//	if (elapsed_time  >= runtime){

			//printf("#nodes %d bytes/node %d totalbytes/node %d\n", j, sizeof(struct ListItem), sizeof(struct ListItem) * j);			
//			printf("%s is done in %G seconds!\n", task_name, elapsed_time);
//			printf("%s Task missed counts =  %d out of %d   \n", task_name, deadlinemiss_count, count);
			rt_exit();
			exit(EXIT_SUCCESS);		
	//	}

/*		++count;
		if (!rt_wait_for_period()) {
		//	printf("%s missed deadline !\n", task_name);			
			++deadlinemiss_count;
		}*/
	}
	
	return 0;
}

