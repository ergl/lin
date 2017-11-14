obj-m += blinkdrv.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

prebuild:
	rmmod usbhid
	modprobe usbhid quirks=0x20A0:0x41E5:0x0004
