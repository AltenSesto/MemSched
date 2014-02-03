GENERAL INFORMATION
-------------------
MemSched is a multi-resource server implementation for multicore architecture based on CPU budget and Memory bandwidth (refer to: "The Multi-Resource Server for Embedded Real-Time Systems" by Rafia Inam et. al). It is a Linux Kernel Module (KLM) developed on top of RESCH KLM as an external and hierarchical scheduler. It has its own API for managing real-time tasks from user space.

COMPONENTS OF MEMSCHED
----------------------
1. RESCH - this is an external real-time scheduler which gives service to memsched. It implements queue for tasks created in MemSched.
2. memsched - is a plugin to RESCH for implementing the multi-resource server in multicore architecture.
3. Library/ API - a set of functions for creating and manipulating real-time tasks from user space.
4. sample.sh - this is a bash script (it could be any other name given by the programmer) for starting a real-time application developed in  MemSched. It includes RESCH and memsched KLM insertion to Linux Kernel Module tree; and finally starts the multi-resource server and tasks defined by a user.
	This file includes the following parts:
		1. RESCH installation
		2. memsched installation
		3. task initialization
		4. Synchronization time - the tasks are not supposed to be ready before server activation. Otherwise, task and server execution will not be as expected.
			Therefore, giving an absolute time for tasks and servers or else the tasks start early is a correct way of initializing the system.
		5. Sleep until servers and tasks finish execution - the script will sleep until servers and tasks finish execution. This is to avoid removal the kernel modules at early stage.
		6. Remove memsched from kernel 	 # rmmod memsched
		7. Remove resch from kernel	 # rmmod resch
5. start.sh - this is bash script (could be any other name as long as it is the same with the name in sample.sh) which holds real-time tasks declaration.
	Syntax: [priority] [period] [wcet] [server] [runtime - for how long the task is supposed to execute]
		This syntax is compatible to tasks defined in the samples. Depending on tasks definition, this is different.

FILES AND DIRECTORIES
--------------------
1. core directory - holds the RESCH KLM implementation.

2. plug-in directory - holds memsched implementation.
	1. memsched.c -  is the actual multi-resource server implementation with kernel programming.
	2. ready_queue - ready queue implementation for servers.
	3. release_queue - release queue implementation for server which are waiting for activation from kernel timer.
	4. Experiment1_sample directory - demonstrates usage MemSched for normal-tasks (real-time tasks which does not produce intensive memory request and cause 	other real-time tasks to miss deadline).
	5. Experiement2_sample directory - demonstrates MemSched application for legacy application, Mplayer. It enables Mplayer to play at 24 frames/sec (normal play) in a situation where other real-time tasks produce a lot of memory request. This is accomplished by throttling other real-time tasks to an acceptable level and without missing deadline. If other real-time tasks are not throttled, the mplayer plays a movie (e.g. avatar.avi) in bad quality and dropping several frames.

3. library directory - holds an API for coding real-time tasks from user space.


PREREQUISITE
-------------
1. To use high resolution tick of 1ms, use a linux kernel built from CONFIG_HZ_1000=y.
2. MemSched is compatible in vanilla or real-time patched kernel. It is tested on 2.6.x and 3.6.x.
3. create a directory 
	# mkdir /usr/src/kernels/$(shell uname -r)/include/resch
	If this did not work, create each path manually. First kernels, then output of #uname -r, includes and resch directory, one after the other which changing directory.

4. make and install the RESCH core:
	cd core
	./configure --task=(the maximum nubmer of real-time tasks)
	            --cpus=(the number of used CPUs)
	make
	sudo make install

5.  cd plugin/hsf/hsf-fp
    Open 'memsched.c' with your favorite editor. Change the following lines:

    #include <../../kernels/[2.6.31-20-generic-pae or kernelVer]/include/resch/config.h>
    #include <../../kernels/[2.6.31-20-generic-pae or kernelVer]/include/resch/core.h>

    The '2.6.31-20-generic-pae' part should be replaced with the string generated with the shell command: 'uname -r'
    Now, remember to issue 'make clean', './configure', and 'make' in the 'core' directory before you continue.
    Do NOT type 'sudo make install' since this will be done by the [name]_sample.sh' script.

