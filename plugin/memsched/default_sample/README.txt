/* 
* Copyright (c) 2014 MDH.
* All rights reserved. This program and the accompanying materials
* are made available under the terms of the GNU Public License v3.0
* which accompanies this distribution, and is available at
* http://www.gnu.org/licenses/gpl.html
*/
DESCRIPTION: EXPERIEMENT-1: ALL TASKS ARE NORMAL TASKS
Experiment1 of the paper.
-----------

This is an experiment to demonstrate usage of memsched on real-time tasks which will exhibit deadline miss if only CPU usage. However, if memory bandwidht is applied to servers, the real-time tasks won't miss deadline. This shows the effect of memory  bandwidht on real-time tasks. The type of tasks used in this experiment are all normal tasksk (tasks that does not produce intensive memory bandwidth).

SERVER PARAMETER SPECIFICATION
------------------------------
	init_server(&SERVERS[0], 0, 24, 8 , 0, 0,650); // id,period(ms),budget(ms),prio,core,budget(mem)
	init_server(&SERVERS[1], 1, 40, 16, 1, 0,750); 
	
	// core-1 
	init_server(&SERVERS[2], 2, 40, 8 , 2, 1,900); 
	init_server(&SERVERS[3], 3, 80, 12, 0, 1,1100); 
	init_server(&SERVERS[4], 4, 80, 12, 3, 1,1100); 


TASKS PARAMETER SPECIFICATION
------------------------------
	#core-0
	./normal_task 98 40 2 $1 T1 0 10 & // [priority] [period] [wcet] [task name] [server] [runtime in sec]
	./normal_task 97 48 4 $1 T2 0 10 &
	./normal_task 98 40 8  $1 T3 1 10 &

	#core-1
	./normal_task 98 60  4 $1 T4 2 10 & #S2 
	./normal_task 97 160 10 $1 T5 2 10 & #S2 
	./normal_task 97 160 14 $1 T6 3 10 & #S3 
	./normal_task 98 200 8 $1 T7 4 10 & #S4 
	./normal_task 97 200 8 $1 T8 4 10 & #S4 

RESULT SUMMARY
RESULT
------
1. Without memory bandwidth throttling
2. with memory throttling



