#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

#Joris Experiment
#syntax: [priority] [period] [wcet] [server] [runtime]
#core-0
./rt_task1 98 40 2 & #S0 
./rt_task2 97 48 4 & #S0 
./rt_task3 98 60 8 & #S1 
#core-1
./rt_task4 98 60  2 & #S2 
./rt_task5 97 160 4 & #S2 
./rt_task6 97 200 4 & #S3 
./rt_task7 98 200 4 & #S4 
./rt_task8 97 240 4 & #S4 
