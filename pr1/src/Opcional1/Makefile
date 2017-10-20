obj-m += modlist.o

all:
	make CFLAGS="-DCHARLIST" -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
