# Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
# All rights reserved. This program and the accompanying materials are 
# made available under the terms of the GNU Public License v3.0 which 
# accompanies this distribution, and is available at 
# http://www.gnu.org/licenses/gpl.htm

LIB	:= ../../library/libresch.a
CC	:= gcc

default:
	@$(CC) -o schedbench -lm schedbench.c exec.c taskset.c $(LIB)

clean:
	@rm -f  schedbench config.h .error.log .util result *~
distclean:
	@rm -f  schedbench config.h .error.log .util result *~
	@rm -fr taskset
