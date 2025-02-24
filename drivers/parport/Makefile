#
# Makefile for the kernel miscellaneous drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# Note 3! Parport is the Borg.  We have assimilated some other
# drivers in the `char', `net' and `scsi' directories, but left them
# there to allay suspicion.

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := parport.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_PARPORT),y)
  L_OBJS += share.o ieee1284.o ieee1284_ops.o procfs.o

  ifeq ($(CONFIG_PARPORT_1284),y)
    L_OBJS += daisy.o probe.o
  endif

  ifeq ($(CONFIG_PARPORT_PC),y)
    LX_OBJS += parport_pc.o
  else
    ifeq ($(CONFIG_PARPORT_PC),m)
      MX_OBJS += parport_pc.o
    endif
  endif
  ifeq ($(CONFIG_PARPORT_AMIGA),y)
    LX_OBJS += parport_amiga.o
  else
    ifeq ($(CONFIG_PARPORT_AMIGA),m)
      M_OBJS += parport_amiga.o
    endif
  endif
  ifeq ($(CONFIG_PARPORT_MFC3),y)
    LX_OBJS += parport_mfc3.o
  else
    ifeq ($(CONFIG_PARPORT_MFC3),m)
      M_OBJS += parport_mfc3.o
    endif
  endif
  ifeq ($(CONFIG_PARPORT_ATARI),y)
    LX_OBJS += parport_atari.o
  else
    ifeq ($(CONFIG_PARPORT_ATARI),m)
      M_OBJS += parport_atari.o
    endif
  endif
  ifeq ($(CONFIG_PARPORT_SUNBPP),y)
    LX_OBJS += parport_sunbpp.o
  else
    ifeq ($(CONFIG_PARPORT_SUNBPP),m)
      MX_OBJS += parport_sunbpp.o
    endif
  endif
  LX_OBJS += init.o
else
  ifeq ($(CONFIG_PARPORT),m)
    MI_OBJS += share.o ieee1284.o ieee1284_ops.o
    ifeq ($(CONFIG_PARPORT_1284),y)
      MI_OBJS += daisy.o probe.o
    endif
    ifneq ($(CONFIG_PROC_FS),n) 
      MI_OBJS += procfs.o
    endif
    MIX_OBJS += init.o
    M_OBJS += parport.o
  endif
  ifeq ($(CONFIG_PARPORT_PC),m)
    MX_OBJS += parport_pc.o
  endif
  ifeq ($(CONFIG_PARPORT_AMIGA),m)
    M_OBJS += parport_amiga.o
  endif
  ifeq ($(CONFIG_PARPORT_MFC3),m)
    M_OBJS += parport_mfc3.o
  endif
  ifeq ($(CONFIG_PARPORT_ATARI),m)
    M_OBJS += parport_atari.o
  endif
  ifeq ($(CONFIG_PARPORT_SUNBPP),m)
    M_OBJS += parport_sunbpp.o
  endif
endif

include $(TOPDIR)/Rules.make

# Special rule to build the composite parport.o module
parport.o: $(MI_OBJS) $(MIX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(MI_OBJS) $(MIX_OBJS)
