#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

# experiment is not in the paper
echo -e "Description:\n
	EXPERIMENT 3: ON CORE0: MPLAYER; ON CORE1: FFMPEG EXECUTE.\n"

echo "Enter 'n' to exit and anyother character to continue > "; read tocontinue
if [ $tocontinue == "n" ]; then
	echo "Exiting!!";exit
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
if [ $? -ne 0 ];then echo "error: resch module could not install!";exit; fi

# compile and insert memsched module
cd ../plugin/memsched
make
if [ $? -ne 0 ];then echo "error: memsched could not compile!";exit; fi

insmod memsched.ko
if [ $? -ne 0 ];then echo "error: memsched module not inserted!";exit; fi

# Start tasks: pri,period,wcet,runtime,taskname,server it belongs to, timeout(for sync in sec)
cd mplayer_ffmpeg_sample
./start.sh &

#synchronization time: after this time the server and tasks are supposed to start at the same time.
sleep 9
## other activities such as tracing....

echo -e "remove the  modules when the applications are done executing!\n
	shell command: rmmod memsched; rmmod resch"

