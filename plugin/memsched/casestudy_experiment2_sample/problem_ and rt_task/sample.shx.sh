#!/bin/bash

echo -e "Description:\n
	This sample demonstrates the effect and control  of 'problem tasks - a task with intensive memory request and causing cache thrashing'
	using memsched. All tasks are real time and 'normal tasks' except the 'problem task'. The problem task executes at server0 core-0; and
	causes any tasks in core-o and core-1 to miss deadline due to cache thrashing it causes. As a result, the CPU is obliged to
	fetch data/instraction related to the normal tasks causing it longer time enough to miss the tasks' deadline. By throttling the memory
	bandwidth of the 'problem task' using memsched it is made possible to avoid any deadline miss of the normal tasks.(Calibrate to different
	levels of server1 memory budget to see the effect of problem task on the number of deadline missed of normal tasks.)
	"

if [ $# -ne 1 ];then
	echo "usage: sample.sh [execution time of memsched]"
	exit
fi
echo "Enter 'y' to continue and 'n' to exit the script > "; read tocontinue

if [ $tocontinue == "n" ]; then
echo "Exiting!!";exit
else
	echo "unknown input!";exit
fi

#clear the kernel buffer ring
dmesg -c

#clear the cache
free && sync && echo 3 > /proc/sys/vm/drop_caches && free

#compile and install RESCH module
cd ../../../core
./configure
make
make install

# compile and insert multi-hsf-fp module
cd ../plugin/hsf/hsf-fp
make

insmod multi-hsf-fp.ko
if [ $? -ne 0 ];then echo "error: multi-hsf module not inserted!";exit; fi

# Start tasks: pri,period,wcet,runtime,taskname,server it belongs to
cd tasks
#core-0
./normal_task 98 40 2 $1 T1 0 10 &
./normal_task 97 48 4 $1 T2 0 10 &
./normal_task 98 40 8  $1 T3 1 10 &

#core-1
./normal_task 98 60  4 $1 T4 2 10 & #S2 
./normal_task 97 160 10 $1 T5 2 10 & #S2 
./normal_task 97 160 14 $1 T6 3 10 & #S3 
./normal_task 98 200 8 $1 T7 4 10 & #S4 
./normal_task 97 200 8 $1 T8 4 10 & #S4 

cd ../ 

sleep 9 #timeout so that the tracer will log server and task execution as the same time.

echo "Tracing enabled"

sleep $1 #exection time of hsf module

sleep 5

#remove hsf module : tasks will terminate before this module removes!
rmmod multi-hsf-fp
if [ $? -ne 0 ];then echo "error: multi-hsf module not removed!"; fi

rmmod resch
if [ $? -ne 0 ];then echo "error: resch module not removed!"; fi

echo "Tracing disabled"

make clean
