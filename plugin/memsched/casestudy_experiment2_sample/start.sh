#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

# case study Experiment 2 of paper
#syntax: task_para=([priority] [period] [wcet] [runtime] [application name] [server id] [timeout for sync with servers])

#core-0
task_para=(98 40 10 $1 "MP" 0 9)
echo ${task_para[*]} > /tmp/taskinfo
mplayer -hardframedrop -benchmark movie1.mov &

#core-1 
./normal_task 97 160 10 $1 T5 1 9 & #S1 
#./normal_task 97 160 14 $1 T6 1 9 & #S1 
./normal_task 98 200 8 $1 T7 2 9 & #S2 
#./normal_task 97 200 8 $1 T8 2 9 & #S2 
