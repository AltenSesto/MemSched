#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

echo -e "Description:\n
	EXPERIMENT 2: ALL TASKS ARE NORMAL TASKSK except Task 3 which is Problem task executing in SERVER 1.\n"

if [ $# -ne 1 ];then
	echo "usage: sample.sh [execution time of memsched]"
	exit
fi
echo "Enter 'y' to continue and 'n' to exit the script > "; read tocontinue

if [ $tocontinue == "n" ]; then
	echo "Exiting!!";exit
fi

exec_time=$1

#clear the kernel buffer ring
dmesg -c

#clear the cache
free && sync && echo 3 > /proc/sys/vm/drop_caches && free

#compile and install RESCH module
cd ../../../core
./configure
make
make install
#if [ $? -ne 0 ];then echo "error: resch module could not install!";exit; fi

# compile and insert memsched module
cd ../plugin/memsched
make clean; make
if [ $? -ne 0 ];then echo "error: memsched could not compile!"; rmmod resch; exit; fi

insmod memsched.ko
if [ $? -ne 0 ];then echo "error: memsched module not inserted!";exit; fi

# Start tasks: pri,period,wcet,runtime,taskname,server it belongs to, timeout(for sync in sec)
cd experiment2_sample
make
./start.sh $exec_time & 

#synchronization time: after this time the server and tasks are supposed to start at the same time.
sleep 9
echo "Task and Servers syncronized and started execution."
# execution time of memsched
sleep $(($exec_time - 5))

#remove memsched
rmmod memsched
if [ $? -ne 0 ];then 
	echo "error: memsched module not removed!"
else
	echo "memsched removed successfully."
fi
# A relax time for assuring all tasks exited before removing the kernel modules!
sleep 15

#remove resch
rmmod resch
if [ $? -ne 0 ];then echo "error: resch module not removed!"; fi

