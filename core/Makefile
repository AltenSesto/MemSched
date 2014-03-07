
# If KERNELRELEASE is define, we have been invoked from the
# kernel build system and can use its languages.
ifneq ($(KERNELRELEASE),)
	obj-m := resch.o

# Otherwise we were called directly from the command line;
# invoke the kernel build system
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD	:= $(shell pwd)

.PHONY:default
default: config.h
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

.PHONY:test
test:
	$(MAKE) -C $@
	@./test/test_overhead

.PHONY:distclean
distclean: clean
.PHONY:clean
clean:
	@rm -f *.mod.* *.ko *.o .resch* modules.order Module.* config.h *~
	@rm -fr ./.tmp*

.PHONY:install
install:
	@./script/install

.PHONY:uninstall
uninstall:
	@./script/uninstall

endif
