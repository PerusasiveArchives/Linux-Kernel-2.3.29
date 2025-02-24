#
# Makefile for the linux arm-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET	:= mm.o
O_OBJS		:= init.o extable.o fault-$(PROCESSOR).o mm-$(PROCESSOR).o \
		   small_page.o

ifeq ($(CONFIG_CPU_26),y)
  O_OBJS	+= proc-arm2,3.o
endif

ifeq ($(CONFIG_CPU_32),y)
  ifeq ($(CONFIG_CPU_ARM6),y)
    P_OBJS	+= proc-arm6,7.o
  endif
  ifeq ($(CONFIG_CPU_ARM7),y)
    P_OBJS	+= proc-arm6,7.o
  endif
  ifeq ($(CONFIG_CPU_SA110),y)
    P_OBJS	+= proc-sa110.o
  endif
  ifeq ($(CONFIG_CPU_SA1100),y)
    P_OBJS	+= proc-sa110.o
  endif
  O_OBJS	+= mm-$(MACHINE).o ioremap.o $(sort $(P_OBJS))
endif

include $(TOPDIR)/Rules.make

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) $(AFLAGS_$@) -traditional -c -o $*.o $<

# Special dependencies
fault-armv.o: fault-common.c
fault-armo.o: fault-common.c
proc-arm2,3.o:	../lib/constants.h
proc-arm6,7.o:	../lib/constants.h
proc-sa110.o:	../lib/constants.h