6. compile library
	# cd library  // assuming you are now at MemSched level
	# make
	# cp libresch.a /usr/lib/
	If you wana you casestudy_experiement2_sample or mplayer_ffmpeg_sample, Create the resch directory under /usr/lib/ first; and then copy the libresch.a to /usr/lib/resch. 
	This is because mplayer and ffmpeg check use APIs from that location.

	
COMPILING AND USING MEMSCHED
----------------------------
1. Getting the source or if you have it already extract it to /usr/local/shared or your home directory. 
	# tar xzvf MemSched_final.tar.gz
or 	# unzip MemSched_final.zip
2. compile
		1. open memsched.c using your favorite editor.
		2. make some changes to memsched.c, such as creating servers or enabling/disabling memory throttling.
		3. save and exit.
		4. compile it with # make
3. creating a new application	
		The easiest way to start using MemSched is to follow the samples found in memsched directory.
		1. Create a new directory in memsched directory example: 
			# cd MemSched/plugin/memsched
			# mkdir test
			# cd test
		2. According to your server model specification, modify and compile server initialization in memsched.c as described in step 2.	
		3. create real-tasks. For this example I will copy a single task from experiment1_sample.
			# cp experiement1_samples/normal_task.c or create your own task using the API library.
		4. Similarly copy Makefile from experiment1_sample and modify it.
			# cp experiment1_sample/Makefile .
			# make
		5. Copy and modify start.sh from experiement1_sample.
		5a. Download the movie clip you want to use for benchmarking into the same folder as start.sh. 
			You also need to rename movie1.mov to the desired clip in your start.sh file. 
		6. Give root previledge to start.sh

			# chmod +x start.sh

		7. Finally copy and past sample.sh from experiement1_sample.
		8. make sure it has the root privilege
		9. To launch the application execute ./sample.sh from the terminal
		10. to check out the trace or any other log 
			# dmesg|more and to clear the log # demesg -c
4. Sometimes you may want to modify and compile RESCH alone. This can be done first by changing directory to the root of MemSched.
	# cd MemSched
	And modify resch.c or core.h depending on what you want to do.
	# make // Dont do make install unless you want to run your application without memsched.
	
EXPERIMENTAL SAMPLES
--------------------
1. Experiment1_sample     #Experiment1 of the paper.

	This has real-time tasks of type 'normal tasks'. A 'normal task' in our experiment describes tasks consume too much memory request that could disrupt other tasks in the same or different cores. In other words have little impact on CACHE THRASHING or memory bandwidth usage. The following is a structure of 'normal task'. Check out normal_task.c for implementation.
	
	//..
	1. create linked list of MSEC_UNIT nodes. From our experiment we found out 1ms execution to write data to each node sequentially from head to tail.
	...
	2. WHILE(TRUE)
		
		FOR each node in the linked list, 
			write integer value to the data variable.
		END_OF_FOR
		
	3. CALL rt_wait_for_period()
	...
	4. END_OF_WHILE
	
	This task template is developed in such a way to execute for at most exec amount of mseconds input from task parameter in start.sh file.
	In this experiment we are able to create 140000 amounts of nodes in 1ms. And this is dependent on different computer architecture. From this experiment we learnt to normal_tasks based on CPU budget and memory budget using MemSched. The following is a server and task parameter specification used.
	
	Server parameter specification
	-----------------------------
	//core-0 
	init_server(&SERVERS[0], 0, 24, 8 , 0, 0,650); // id,period(ms),budget(ms),prio,core,budget(mem)
	init_server(&SERVERS[1], 1, 40, 16, 1, 0,750); 
	
	// core-1 
	init_server(&SERVERS[2], 2, 40, 8 , 2, 1,900); 
	init_server(&SERVERS[3], 3, 80, 12, 0, 1,1100); 
	init_server(&SERVERS[4], 4, 80, 12, 3, 1,1100); 
    I.e., 2 servers must correspond to 2 'init_server' lines. The parameters are set in ms but they will be converted
    to 'jiffies'.
    #define RUN_JIFFIES 30 defines the amount of jiffies to run the scheduler before halting.
 
    queue.c and release-queue.c have important defines that decides the queue sizes. #define REL_Q_SIZE and
    #define BITMAPSIZE define these sizes. They should be proportionate to the server release times. Contact us if
    support is needed.


	task parameter specification
	----------------------------
	#core-0
	./normal_task 98 40 2 $1 T1 0 10 & // [priority] [period] [wcet] [task name] [server] [runtime in sec]
	./normal_task 97 48 4 $1 T2 0 10 &
	./normal_task 98 40 8  $1 T3 1 10 &
