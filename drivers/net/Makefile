# File: drivers/net/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#

O_OBJS          :=
M_OBJS          :=
MX_OBJS         :=

# Object file lists.

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

SUB_DIRS     := 
MOD_SUB_DIRS :=
MOD_IN_SUB_DIRS :=
ALL_SUB_DIRS := $(SUB_DIRS) fc hamradio irda pcmcia tokenring wan sk98lin

O_TARGET := net.o
O_OBJS   :=
M_OBJS   :=
MOD_LIST_NAME := NET_MODULES

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=	8390.o arcnet.o arlan.o ppp_async.o \
			ppp_generic.o slhc.o

ifeq ($(CONFIG_PCMCIA),y)
  SUB_DIRS += pcmcia
  MOD_IN_SUB_DIRS += pcmcia
else
  ifeq ($(CONFIG_PCMCIA),m)
    MOD_IN_SUB_DIRS += pcmcia
  endif
endif

ifeq ($(CONFIG_IRDA),y)
SUB_DIRS += irda
MOD_IN_SUB_DIRS += irda
else
  ifeq ($(CONFIG_IRDA),m)
  MOD_IN_SUB_DIRS += irda
  endif
endif

ifeq ($(CONFIG_SK98LIN),y)
SUB_DIRS += sk98lin
MOD_IN_SUB_DIRS += sk98lin
L_OBJS += sk98lin/sk98lin.o
else
  ifeq ($(CONFIG_SK98LIN),m)
  MOD_IN_SUB_DIRS += sk98lin
  endif
endif

ifeq ($(CONFIG_TR),y)
SUB_DIRS += tokenring
MOD_IN_SUB_DIRS += tokenring
else
  ifeq ($(CONFIG_TR),m)
  MOD_IN_SUB_DIRS += tokenring
  endif
endif

ifeq ($(CONFIG_WAN),y)
SUB_DIRS += wan
MOD_IN_SUB_DIRS += wan
else
  ifeq ($(CONFIG_WAN),m)
  MOD_IN_SUB_DIRS += wan
  endif
endif

ifeq ($(CONFIG_NET_FC),y)
SUB_DIRS += fc
MOD_IN_SUB_DIRS += fc
else
  ifeq ($(CONFIG_NET_FC),m)
  MOD_IN_SUB_DIRS += fc
  endif
endif

ifeq ($(CONFIG_ISDN),y)
  ifeq ($(CONFIG_ISDN_PPP),y)
    obj-y += slhc.o ppp_deflate.o
  endif
else
  ifeq ($(CONFIG_ISDN),m)
    ifeq ($(CONFIG_ISDN_PPP),y)
      obj-m += slhc.o ppp_deflate.o
    endif
  endif
endif

obj-$(CONFIG_NET) += Space.o setup.o net_init.o loopback.o
obj-$(CONFIG_SEEQ8005) += seeq8005.o
obj-$(CONFIG_ETHERTAP) += ethertap.o
obj-$(CONFIG_NET_SB1000) += sb1000.o
obj-$(CONFIG_DAYNAPORT) += daynaport.o 8390.o
obj-$(CONFIG_APNE) += apne.o 8390.o
obj-$(CONFIG_PCMCIA_PCNET) += 8390.o
obj-$(CONFIG_SHAPER) += shaper.o
obj-$(CONFIG_SK_G16) += sk_g16.o
obj-$(CONFIG_HP100) += hp100.o
obj-$(CONFIG_SMC9194) += smc9194.o
obj-$(CONFIG_ARM_AM79C961A) += am79c961a.o
obj-$(CONFIG_ARM_ETHERH) += 8390.o
obj-$(CONFIG_WD80x3) += wd.o 8390.o
obj-$(CONFIG_EL2) += 3c503.o 8390.o
obj-$(CONFIG_NE2K_PCI) += ne2k-pci.o 8390.o
obj-$(CONFIG_NE2000) += ne.o 8390.o
obj-$(CONFIG_NE2_MCA) += ne2.o 8390.o
obj-$(CONFIG_HPLAN) += hp.o 8390.o
obj-$(CONFIG_HPLAN_PLUS) += hp-plus.o 8390.o
obj-$(CONFIG_ULTRA) += smc-ultra.o 8390.o
obj-$(CONFIG_ULTRAMCA) += smc-mca.o 8390.o
obj-$(CONFIG_ULTRA32) += smc-ultra32.o 8390.o
obj-$(CONFIG_E2100) += e2100.o 8390.o
obj-$(CONFIG_ES3210) += es3210.o 8390.o
obj-$(CONFIG_LNE390) += lne390.o 8390.o
obj-$(CONFIG_NE3210) += ne3210.o 8390.o
obj-$(CONFIG_PLIP) += plip.o

