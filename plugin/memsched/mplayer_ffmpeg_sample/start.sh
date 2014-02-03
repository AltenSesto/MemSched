#!/bin/bash
# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

#syntax: task_para=([priority] [period] [wcet] [runtime] [application name] [server id] [timeout for sync with servers])

#core-0
task_para=(98 40 10 $1 "MP" 0 9000)
echo ${task_para[*]} > /tmp/taskinfo
mplayer -hardframedrop -benchmark movie1.mov &

#core-1
task_para=(98 40 10 $1 "FF" 1 9000)
echo ${task_para[*]} > /tmp/ffmpeg_rt 
rm -f taken_tmp.yuv
ffmpeg -i movie2.mp4 -vcodec rawvideo movie2_tmp.yuv &	
