obj-m +=page_buffer_cache.o


PWD:=$(CURDIR)
all:
	make -C  /lib/modules/3.10.0-1160.108.1.el7.x86_64/build  M=$(PWD) modules

clean:
	make -C  /lib/modules/3.10.0-1160.108.1.el7.x86_64/build M=$(PWD) clean
