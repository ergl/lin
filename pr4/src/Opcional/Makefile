obj-m += fifodev.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	insmod fifodev.ko
	./make_fifo.sh

uninstall:
	rmmod fifodev
	rm -rf /dev/fifodev
