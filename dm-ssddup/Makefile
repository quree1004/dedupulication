KERNELDIR=/lib/modules/$(shell uname -r)/build

dm-ssddup-y := dm-ssddup-target.o dm-ssddup-hash.o dm-ssddup-meta.o
obj-m=dm-ssddup.o


KDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules 

clean:
	rm -rf *.ko
	rm -rf *.mod.*
	rm -rf .*.cmd
	rm -rf *.o
