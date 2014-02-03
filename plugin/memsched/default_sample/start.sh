#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

#syntax: task_para=([priority] [period] [wcet] [runtime] [server id] [timeout for sync with servers])

#core-0
./test_task 98 40 4 $1 T1 0 2 & 
#./normal_task 98 40 2 $1 T1 0 10 &
#./problem_task 98 40 2 $1 T1 0 10 &

#task_para=(98 40 10 $1 "MP" 0 9)
#echo ${task_para[*]} > /tmp/taskinfo
#mplayer -hardframedrop -benchmark movie1.mov &

#core-1
#./normal_task 98 60  4 $1 T2 1 10 & #S2 
