obj-m += multilist.o
multilist-objs := modlist.o modmain.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	insmod multilist.ko

installbig:
	insmod multilist.ko max_entries=90 max_size=500

uninstall:
	rmmod multilist