# bsd_comp.o is *always* a module, for some documented reason
# (licensing).
ifeq ($(CONFIG_PPP),y)
  obj-y += ppp_generic.o slhc.o
  obj-$(CONFIG_PPP_ASYNC) += ppp_async.o
  obj-$(CONFIG_PPP_SYNC_TTY) += ppp_synctty.o
  obj-$(CONFIG_PPP_DEFLATE) += ppp_deflate.o
  ifeq ($(CONFIG_PPP_BSDCOMP),m)
    obj-m += bsd_comp.o
  endif
else
  ifeq ($(CONFIG_PPP),m)
    obj-m += ppp_generic.o slhc.o
    obj-$(CONFIG_PPP_ASYNC) += ppp_async.o
    obj-$(CONFIG_PPP_SYNC_TTY) += ppp_synctty.o
    obj-$(CONFIG_PPP_DEFLATE) += ppp_deflate.o
    ifeq ($(CONFIG_PPP_BSDCOMP),m)
      obj-m += bsd_comp.o
    endif
  endif
endif

obj-$(CONFIG_SLIP) += slip.o
ifeq ($(CONFIG_SLIP),y)
  obj-$(CONFIG_SLIP_COMPRESSED) += slhc.o
else
  ifeq ($(CONFIG_SLIP),m)
    obj-$(CONFIG_SLIP_COMPRESSED) += slhc.o
  endif
endif

