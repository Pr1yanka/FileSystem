obj-m := hgfs.o
ccflags-y := -DHGFS_DEBUG

all: ko hgfs_mkfs
ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkfs-hgfs_SOURCES:
	hgfs_mkfs.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
