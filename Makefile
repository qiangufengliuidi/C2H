KERNELVER ?= $(shell uname -r)
KERNEL_SRC = /lib/modules/$(KERNELVER)/build
PWD=$(shell pwd)

TARGET_MODULE := hot_patch
obj-m := $(TARGET_MODULE).o

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
load:
	insmod $(TARGET_MODULE).ko
unload:
	rmmod $(TARGET_MODULE).ko