#./problem_task 98 40 8  $1 T3 1 10 &   For experiment2 of the paper remove third task and use this memory-intensive one.
#./rt_task 98 40 8  $1 T3 1 10 &      For experiment3 of the paper remove third task and use this cache-polluting task.
#codes for rt_task and problem_task are in the old directory

	#core-1
	./normal_task 98 60  4 $1 T4 2 10 & #S2 
	./normal_task 97 160 10 $1 T5 2 10 & #S2 
	./normal_task 97 160 14 $1 T6 3 10 & #S3 
	./normal_task 98 200 8 $1 T7 4 10 & #S4 
	./normal_task 97 200 8 $1 T8 4 10 & #S4 

The task c-files in (in directory 'tasks') connect to servers through the function call 'rt_set_server()'.
    'rt_set_server(0)' will connect to 'init_server(&SERVERS[0], 0, 24, 8 , 0, 0,550)' and 'rt_set_server(1)' will
    connect to server ' init_server(&SERVERS[1], 1, 40, 16, 1, 0,750)'. Remember in task priority 97 is higher than 96 (opposite to servers).

 Last but not least, observe that each server has a memory budget given as last argument in init_server. This is the number of 
    last level cache misses a server can do. When one of the budgets depletes(CPU or Memory) the server is stopped and must wait for a new period.

	
2. Casestudy_Experiment2_sample
	This experiment involves normal tasks and mplayer playing avatar.avi. This is to demonstrate a collective effect of memory bandwidth by normal_tasks on mplayer. First the mplayer and normal_tasks are started without memory bandwidth throttling. During this time the mplayer could hardly play the movie smoothly due to shared  memory bandwidth and CACHE TRASHING effect by normal_tasks. And the mplayer is observed to drope more than 10% of total frames as a result. In the second test, we applied memory bandwidth throttling with proper memory bandwidth budget to normal_tasks and the result showed no deadline miss while the mplayer succeeded to play avatar.avi without any drop of frames.
	
EXPERIMENT 2: ALL TASKS ARE NORMAL TASKSK IN ALL SERVERS EXCEPT SERVER1 THAT EXECUTES mplayer.

	The following the serever and task parameter specification used.
	
	server parameter specification
	------------------------------
	//core-0 
	init_server(&SERVERS[0], 0, 15, 10 , 1, 0); // id,period(ms),budget(ms),prio,core,budget(mem). It is mplayer server and not throttled for memory

	// core-1 
	init_server(&SERVERS[1], 2, 80, 12, 2, 1,1100); 
	init_server(&SERVERS[2], 3, 80, 12, 0, 1,1100); 
	
	task parameter specification
	-----------------------------
	#core-0


	task_para=(98 40 10 $1 "MP" 1 9)
	echo ${task_para[*]} > /tmp/taskinfo
	mplayer -hardframedrop -benchmark avatar_1920_800_24fps_h264.mov &
	
	#core-1

	./normal_task 97 160 10 $1 T5 2 9 & #S2 
	./normal_task 97 160 14 $1 T6 3 9 & #S3 

	./normal_task 98 200 8 $1 T7 4 9 & #S4 

	./normal_task 97 200 8 $1 T8 4 9 & #S4 

MISCELLANEOUS
-------------
1. If you are wondering how to use RESCH without memsched, checkout INSTALL file.
2. make sure all tasks are removed from the system before removing the kernel modules: memsched and resch. Sometimes, resch module cannot be removed due to some tasks not properly removed from requeues or have already became zombie. And the system assumes RESCH is being used,this time you need to restart to see changes you make to resch. If nothing is modified to resch but could not be removed, it is ok to continue working on memsched changes, no need to restart the system.

CONTACT
-------
MemSched: 	Rafia Inam <rafia.inam@mdh.se>
Multicore: 	Nesredin M.Mahmud <nesredin.mom@gmail.com>
Memory: 	Joris <>

Thank you and fun! Don't forget to contact us for feedback, questions or any progress on MemSched.
//nesredin m.j
