ifneq ($(KERNELRELEASE),)

TARGET_MODULE := shannon_hot_patch_1st
$(TARGET_MODULE)-objs := hot_patch.o
obj-m := $(TARGET_MODULE).o

else

KERNELVER ?= $(shell uname -r)
KERNEL_SRC = /lib/modules/$(KERNELVER)/build
PWD=$(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

endif