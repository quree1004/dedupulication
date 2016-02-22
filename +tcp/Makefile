.PHONY: default clean
SRCS=sim_dedup.c ksocket.c
OBJS=$(SRCS:.c=.o)

obj-m+=$(OBJS)

KDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)
EXTRA_CFLAGS=-Wall

EXTRA_LDFLAGS:=-I/lib/modules/$(shell uname -r)/build/

ifeq ($(ADDRSAFE),true)
 	EXTRA_CFLAGS+=-DKSOCKET_ADDR_SAFE
endif

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules 

clean:
	rm -rf *.ko
	rm -rf *.mod.*
	rm -rf .*.cmd
	rm -rf *.o
