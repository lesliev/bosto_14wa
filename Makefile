

.PHONY: all clean archive

obj-m += bosto_14wa.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=/home/leslie/dev/git/external/tablet/bosto-les modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=/home/leslie/dev/git/external/tablet/bosto-les clean

archive:
	tar f - --exclude=.git -C ../ -c bosto_14wa | gzip -c9 > ../bosto_14wa-`date +%Y%m%d`.tgz

install:
	cp ./bosto_14wa.ko /lib/modules/$(shell uname -r)
	echo bosto_14wa >> /etc/modules
	depmod
	cp ./scripts/insert_bosto_14wa /usr/local/bin
	cp ./scripts/bosto_14wa.rules /etc/udev/rules.d
	/sbin/udevadm control --reload
	modprobe bosto_14wa

uninstall:
	rm -f /usr/local/bin/insert_bosto_14wa
	rm -f /etc/udev/rules.d/bosto_14wa.rules
	/sbin/udevadm control --reload
	rm -f /lib/modules/$(shell uname -r)/bosto_14wa.ko
	sed -i '/bosto_14wa/d' /etc/modules
	depmod
	rmmod bosto_14wa
