# Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
# All rights reserved. This program and the accompanying materials are 
# made available under the terms of the GNU Public License v3.0 which 
# accompanies this distribution, and is available at 
# http://www.gnu.org/licenses/gpl.htm

TARGET	:= libresch
#INCDIR	:= /usr/src/kernels/$(shell uname --release)/include

$(TARGET).o:
	@gcc -fpic -o $(TARGET).o -c $(TARGET).c
	@ar -rcs $(TARGET).a $(TARGET).o

clean:
	@rm -f $(TARGET).o $(TARGET).a *~
