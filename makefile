obj-m += lkmasg2_input.o lkmasg2_output.o
ccflags-y += -I$(PWD)/include

KDIR ?= /usr/src/linux-headers-$(shell uname -r)


all:
	make -C $(KDIR) M=$(PWD) modules


clean:
	make -C $(KDIR) M=$(PWD) clean


test: lkmasg2_input.o lkmasg2_output.o
	python3 test.c


lkmasg2_input.o: lkmasg2_input.c
	make -C $(KDIR) M=$(PWD) modules


lkmasg2_output.o: lkmasg2_output.c
	make -C $(KDIR) M=$(PWD) modules
