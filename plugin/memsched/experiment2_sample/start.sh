#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

#syntax: task_para=([priority] [period] [wcet] [runtime] [application name] [server id] [timeout for sync with servers])

#core-0
./normal_task 98 40 2 $1 T1 0 10 &
./normal_task 97 48 4 $1 T2 0 10 &
./problem_task 98 40 8  $1 T3 1 10 &   

#core-1
./normal_task 98 60  4 $1 T4 2 10 & #S2 
./normal_task 97 160 10 $1 T5 2 10 & #S2 
./normal_task 97 160 14 $1 T6 3 10 & #S3 
./normal_task 98 200 8 $1 T7 4 10 & #S4 
./normal_task 97 200 8 $1 T8 4 10 & #S4 