obj-$(CONFIG_STRIP) += strip.o
obj-$(CONFIG_DE650) += de650.o 8390.o
obj-$(CONFIG_3C589) += 3c589.o
obj-$(CONFIG_DUMMY) += dummy.o
obj-$(CONFIG_DE600) += de600.o
obj-$(CONFIG_DE620) += de620.o
obj-$(CONFIG_AT1500) += lance.o
obj-$(CONFIG_LANCE) += lance.o
obj-$(CONFIG_SUN3LANCE) += sun3lance.o
obj-$(CONFIG_PCNET32) += pcnet32.o
obj-$(CONFIG_DEFXX) += defxx.o
obj-$(CONFIG_SUNLANCE) += sunlance.o
obj-$(CONFIG_SGISEEQ) += sgiseeq.o
obj-$(CONFIG_HAPPYMEAL) += sunhme.o
obj-$(CONFIG_SUNQE) += sunqe.o
obj-$(CONFIG_SUNBMAC) += sunbmac.o
obj-$(CONFIG_SUNBMAC) += sunbmac.o
obj-$(CONFIG_MYRI_SBUS) += myri_sbus.o
obj-$(CONFIG_AT1700) += at1700.o
obj-$(CONFIG_FMV18X) += fmv18x.o
obj-$(CONFIG_EL1) += 3c501.o
obj-$(CONFIG_EL16) += 3c507.o
obj-$(CONFIG_ELMC) += 3c523.o
obj-$(CONFIG_SKMC) += sk_mca.o
obj-$(CONFIG_ELMC_II) += 3c527.o
obj-$(CONFIG_EL3) += 3c509.o
obj-$(CONFIG_3C515) += 3c515.o
obj-$(CONFIG_VORTEX) += 3c59x.o
obj-$(CONFIG_EEXPRESS) += eexpress.o
obj-$(CONFIG_EEXPRESS_PRO) += eepro.o
obj-$(CONFIG_EEXPRESS_PRO100) += eepro100.o
obj-$(CONFIG_RTL8139) += rtl8139.o
obj-$(CONFIG_SIS900) += sis900.o
obj-$(CONFIG_DM9102) += dmfe.o
obj-$(CONFIG_YELLOWFIN) += yellowfin.o
obj-$(CONFIG_ACENIC) += acenic.o
obj-$(CONFIG_WAVELAN) += wavelan.o
obj-$(CONFIG_ARLAN) += arlan.o arlan-proc.o
obj-$(CONFIG_TLAN) += tlan.o
obj-$(CONFIG_VIA_RHINE) += via-rhine.o
obj-$(CONFIG_ZNET) += znet.o
obj-$(CONFIG_DEPCA) += depca.o
obj-$(CONFIG_EWRK3) += ewrk3.o
obj-$(CONFIG_ATP) += atp.o
obj-$(CONFIG_DE4X5) += de4x5.o
obj-$(CONFIG_NI5010) += ni5010.o
obj-$(CONFIG_NI52) += ni52.o
obj-$(CONFIG_NI65) += ni65.o
obj-$(CONFIG_ELPLUS) += 3c505.o
obj-$(CONFIG_AC3200) += ac3200.o 8390.o
obj-$(CONFIG_APRICOT) += 82596.o
obj-$(CONFIG_MVME16x_NET) += 82596.o
obj-$(CONFIG_BVME6000_NET) += 82596.o
obj-$(CONFIG_DEC_ELCP) += tulip.o
obj-$(CONFIG_ARCNET) += arcnet.o
obj-$(CONFIG_ARCNET_COM90xx) += com90xx.o
obj-$(CONFIG_ARCNET_COM90xxIO) += com90io.o
obj-$(CONFIG_ARCNET_RIM_I) += arc-rimi.o
obj-$(CONFIG_ARCNET_COM20020) += com20020.o
obj-$(CONFIG_ETH16I) += eth16i.o
obj-$(CONFIG_EPIC100) += epic100.o
obj-$(CONFIG_ARIADNE2) += ariadne2.o 8390.o
obj-$(CONFIG_HPLANCE) += hplance.o 7990.o
obj-$(CONFIG_MVME147_NET) += mvme147.o 7990.o
obj-$(CONFIG_EQUALIZER) += eql.o
obj-$(CONFIG_MIPS_JAZZ_SONIC) += jazzsonic.o
obj-$(CONFIG_BAGETLANCE) += bagetlance.o
obj-$(CONFIG_DECLANCE) += declance.o
obj-$(CONFIG_ATARILANCE) += atarilance.o
obj-$(CONFIG_ATARI_BIONET) += atari_bionet.o
obj-$(CONFIG_ATARI_PAMSNET) += atari_pamsnet.o
obj-$(CONFIG_A2065) += a2065.o
obj-$(CONFIG_HYDRA) += hydra.o
obj-$(CONFIG_ARIADNE) += ariadne.o
obj-$(CONFIG_DGRS) += dgrs.o
obj-$(CONFIG_CS89x0) += cs89x0.o
obj-$(CONFIG_LTPC) += ltpc.o
obj-$(CONFIG_COPS) += cops.o
obj-$(CONFIG_IPDDP) += ipddp.o
obj-$(CONFIG_RCPCI) += rcpci.o
obj-$(CONFIG_MACE) += mace.o
obj-$(CONFIG_MACSONIC) += macsonic.o
obj-$(CONFIG_BMAC) += bmac.o
obj-$(CONFIG_NCR885E) += ncr885e.o
obj-$(CONFIG_ADAPTEC_STARFIRE) += starfire.o

#
# HIPPI adapters
#

obj-$(CONFIG_ROADRUNNER) += rrunner.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m           := $(filter-out $(obj-y), $(obj-m))
int-m           := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y           := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS          := $(filter-out $(export-objs), $(obj-y))
OX_OBJS         := $(filter     $(export-objs), $(obj-y))
M_OBJS          := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS         := $(sort $(filter     $(export-objs), $(obj-m)))

O_OBJS += auto_irq.o

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

rcpci.o: rcpci45.o rclanmtl.o
	$(LD) -r -o rcpci.o rcpci45.o rclanmtl.o

