# Copyright (c) 2014 MDH.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html

lib='/usr/lib/libresch.a'

all: 
	@gcc -o normal_task normal_task.c $(lib)
	@gcc -o problem_task problem_task.c $(lib)
	@gcc -o test_task test_task.c -lrt $(lib)
clean:
	rm -f normal_task problem_task test_task *~
