#
# arch/ppc/mbxboot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS)  -DINITRD_OFFSET=$(IOFF) -DINITRD_SIZE=$(ISZ) -DZIMAGE_OFFSET=$(ZOFF) -DZIMAGE_SIZE=$(ZSZ) -c -o $*.o $<
.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

ZOFF = 0
ZSZ = 0
IOFF = 0
ISZ = 0

TFTPIMAGE=/tftpboot/zImage.mbx
ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00100000
GZIP_FLAGS = -v9

OBJECTS := head.o misc.o ../coffboot/zlib.o m8xx_tty.o
CFLAGS = -O2 -DSTDC_HEADERS -fno-builtin -I$(TOPDIR)/include -DCONFIG_8xx
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O elf32-powerpc

ifeq ($(CONFIG_MBX),y)
OBJECTS += pci.o qspan_pci.o
CFLAGS += -DCONFIG_MBX
endif
ifeq ($(CONFIG_RPXLITE),y)
CFLAGS += -DCONFIG_RPXLITE
OBJECTS += iic.o embed_config.o
endif
ifeq ($(CONFIG_RPXCLASSIC),y)
CFLAGS += -DCONFIG_RPXCLASSIC
OBJECTS += iic.o embed_config.o pci.o qspan_pci.o
endif
ifeq ($(CONFIG_BSEIP),y)
CFLAGS += -DCONFIG_BSEIP
OBJECTS += iic.o embed_config.o
endif

all:	zImage

zvmlinux.initrd: zvmlinux
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp1 $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp1 zvmlinux.initrd1
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd1 initrd` \
		-DINITRD_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd1 initrd` \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd1 image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd1 image` \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp $@

zImage: zvmlinux
	ln -sf zvmlinux zImage

zImage.initrd: zvmlinux.initrd
	ln -sf zvmlinux.initrd zImage.initrd

zvmlinux: $(OBJECTS) ../coffboot/vmlinux.gz
#
# build the boot loader image and then compute the offset into it
# for the kernel image
#
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
#
# then with the offset rebuild the bootloader so we know where the kernel is
#
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux image` \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
	rm zvmlinux.tmp

znetboot : zImage
	cp zImage $(TFTPIMAGE)

znetboot.initrd : zImage.initrd
	cp zImage.initrd $(TFTPIMAGE)

clean:
	rm -f vmlinux* zvmlinux* zImage*

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:
