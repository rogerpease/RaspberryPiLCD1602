
obj-m += lcd1602DirectDriver.o 


KERNEL_DIR ?= $(HOME)/RaspberryPi/linux

all:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) clean

deploy:
	scp *.ko root@raspberrypi:



