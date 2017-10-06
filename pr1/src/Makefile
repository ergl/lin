obj-m += modlist.o
EXTRA_CFLAFS = -Wall -Wextra -Wshadow

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
