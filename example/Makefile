KERNELDIR ?= /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

all: modules

obj-m := hello.o

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $(KERNEL_VERBOSE) $(KERNEL_MAKE_ARGS) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $(KERNEL_VERBOSE) $(KERNEL_MAKE_ARGS) clean
