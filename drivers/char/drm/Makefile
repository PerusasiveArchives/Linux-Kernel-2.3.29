#
# Makefile for the drm device driver.  This driver provides support for
# the Direct Rendering Infrastructure (DRI) in XFree86 4.x.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

L_TARGET        := libdrm.a

L_OBJS		:= init.o memory.o proc.o auth.o context.o drawable.o bufs.o \
			lists.o lock.o ioctl.o fops.o vm.o dma.o

M_OBJS		:= 

ifdef CONFIG_DRM_GAMMA
M_OBJS          += gamma.o
endif

include $(TOPDIR)/Rules.make

gamma.o: gamma_drv.o gamma_dma.o $(L_TARGET)
	$(LD) $(LD_RFLAG) -r -o $@ gamma_drv.o gamma_dma.o -L. -ldrm
