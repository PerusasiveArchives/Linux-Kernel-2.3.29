/*
 *	WaveLAN ISA driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 * Original copyright follows (also see the end of this file).
 * See wavelan.p.h for details.
 */

/*
 * AT&T GIS (nee NCR) WaveLAN card:
 *	An Ethernet-like radio transceiver
 *	controlled by an Intel 82586 coprocessor.
 */

#include "wavelan.p.h"		/* Private header */

/************************* MISC SUBROUTINES **************************/
/*
 * Subroutines which won't fit in one of the following category
 * (WaveLAN modem or i82586)
 */

/*------------------------------------------------------------------*/
/*
 * Wrapper for disabling interrupts.
 */
static inline unsigned long
wv_splhi(void)
{
  unsigned long flags;

  save_flags(flags);
  cli();

  return(flags);
}

/*------------------------------------------------------------------*/
/*
 * Wrapper for re-enabling interrupts.
 */
static inline void
wv_splx(unsigned long	flags)
{
  restore_flags(flags);
}

/*------------------------------------------------------------------*/
/*
 * Translate irq number to PSA irq parameter
 */
static u_char
wv_irq_to_psa(int	irq)
{
  if(irq < 0 || irq >= NELS(irqvals))
    return 0;

  return irqvals[irq];
}

/*------------------------------------------------------------------*/
/*
 * Translate PSA irq parameter to irq number 
 */
static int __init 
wv_psa_to_irq(u_char irqval)
{
  int	irq;

  for(irq = 0; irq < NELS(irqvals); irq++)
    if(irqvals[irq] == irqval)
      return irq;

  return -1;
}

#ifdef STRUCT_CHECK
/*------------------------------------------------------------------*/
/*
 * Sanity routine to verify the sizes of the various WaveLAN interface
 * structures.
 */
static char *
wv_struct_check(void)
{
#define	SC(t,s,n)	if (sizeof(t) != s) return(n);

  SC(psa_t, PSA_SIZE, "psa_t");
  SC(mmw_t, MMW_SIZE, "mmw_t");
  SC(mmr_t, MMR_SIZE, "mmr_t");
  SC(ha_t, HA_SIZE, "ha_t");

#undef	SC

  return((char *) NULL);
} /* wv_struct_check */
#endif	/* STRUCT_CHECK */

/********************* HOST ADAPTER SUBROUTINES *********************/
/*
 * Useful subroutines to manage the WaveLAN ISA interface
 *
 * One major difference with the PCMCIA hardware (except the port mapping)
 * is that we have to keep the state of the Host Control Register
 * because of the interrupt enable & bus size flags.
 */

/*------------------------------------------------------------------*/
/*
 * Read from card's Host Adaptor Status Register.
 */
static inline u_short
hasr_read(u_long	ioaddr)
{
  return(inw(HASR(ioaddr)));
} /* hasr_read */

/*------------------------------------------------------------------*/
/*
 * Write to card's Host Adapter Command Register.
 */
static inline void
hacr_write(u_long	ioaddr,
	   u_short	hacr)
{
  outw(hacr, HACR(ioaddr));
} /* hacr_write */

/*------------------------------------------------------------------*/
/*
 * Write to card's Host Adapter Command Register. Include a delay for
 * those times when it is needed.
 */
static inline void
hacr_write_slow(u_long	ioaddr,
		u_short	hacr)
{
  hacr_write(ioaddr, hacr);
  /* delay might only be needed sometimes */
  mdelay(1);
} /* hacr_write_slow */

/*------------------------------------------------------------------*/
/*
 * Set the channel attention bit.
 */
static inline void
set_chan_attn(u_long	ioaddr,
	      u_short	hacr)
{
  hacr_write(ioaddr, hacr | HACR_CA);
} /* set_chan_attn */

/*------------------------------------------------------------------*/
/*
 * Reset, and then set host adaptor into default mode.
 */
static inline void
wv_hacr_reset(u_long	ioaddr)
{
  hacr_write_slow(ioaddr, HACR_RESET);
  hacr_write(ioaddr, HACR_DEFAULT);
} /* wv_hacr_reset */

/*------------------------------------------------------------------*/
/*
 * Set the I/O transfer over the ISA bus to 8-bit mode
 */
static inline void
wv_16_off(u_long	ioaddr,
	  u_short	hacr)
{
  hacr &= ~HACR_16BITS;
  hacr_write(ioaddr, hacr);
} /* wv_16_off */

/*------------------------------------------------------------------*/
/*
 * Set the I/O transfer over the ISA bus to 8-bit mode
 */
static inline void
wv_16_on(u_long		ioaddr,
	 u_short	hacr)
{
  hacr |= HACR_16BITS;
  hacr_write(ioaddr, hacr);
} /* wv_16_on */

/*------------------------------------------------------------------*/
/*
 * Disable interrupts on the WaveLAN hardware.
 */
static inline void
wv_ints_off(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_long	x;

  x = wv_splhi();

  lp->hacr &= ~HACR_INTRON;
  hacr_write(ioaddr, lp->hacr);

  wv_splx(x);
} /* wv_ints_off */

/*------------------------------------------------------------------*/
/*
 * Enable interrupts on the WaveLAN hardware.
 */
static inline void
wv_ints_on(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_long	x;

  x = wv_splhi();

  lp->hacr |= HACR_INTRON;
  hacr_write(ioaddr, lp->hacr);

  wv_splx(x);
} /* wv_ints_on */

/******************* MODEM MANAGEMENT SUBROUTINES *******************/
/*
 * Useful subroutines to manage the modem of the WaveLAN
 */

/*------------------------------------------------------------------*/
/*
 * Read the Parameter Storage Area from the WaveLAN card's memory
 */
/*
 * Read bytes from the PSA.
 */
static void
psa_read(u_long		ioaddr,
	 u_short	hacr,
	 int		o,	/* offset in PSA */
	 u_char *	b,	/* buffer to fill */
	 int		n)	/* size to read */
{
  wv_16_off(ioaddr, hacr);

  while(n-- > 0)
    {
      outw(o, PIOR2(ioaddr));
      o++;
      *b++ = inb(PIOP2(ioaddr));
    }

  wv_16_on(ioaddr, hacr);
} /* psa_read */

/*------------------------------------------------------------------*/
/*
 * Write the Parameter Storage Area to the WaveLAN card's memory.
 */
static void
psa_write(u_long	ioaddr,
	  u_short	hacr,
	  int		o,	/* Offset in PSA */
	  u_char *	b,	/* Buffer in memory */
	  int		n)	/* Length of buffer */
{
  int	count = 0;

  wv_16_off(ioaddr, hacr);

  while(n-- > 0)
    {
      outw(o, PIOR2(ioaddr));
      o++;

      outb(*b, PIOP2(ioaddr));
      b++;

      /* Wait for the memory to finish its write cycle */
      count = 0;
      while((count++ < 100) &&
	    (hasr_read(ioaddr) & HASR_PSA_BUSY))
	mdelay(1);
    }

  wv_16_on(ioaddr, hacr);
} /* psa_write */

#ifdef SET_PSA_CRC
/*------------------------------------------------------------------*/
/*
 * Calculate the PSA CRC
 * Thanks to Valster, Nico <NVALSTER@wcnd.nl.lucent.com> for the code
 * NOTE: By specifying a length including the CRC position the
 * returned value should be zero. (i.e. a correct checksum in the PSA)
 *
 * The Windows drivers don't use the CRC, but the AP and the PtP tool
 * depend on it.
 */
static inline u_short
psa_crc(u_char *	psa,	/* The PSA */
	int		size)	/* Number of short for CRC */
{
  int		byte_cnt;	/* Loop on the PSA */
  u_short	crc_bytes = 0;	/* Data in the PSA */
  int		bit_cnt;	/* Loop on the bits of the short */

  for(byte_cnt = 0; byte_cnt < size; byte_cnt++ )
    {
      crc_bytes ^= psa[byte_cnt];	/* Its an xor */

      for(bit_cnt = 1; bit_cnt < 9; bit_cnt++ )
	{
	  if(crc_bytes & 0x0001)
	    crc_bytes = (crc_bytes >> 1) ^ 0xA001;
	  else
	    crc_bytes >>= 1 ;
        }
    }

  return crc_bytes;
} /* psa_crc */
#endif	/* SET_PSA_CRC */

/*------------------------------------------------------------------*/
/*
 * update the checksum field in the Wavelan's PSA
 */
static void
update_psa_checksum(device *	dev,
		    u_long	ioaddr,
		    u_short	hacr)
{
#ifdef SET_PSA_CRC
  psa_t		psa;
  u_short	crc;

  /* read the parameter storage area */
  psa_read(ioaddr, hacr, 0, (unsigned char *) &psa, sizeof(psa));

  /* update the checksum */
  crc = psa_crc((unsigned char *) &psa,
		sizeof(psa) - sizeof(psa.psa_crc[0]) - sizeof(psa.psa_crc[1])
		- sizeof(psa.psa_crc_status));

  psa.psa_crc[0] = crc & 0xFF;
  psa.psa_crc[1] = (crc & 0xFF00) >> 8;

  /* Write it ! */
  psa_write(ioaddr, hacr, (char *)&psa.psa_crc - (char *)&psa,
	    (unsigned char *)&psa.psa_crc, 2);

#ifdef DEBUG_IOCTL_INFO
  printk (KERN_DEBUG "%s: update_psa_checksum(): crc = 0x%02x%02x\n",
          dev->name, psa.psa_crc[0], psa.psa_crc[1]);

  /* Check again (luxury !) */
  crc = psa_crc ((unsigned char *) &psa,
		 sizeof(psa) - sizeof(psa.psa_crc_status));

  if(crc != 0)
    printk(KERN_WARNING "%s: update_psa_checksum(): CRC does not agree with PSA data (even after recalculating)\n", dev->name);
#endif /* DEBUG_IOCTL_INFO */
#endif	/* SET_PSA_CRC */
} /* update_psa_checksum */

/*------------------------------------------------------------------*/
/*
 * Write 1 byte to the MMC.
 */
static inline void
mmc_out(u_long		ioaddr,
	u_short		o,
	u_char		d)
{
  /* Wait for MMC to go idle */
  while(inw(HASR(ioaddr)) & HASR_MMC_BUSY)
    ;

  outw((u_short) (((u_short) d << 8) | (o << 1) | 1),
       MMCR(ioaddr));
}

/*------------------------------------------------------------------*/
/*
 * Routine to write bytes to the Modem Management Controller.
 * We start at the end because it is the way it should be!
 */
static inline void
mmc_write(u_long	ioaddr,
	  u_char	o,
	  u_char *	b,
	  int		n)
{
  o += n;
  b += n;

  while(n-- > 0 )
    mmc_out(ioaddr, --o, *(--b));
} /* mmc_write */

/*------------------------------------------------------------------*/
/*
 * Read a byte from the MMC.
 * Optimised version for 1 byte, avoid using memory.
 */
static inline u_char
mmc_in(u_long	ioaddr,
       u_short	o)
{
  while(inw(HASR(ioaddr)) & HASR_MMC_BUSY)
    ;
  outw(o << 1, MMCR(ioaddr));

  while(inw(HASR(ioaddr)) & HASR_MMC_BUSY)
    ;
  return (u_char) (inw(MMCR(ioaddr)) >> 8);
}

/*------------------------------------------------------------------*/
/*
 * Routine to read bytes from the Modem Management Controller.
 * The implementation is complicated by a lack of address lines,
 * which prevents decoding of the low-order bit.
 * (code has just been moved in the above function)
 * We start at the end because it is the way it should be!
 */
static inline void
mmc_read(u_long		ioaddr,
	 u_char		o,
	 u_char *	b,
	 int		n)
{
  o += n;
  b += n;

  while(n-- > 0)
    *(--b) = mmc_in(ioaddr, --o);
} /* mmc_read */

/*------------------------------------------------------------------*/
/*
 * Get the type of encryption available.
 */
static inline int
mmc_encr(u_long		ioaddr)	/* I/O port of the card */
{
  int	temp;

  temp = mmc_in(ioaddr, mmroff(0, mmr_des_avail));
  if((temp != MMR_DES_AVAIL_DES) && (temp != MMR_DES_AVAIL_AES))
    return 0;
  else
    return temp;
}

/*------------------------------------------------------------------*/
/*
 * Wait for the frequency EEPROM to complete a command.
 * I hope this one will be optimally inlined.
 */
static inline void
fee_wait(u_long		ioaddr,	/* I/O port of the card */
	 int		delay,	/* Base delay to wait for */
	 int		number)	/* Number of time to wait */
{
  int		count = 0;	/* Wait only a limited time */

  while((count++ < number) &&
	(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) & MMR_FEE_STATUS_BUSY))
    udelay(delay);
}

/*------------------------------------------------------------------*/
/*
 * Read bytes from the Frequency EEPROM (frequency select cards).
 */
static void
fee_read(u_long		ioaddr,	/* I/O port of the card */
	 u_short	o,	/* destination offset */
	 u_short *	b,	/* data buffer */
	 int		n)	/* number of registers */
{
  b += n;		/* Position at the end of the area */

  /* Write the address */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), o + n - 1);

  /* Loop on all buffer */
  while(n-- > 0)
    {
      /* Write the read command */
      mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_READ);

      /* Wait until EEPROM is ready (should be quick). */
      fee_wait(ioaddr, 10, 100);

      /* Read the value. */
      *--b = ((mmc_in(ioaddr, mmroff(0, mmr_fee_data_h)) << 8) |
	      mmc_in(ioaddr, mmroff(0, mmr_fee_data_l)));
    }
}

#ifdef WIRELESS_EXT	/* if the wireless extension exists in the kernel */

/*------------------------------------------------------------------*/
/*
 * Write bytes from the Frequency EEPROM (frequency select cards).
 * This is a bit complicated, because the frequency EEPROM has to
 * be unprotected and the write enabled.
 * Jean II
 */
static void
fee_write(u_long	ioaddr,	/* I/O port of the card */
	  u_short	o,	/* destination offset */
	  u_short *	b,	/* data buffer */
	  int		n)	/* number of registers */
{
  b += n;		/* Position at the end of the area. */

#ifdef EEPROM_IS_PROTECTED	/* disabled */
#ifdef DOESNT_SEEM_TO_WORK	/* disabled */
  /* Ask to read the protected register */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRREAD);

  fee_wait(ioaddr, 10, 100);

  /* Read the protected register. */
  printk("Protected 2:  %02X-%02X\n",
	 mmc_in(ioaddr, mmroff(0, mmr_fee_data_h)),
	 mmc_in(ioaddr, mmroff(0, mmr_fee_data_l)));
#endif	/* DOESNT_SEEM_TO_WORK */

  /* Enable protected register. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_EN);
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PREN);

  fee_wait(ioaddr, 10, 100);

  /* Unprotect area. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), o + n);
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRWRITE);
#ifdef DOESNT_SEEM_TO_WORK	/* disabled */
  /* or use: */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRCLEAR);
#endif	/* DOESNT_SEEM_TO_WORK */

  fee_wait(ioaddr, 10, 100);
#endif	/* EEPROM_IS_PROTECTED */

  /* Write enable. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_EN);
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WREN);

  fee_wait(ioaddr, 10, 100);

  /* Write the EEPROM address. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), o + n - 1);

  /* Loop on all buffer */
  while(n-- > 0)
    {
      /* Write the value. */
      mmc_out(ioaddr, mmwoff(0, mmw_fee_data_h), (*--b) >> 8);
      mmc_out(ioaddr, mmwoff(0, mmw_fee_data_l), *b & 0xFF);

      /* Write the write command. */
      mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WRITE);

      /* WaveLAN documentation says to wait at least 10 ms for EEBUSY = 0 */
      mdelay(10);
      fee_wait(ioaddr, 10, 100);
    }

  /* Write disable. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), MMW_FEE_ADDR_DS);
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_WDS);

  fee_wait(ioaddr, 10, 100);

#ifdef EEPROM_IS_PROTECTED	/* disabled */
  /* Reprotect EEPROM. */
  mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), 0x00);
  mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl), MMW_FEE_CTRL_PRWRITE);

  fee_wait(ioaddr, 10, 100);
#endif	/* EEPROM_IS_PROTECTED */
}
#endif	/* WIRELESS_EXT */

/************************ I82586 SUBROUTINES *************************/
/*
 * Useful subroutines to manage the Ethernet controller
 */

/*------------------------------------------------------------------*/
/*
 * Read bytes from the on-board RAM.
 * Why does inlining this function make it fail?
 */
static /*inline*/ void
obram_read(u_long	ioaddr,
	   u_short	o,
	   u_char *	b,
	   int		n)
{
  outw(o, PIOR1(ioaddr));
  insw(PIOP1(ioaddr), (unsigned short *) b, (n + 1) >> 1);
}

/*------------------------------------------------------------------*/
/*
 * Write bytes to the on-board RAM.
 */
static inline void
obram_write(u_long	ioaddr,
	    u_short	o,
	    u_char *	b,
	    int		n)
{
  outw(o, PIOR1(ioaddr));
  outsw(PIOP1(ioaddr), (unsigned short *) b, (n + 1) >> 1);
}

/*------------------------------------------------------------------*/
/*
 * Acknowledge the reading of the status issued by the i82586.
 */
static void
wv_ack(device *		dev)
{
  net_local *	lp = (net_local *)dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_short	scb_cs;
  int		i;

  obram_read(ioaddr, scboff(OFFSET_SCB, scb_status),
	     (unsigned char *) &scb_cs, sizeof(scb_cs));
  scb_cs &= SCB_ST_INT;

  if(scb_cs == 0)
    return;

  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *) &scb_cs, sizeof(scb_cs));

  set_chan_attn(ioaddr, lp->hacr);

  for(i = 1000; i > 0; i--)
    {
      obram_read(ioaddr, scboff(OFFSET_SCB, scb_command), (unsigned char *)&scb_cs, sizeof(scb_cs));
      if(scb_cs == 0)
	break;

      udelay(10);
    }
  udelay(100);

#ifdef DEBUG_CONFIG_ERROR
  if(i <= 0)
    printk(KERN_INFO "%s: wv_ack(): board not accepting command.\n",
	   dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * Set channel attention bit and busy wait until command has
 * completed, then acknowledge completion of the command.
 */
static inline int
wv_synchronous_cmd(device *	dev,
		   const char *	str)
{
  net_local *	lp = (net_local *)dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_short	scb_cmd;
  ach_t		cb;
  int		i;

  scb_cmd = SCB_CMD_CUC & SCB_CMD_CUC_GO;
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *) &scb_cmd, sizeof(scb_cmd));

  set_chan_attn(ioaddr, lp->hacr);

  for (i = 1000; i > 0; i--)
    {
      obram_read(ioaddr, OFFSET_CU, (unsigned char *)&cb, sizeof(cb));
      if (cb.ac_status & AC_SFLD_C)
	break;

      udelay(10);
    }
  udelay(100);

  if(i <= 0 || !(cb.ac_status & AC_SFLD_OK))
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: %s failed; status = 0x%x\n",
	     dev->name, str, cb.ac_status);
#endif
#ifdef DEBUG_I82586_SHOW
      wv_scb_show(ioaddr);
#endif
      return -1;
    }

  /* Ack the status */
  wv_ack(dev);

  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Configuration commands completion interrupt.
 * Check if done, and if OK.
 */
static inline int
wv_config_complete(device *	dev,
		   u_long	ioaddr,
		   net_local *	lp)
{
  unsigned short	mcs_addr;
  unsigned short	status;
  int			ret;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wv_config_complete()\n", dev->name);
#endif

  mcs_addr = lp->tx_first_in_use + sizeof(ac_tx_t) + sizeof(ac_nop_t)
    + sizeof(tbd_t) + sizeof(ac_cfg_t) + sizeof(ac_ias_t);

  /* Read the status of the last command (set mc list). */
  obram_read(ioaddr, acoff(mcs_addr, ac_status), (unsigned char *)&status, sizeof(status));

  /* If not completed -> exit */
  if((status & AC_SFLD_C) == 0)
    ret = 0;		/* Not ready to be scrapped */
  else
    {
#ifdef DEBUG_CONFIG_ERROR
      unsigned short	cfg_addr;
      unsigned short	ias_addr;

      /* Check mc_config command */
      if((status & AC_SFLD_OK) != AC_SFLD_OK)
	printk(KERN_INFO "%s: wv_config_complete(): set_multicast_address failed; status = 0x%x\n",
	       dev->name, status);

      /* check ia-config command */
      ias_addr = mcs_addr - sizeof(ac_ias_t);
      obram_read(ioaddr, acoff(ias_addr, ac_status), (unsigned char *)&status, sizeof(status));
      if((status & AC_SFLD_OK) != AC_SFLD_OK)
	printk(KERN_INFO "%s: wv_config_complete(): set_MAC_address failed; status = 0x%x\n",
	       dev->name, status);

      /* Check config command. */
      cfg_addr = ias_addr - sizeof(ac_cfg_t);
      obram_read(ioaddr, acoff(cfg_addr, ac_status), (unsigned char *)&status, sizeof(status));
      if((status & AC_SFLD_OK) != AC_SFLD_OK)
	printk(KERN_INFO "%s: wv_config_complete(): configure failed; status = 0x%x\n",
	       dev->name, status);
#endif	/* DEBUG_CONFIG_ERROR */

      ret = 1;		/* Ready to be scrapped */
    }

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wv_config_complete() - %d\n", dev->name, ret);
#endif
  return ret;
}

/*------------------------------------------------------------------*/
/*
 * Command completion interrupt.
 * Reclaim as many freed tx buffers as we can.
 */
static int
wv_complete(device *	dev,
	    u_long	ioaddr,
	    net_local *	lp)
{
  int	nreaped = 0;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wv_complete()\n", dev->name);
#endif

  /* Loop on all the transmit buffers */
  while(lp->tx_first_in_use != I82586NULL)
    {
      unsigned short	tx_status;

      /* Read the first transmit buffer */
      obram_read(ioaddr, acoff(lp->tx_first_in_use, ac_status), (unsigned char *)&tx_status, sizeof(tx_status));

      /* If not completed -> exit */
      if((tx_status & AC_SFLD_C) == 0)
	break;

      /* Hack for reconfiguration */
      if(tx_status == 0xFFFF)
	if(!wv_config_complete(dev, ioaddr, lp))
	  break;	/* Not completed */

      /* We now remove this buffer */
      nreaped++;
      --lp->tx_n_in_use;

/*
if (lp->tx_n_in_use > 0)
	printk("%c", "0123456789abcdefghijk"[lp->tx_n_in_use]);
*/

      /* Was it the last one? */
      if(lp->tx_n_in_use <= 0)
	lp->tx_first_in_use = I82586NULL;
      else
	{
	  /* Next one in the chain */
	  lp->tx_first_in_use += TXBLOCKZ;
	  if(lp->tx_first_in_use >= OFFSET_CU + NTXBLOCKS * TXBLOCKZ)
	    lp->tx_first_in_use -= NTXBLOCKS * TXBLOCKZ;
	}

      /* Hack for reconfiguration */
      if(tx_status == 0xFFFF)
	continue;

      /* Now, check status of the finished command */
      if(tx_status & AC_SFLD_OK)
	{
	  int	ncollisions;

	  lp->stats.tx_packets++;
	  ncollisions = tx_status & AC_SFLD_MAXCOL;
	  lp->stats.collisions += ncollisions;
#ifdef DEBUG_TX_INFO
	  if(ncollisions > 0)
	    printk(KERN_DEBUG "%s: wv_complete(): tx completed after %d collisions.\n",
		   dev->name, ncollisions);
#endif
	}
      else
	{
	  lp->stats.tx_errors++;
	  if(tx_status & AC_SFLD_S10)
	    {
	      lp->stats.tx_carrier_errors++;
#ifdef DEBUG_TX_FAIL
	      printk(KERN_DEBUG "%s: wv_complete(): tx error: no CS.\n",
		     dev->name);
#endif
	    }
	  if(tx_status & AC_SFLD_S9)
	    {
	      lp->stats.tx_carrier_errors++;
#ifdef DEBUG_TX_FAIL
	      printk(KERN_DEBUG "%s: wv_complete(): tx error: lost CTS.\n",
		     dev->name);
#endif
	    }
	  if(tx_status & AC_SFLD_S8)
	    {
	      lp->stats.tx_fifo_errors++;
#ifdef DEBUG_TX_FAIL
	      printk(KERN_DEBUG "%s: wv_complete(): tx error: slow DMA.\n",
		     dev->name);
#endif
	    }
	  if(tx_status & AC_SFLD_S6)
	    {
	      lp->stats.tx_heartbeat_errors++;
#ifdef DEBUG_TX_FAIL
	      printk(KERN_DEBUG "%s: wv_complete(): tx error: heart beat.\n",
		     dev->name);
#endif
	    }
	  if(tx_status & AC_SFLD_S5)
	    {
	      lp->stats.tx_aborted_errors++;
#ifdef DEBUG_TX_FAIL
	      printk(KERN_DEBUG "%s: wv_complete(): tx error: too many collisions.\n",
		     dev->name);
#endif
	    }
	}

#ifdef DEBUG_TX_INFO
      printk(KERN_DEBUG "%s: wv_complete(): tx completed, tx_status 0x%04x\n",
	     dev->name, tx_status);
#endif
    }

#ifdef DEBUG_INTERRUPT_INFO
  if(nreaped > 1)
    printk(KERN_DEBUG "%s: wv_complete(): reaped %d\n", dev->name, nreaped);
#endif

  /*
   * Inform upper layers.
   */
  if(lp->tx_n_in_use < NTXBLOCKS - 1)
    {
      dev->tbusy = 0;
      mark_bh(NET_BH);
    }

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wv_complete()\n", dev->name);
#endif
  return nreaped;
}

/*------------------------------------------------------------------*/
/*
 * Reconfigure the i82586, or at least ask for it.
 * Because wv_82586_config uses a transmission buffer, we must do it
 * when we are sure that there is one left, so we do it now
 * or in wavelan_packet_xmit() (I can't find any better place,
 * wavelan_interrupt is not an option), so you may experience
 * delays sometimes.
 */
static inline void
wv_82586_reconfig(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;

  /* Check if we can do it now ! */
  if(!(dev->start) || (test_and_set_bit(0, (void *)&dev->tbusy) != 0))
    {
      lp->reconfig_82586 = 1;
#ifdef DEBUG_CONFIG_INFO
      printk(KERN_DEBUG "%s: wv_82586_reconfig(): delayed (busy = %ld, start = %d)\n",
	     dev->name, dev->tbusy, dev->start);
#endif
    }
  else
    wv_82586_config(dev);
}

/********************* DEBUG & INFO SUBROUTINES *********************/
/*
 * This routine is used in the code to show information for debugging.
 * Most of the time, it dumps the contents of hardware structures.
 */

#ifdef DEBUG_PSA_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted contents of the Parameter Storage Area.
 */
static void
wv_psa_show(psa_t *	p)
{
  printk(KERN_DEBUG "##### WaveLAN PSA contents: #####\n");
  printk(KERN_DEBUG "psa_io_base_addr_1: 0x%02X %02X %02X %02X\n",
	 p->psa_io_base_addr_1,
	 p->psa_io_base_addr_2,
	 p->psa_io_base_addr_3,
	 p->psa_io_base_addr_4);
  printk(KERN_DEBUG "psa_rem_boot_addr_1: 0x%02X %02X %02X\n",
	 p->psa_rem_boot_addr_1,
	 p->psa_rem_boot_addr_2,
	 p->psa_rem_boot_addr_3);
  printk(KERN_DEBUG "psa_holi_params: 0x%02x, ", p->psa_holi_params);
  printk("psa_int_req_no: %d\n", p->psa_int_req_no);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "psa_unused0[]: %02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
	 p->psa_unused0[0],
	 p->psa_unused0[1],
	 p->psa_unused0[2],
	 p->psa_unused0[3],
	 p->psa_unused0[4],
	 p->psa_unused0[5],
	 p->psa_unused0[6]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "psa_univ_mac_addr[]: %02x:%02x:%02x:%02x:%02x:%02x\n",
	 p->psa_univ_mac_addr[0],
	 p->psa_univ_mac_addr[1],
	 p->psa_univ_mac_addr[2],
	 p->psa_univ_mac_addr[3],
	 p->psa_univ_mac_addr[4],
	 p->psa_univ_mac_addr[5]);
  printk(KERN_DEBUG "psa_local_mac_addr[]: %02x:%02x:%02x:%02x:%02x:%02x\n",
	 p->psa_local_mac_addr[0],
	 p->psa_local_mac_addr[1],
	 p->psa_local_mac_addr[2],
	 p->psa_local_mac_addr[3],
	 p->psa_local_mac_addr[4],
	 p->psa_local_mac_addr[5]);
  printk(KERN_DEBUG "psa_univ_local_sel: %d, ", p->psa_univ_local_sel);
  printk("psa_comp_number: %d, ", p->psa_comp_number);
  printk("psa_thr_pre_set: 0x%02x\n", p->psa_thr_pre_set);
  printk(KERN_DEBUG "psa_feature_select/decay_prm: 0x%02x, ",
	 p->psa_feature_select);
  printk("psa_subband/decay_update_prm: %d\n", p->psa_subband);
  printk(KERN_DEBUG "psa_quality_thr: 0x%02x, ", p->psa_quality_thr);
  printk("psa_mod_delay: 0x%02x\n", p->psa_mod_delay);
  printk(KERN_DEBUG "psa_nwid: 0x%02x%02x, ", p->psa_nwid[0], p->psa_nwid[1]);
  printk("psa_nwid_select: %d\n", p->psa_nwid_select);
  printk(KERN_DEBUG "psa_encryption_select: %d, ", p->psa_encryption_select);
  printk("psa_encryption_key[]: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	 p->psa_encryption_key[0],
	 p->psa_encryption_key[1],
	 p->psa_encryption_key[2],
	 p->psa_encryption_key[3],
	 p->psa_encryption_key[4],
	 p->psa_encryption_key[5],
	 p->psa_encryption_key[6],
	 p->psa_encryption_key[7]);
  printk(KERN_DEBUG "psa_databus_width: %d\n", p->psa_databus_width);
  printk(KERN_DEBUG "psa_call_code/auto_squelch: 0x%02x, ",
	 p->psa_call_code[0]);
  printk("psa_call_code[]: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
	 p->psa_call_code[0],
	 p->psa_call_code[1],
	 p->psa_call_code[2],
	 p->psa_call_code[3],
	 p->psa_call_code[4],
	 p->psa_call_code[5],
	 p->psa_call_code[6],
	 p->psa_call_code[7]);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "psa_reserved[]: %02X:%02X:%02X:%02X\n",
	 p->psa_reserved[0],
	 p->psa_reserved[1],
	 p->psa_reserved[2],
	 p->psa_reserved[3]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "psa_conf_status: %d, ", p->psa_conf_status);
  printk("psa_crc: 0x%02x%02x, ", p->psa_crc[0], p->psa_crc[1]);
  printk("psa_crc_status: 0x%02x\n", p->psa_crc_status);
} /* wv_psa_show */
#endif	/* DEBUG_PSA_SHOW */

#ifdef DEBUG_MMC_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the Modem Management Controller.
 * This function needs to be completed.
 */
static void
wv_mmc_show(device *	dev)
{
  u_long	ioaddr = dev->base_addr;
  net_local *	lp = (net_local *)dev->priv;
  mmr_t		m;

  /* Basic check */
  if(hasr_read(ioaddr) & HASR_NO_CLK)
    {
      printk(KERN_WARNING "%s: wv_mmc_show: modem not connected\n",
	     dev->name);
      return;
    }

  /* Read the mmc */
  mmc_out(ioaddr, mmwoff(0, mmw_freeze), 1);
  mmc_read(ioaddr, 0, (u_char *)&m, sizeof(m));
  mmc_out(ioaddr, mmwoff(0, mmw_freeze), 0);

#ifdef WIRELESS_EXT	/* if wireless extension exists in the kernel */
  /* Don't forget to update statistics */
  lp->wstats.discard.nwid += (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l;
#endif	/* WIRELESS_EXT */

  printk(KERN_DEBUG "##### WaveLAN modem status registers: #####\n");
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused0[]: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
	 m.mmr_unused0[0],
	 m.mmr_unused0[1],
	 m.mmr_unused0[2],
	 m.mmr_unused0[3],
	 m.mmr_unused0[4],
	 m.mmr_unused0[5],
	 m.mmr_unused0[6],
	 m.mmr_unused0[7]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "Encryption algorithm: %02X - Status: %02X\n",
	 m.mmr_des_avail, m.mmr_des_status);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused1[]: %02X:%02X:%02X:%02X:%02X\n",
	 m.mmr_unused1[0],
	 m.mmr_unused1[1],
	 m.mmr_unused1[2],
	 m.mmr_unused1[3],
	 m.mmr_unused1[4]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "dce_status: 0x%x [%s%s%s%s]\n",
	 m.mmr_dce_status,
	 (m.mmr_dce_status & MMR_DCE_STATUS_RX_BUSY) ? "energy detected,":"",
	 (m.mmr_dce_status & MMR_DCE_STATUS_LOOPT_IND) ?
	 "loop test indicated," : "",
	 (m.mmr_dce_status & MMR_DCE_STATUS_TX_BUSY) ? "transmitter on," : "",
	 (m.mmr_dce_status & MMR_DCE_STATUS_JBR_EXPIRED) ?
	 "jabber timer expired," : "");
  printk(KERN_DEBUG "Dsp ID: %02X\n",
	 m.mmr_dsp_id);
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "mmc_unused2[]: %02X:%02X\n",
	 m.mmr_unused2[0],
	 m.mmr_unused2[1]);
#endif	/* DEBUG_SHOW_UNUSED */
  printk(KERN_DEBUG "# correct_nwid: %d, # wrong_nwid: %d\n",
	 (m.mmr_correct_nwid_h << 8) | m.mmr_correct_nwid_l,
	 (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l);
  printk(KERN_DEBUG "thr_pre_set: 0x%x [current signal %s]\n",
	 m.mmr_thr_pre_set & MMR_THR_PRE_SET,
	 (m.mmr_thr_pre_set & MMR_THR_PRE_SET_CUR) ? "above" : "below");
  printk(KERN_DEBUG "signal_lvl: %d [%s], ",
	 m.mmr_signal_lvl & MMR_SIGNAL_LVL,
	 (m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) ? "new msg" : "no new msg");
  printk("silence_lvl: %d [%s], ", m.mmr_silence_lvl & MMR_SILENCE_LVL,
	 (m.mmr_silence_lvl & MMR_SILENCE_LVL_VALID) ? "update done" : "no new update");
  printk("sgnl_qual: 0x%x [%s]\n",
	 m.mmr_sgnl_qual & MMR_SGNL_QUAL,
	 (m.mmr_sgnl_qual & MMR_SGNL_QUAL_ANT) ? "Antenna 1" : "Antenna 0");
#ifdef DEBUG_SHOW_UNUSED
  printk(KERN_DEBUG "netw_id_l: %x\n", m.mmr_netw_id_l);
#endif	/* DEBUG_SHOW_UNUSED */
} /* wv_mmc_show */
#endif	/* DEBUG_MMC_SHOW */

#ifdef DEBUG_I82586_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the last block of the i82586 memory.
 */
static void
wv_scb_show(u_long	ioaddr)
{
  scb_t		scb;

  obram_read(ioaddr, OFFSET_SCB, (unsigned char *)&scb, sizeof(scb));   

  printk(KERN_DEBUG "##### WaveLAN system control block: #####\n");

  printk(KERN_DEBUG "status: ");
  printk("stat 0x%x[%s%s%s%s] ",
	 (scb.scb_status & (SCB_ST_CX | SCB_ST_FR | SCB_ST_CNA | SCB_ST_RNR)) >> 12,
	 (scb.scb_status & SCB_ST_CX) ? "command completion interrupt," : "",
	 (scb.scb_status & SCB_ST_FR) ? "frame received," : "",
	 (scb.scb_status & SCB_ST_CNA) ? "command unit not active," : "",
	 (scb.scb_status & SCB_ST_RNR) ? "receiving unit not ready," : "");
  printk("cus 0x%x[%s%s%s] ",
	 (scb.scb_status & SCB_ST_CUS) >> 8,
	 ((scb.scb_status & SCB_ST_CUS) == SCB_ST_CUS_IDLE) ? "idle" : "",
	 ((scb.scb_status & SCB_ST_CUS) == SCB_ST_CUS_SUSP) ? "suspended" : "",
	 ((scb.scb_status & SCB_ST_CUS) == SCB_ST_CUS_ACTV) ? "active" : "");
  printk("rus 0x%x[%s%s%s%s]\n",
	 (scb.scb_status & SCB_ST_RUS) >> 4,
	 ((scb.scb_status & SCB_ST_RUS) == SCB_ST_RUS_IDLE) ? "idle" : "",
	 ((scb.scb_status & SCB_ST_RUS) == SCB_ST_RUS_SUSP) ? "suspended" : "",
	 ((scb.scb_status & SCB_ST_RUS) == SCB_ST_RUS_NRES) ? "no resources" : "",
	 ((scb.scb_status & SCB_ST_RUS) == SCB_ST_RUS_RDY) ? "ready" : "");

  printk(KERN_DEBUG "command: ");
  printk("ack 0x%x[%s%s%s%s] ",
	 (scb.scb_command & (SCB_CMD_ACK_CX | SCB_CMD_ACK_FR | SCB_CMD_ACK_CNA | SCB_CMD_ACK_RNR)) >> 12,
	 (scb.scb_command & SCB_CMD_ACK_CX) ? "ack cmd completion," : "",
	 (scb.scb_command & SCB_CMD_ACK_FR) ? "ack frame received," : "",
	 (scb.scb_command & SCB_CMD_ACK_CNA) ? "ack CU not active," : "",
	 (scb.scb_command & SCB_CMD_ACK_RNR) ? "ack RU not ready," : "");
  printk("cuc 0x%x[%s%s%s%s%s] ",
	 (scb.scb_command & SCB_CMD_CUC) >> 8,
	 ((scb.scb_command & SCB_CMD_CUC) == SCB_CMD_CUC_NOP) ? "nop" : "",
	 ((scb.scb_command & SCB_CMD_CUC) == SCB_CMD_CUC_GO) ? "start cbl_offset" : "",
	 ((scb.scb_command & SCB_CMD_CUC) == SCB_CMD_CUC_RES) ? "resume execution" : "",
	 ((scb.scb_command & SCB_CMD_CUC) == SCB_CMD_CUC_SUS) ? "suspend execution" : "",
	 ((scb.scb_command & SCB_CMD_CUC) == SCB_CMD_CUC_ABT) ? "abort execution" : "");
  printk("ruc 0x%x[%s%s%s%s%s]\n",
	 (scb.scb_command & SCB_CMD_RUC) >> 4,
	 ((scb.scb_command & SCB_CMD_RUC) == SCB_CMD_RUC_NOP) ? "nop" : "",
	 ((scb.scb_command & SCB_CMD_RUC) == SCB_CMD_RUC_GO) ? "start rfa_offset" : "",
	 ((scb.scb_command & SCB_CMD_RUC) == SCB_CMD_RUC_RES) ? "resume reception" : "",
	 ((scb.scb_command & SCB_CMD_RUC) == SCB_CMD_RUC_SUS) ? "suspend reception" : "",
	 ((scb.scb_command & SCB_CMD_RUC) == SCB_CMD_RUC_ABT) ? "abort reception" : "");

  printk(KERN_DEBUG "cbl_offset 0x%x ", scb.scb_cbl_offset);
  printk("rfa_offset 0x%x\n", scb.scb_rfa_offset);

  printk(KERN_DEBUG "crcerrs %d ", scb.scb_crcerrs);
  printk("alnerrs %d ", scb.scb_alnerrs);
  printk("rscerrs %d ", scb.scb_rscerrs);
  printk("ovrnerrs %d\n", scb.scb_ovrnerrs);
}

/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the i82586's receive unit.
 */
static void
wv_ru_show(device *	dev)
{
  /* net_local *lp = (net_local *) dev->priv; */

  printk(KERN_DEBUG "##### WaveLAN i82586 receiver unit status: #####\n");
  printk(KERN_DEBUG "ru:");
  /*
   * Not implemented yet
   */
  printk("\n");
} /* wv_ru_show */

/*------------------------------------------------------------------*/
/*
 * Display info about one control block of the i82586 memory.
 */
static void
wv_cu_show_one(device *		dev,
	       net_local *	lp,
	       int		i,
	       u_short		p)
{
  u_long		ioaddr;
  ac_tx_t		actx;

  ioaddr = dev->base_addr;

  printk("%d: 0x%x:", i, p);

  obram_read(ioaddr, p, (unsigned char *)&actx, sizeof(actx));
  printk(" status=0x%x,", actx.tx_h.ac_status);
  printk(" command=0x%x,", actx.tx_h.ac_command);

  /*
  {
    tbd_t	tbd;

    obram_read(ioaddr, actx.tx_tbd_offset, (unsigned char *)&tbd, sizeof(tbd));
    printk(" tbd_status=0x%x,", tbd.tbd_status);
  }
  */

  printk("|");
}

/*------------------------------------------------------------------*/
/*
 * Print status of the command unit of the i82586.
 */
static void
wv_cu_show(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;
  unsigned int	i;
  u_short	p;

  printk(KERN_DEBUG "##### WaveLAN i82586 command unit status: #####\n");

  printk(KERN_DEBUG);
  for(i = 0, p = lp->tx_first_in_use; i < NTXBLOCKS; i++)
    {
      wv_cu_show_one(dev, lp, i, p);

      p += TXBLOCKZ;
      if(p >= OFFSET_CU + NTXBLOCKS * TXBLOCKZ)
	p -= NTXBLOCKS * TXBLOCKZ;
    }
  printk("\n");
}
#endif	/* DEBUG_I82586_SHOW */

#ifdef DEBUG_DEVICE_SHOW
/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the WaveLAN PCMCIA device driver.
 */
static void
wv_dev_show(device *	dev)
{
  printk(KERN_DEBUG "dev:");
  printk(" start=%d,", dev->start);
  printk(" tbusy=%ld,", dev->tbusy);
  printk(" interrupt=%d,", dev->interrupt);
  printk(" trans_start=%ld,", dev->trans_start);
  printk(" flags=0x%x,", dev->flags);
  printk("\n");
} /* wv_dev_show */

/*------------------------------------------------------------------*/
/*
 * Print the formatted status of the WaveLAN PCMCIA device driver's
 * private information.
 */
static void
wv_local_show(device *	dev)
{
  net_local *lp;

  lp = (net_local *)dev->priv;

  printk(KERN_DEBUG "local:");
  printk(" tx_n_in_use=%d,", lp->tx_n_in_use);
  printk(" hacr=0x%x,", lp->hacr);
  printk(" rx_head=0x%x,", lp->rx_head);
  printk(" rx_last=0x%x,", lp->rx_last);
  printk(" tx_first_free=0x%x,", lp->tx_first_free);
  printk(" tx_first_in_use=0x%x,", lp->tx_first_in_use);
  printk("\n");
} /* wv_local_show */
#endif	/* DEBUG_DEVICE_SHOW */

#if defined(DEBUG_RX_INFO) || defined(DEBUG_TX_INFO)
/*------------------------------------------------------------------*/
/*
 * Dump packet header (and content if necessary) on the screen
 */
static inline void
wv_packet_info(u_char *		p,		/* Packet to dump */
	       int		length,		/* Length of the packet */
	       char *		msg1,		/* Name of the device */
	       char *		msg2)		/* Name of the function */
{
  int		i;
  int		maxi;

  printk(KERN_DEBUG "%s: %s(): dest %02X:%02X:%02X:%02X:%02X:%02X, length %d\n",
	 msg1, msg2, p[0], p[1], p[2], p[3], p[4], p[5], length);
  printk(KERN_DEBUG "%s: %s(): src %02X:%02X:%02X:%02X:%02X:%02X, type 0x%02X%02X\n",
	 msg1, msg2, p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13]);

#ifdef DEBUG_PACKET_DUMP

  printk(KERN_DEBUG "data=\"");

  if((maxi = length) > DEBUG_PACKET_DUMP)
    maxi = DEBUG_PACKET_DUMP;
  for(i = 14; i < maxi; i++)
    if(p[i] >= ' ' && p[i] <= '~')
      printk(" %c", p[i]);
    else
      printk("%02X", p[i]);
  if(maxi < length)
    printk("..");
  printk("\"\n");
  printk(KERN_DEBUG "\n");
#endif	/* DEBUG_PACKET_DUMP */
}
#endif	/* defined(DEBUG_RX_INFO) || defined(DEBUG_TX_INFO) */

/*------------------------------------------------------------------*/
/*
 * This is the information which is displayed by the driver at startup.
 * There are lots of flags for configuring it to your liking.
 */
static inline void
wv_init_info(device *	dev)
{
  short		ioaddr = dev->base_addr;
  net_local *	lp = (net_local *)dev->priv;
  psa_t		psa;
  int		i;

  /* Read the parameter storage area */
  psa_read(ioaddr, lp->hacr, 0, (unsigned char *) &psa, sizeof(psa));

#ifdef DEBUG_PSA_SHOW
  wv_psa_show(&psa);
#endif
#ifdef DEBUG_MMC_SHOW
  wv_mmc_show(dev);
#endif
#ifdef DEBUG_I82586_SHOW
  wv_cu_show(dev);
#endif

#ifdef DEBUG_BASIC_SHOW
  /* Now, let's go for the basic stuff. */
  printk(KERN_NOTICE "%s: WaveLAN at %#x,", dev->name, ioaddr);
  for(i = 0; i < WAVELAN_ADDR_SIZE; i++)
    printk("%s%02X", (i == 0) ? " " : ":", dev->dev_addr[i]);
  printk(", IRQ %d", dev->irq);

  /* Print current network ID. */
  if(psa.psa_nwid_select)
    printk(", nwid 0x%02X-%02X", psa.psa_nwid[0], psa.psa_nwid[1]);
  else
    printk(", nwid off");

  /* If 2.00 card */
  if(!(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) &
       (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
    {
      unsigned short	freq;

      /* Ask the EEPROM to read the frequency from the first area. */
      fee_read(ioaddr, 0x00, &freq, 1);

      /* Print frequency */
      printk(", 2.00, %ld", (freq >> 6) + 2400L);

      /* Hack! */
      if(freq & 0x20)
	printk(".5");
    }
  else
    {
      printk(", PC");
      switch(psa.psa_comp_number)
	{
	case PSA_COMP_PC_AT_915:
	case PSA_COMP_PC_AT_2400:
	  printk("-AT");
	  break;
	case PSA_COMP_PC_MC_915:
	case PSA_COMP_PC_MC_2400:
	  printk("-MC");
	  break;
	case PSA_COMP_PCMCIA_915:
	  printk("MCIA");
	  break;
	default:
	  printk("?");
	}
      printk(", ");
      switch (psa.psa_subband)
	{
	case PSA_SUBBAND_915:
	  printk("915");
	  break;
	case PSA_SUBBAND_2425:
	  printk("2425");
	  break;
	case PSA_SUBBAND_2460:
	  printk("2460");
	  break;
	case PSA_SUBBAND_2484:
	  printk("2484");
	  break;
	case PSA_SUBBAND_2430_5:
	  printk("2430.5");
	  break;
	default:
	  printk("?");
	}
    }

  printk(" MHz\n");
#endif	/* DEBUG_BASIC_SHOW */

#ifdef DEBUG_VERSION_SHOW
  /* Print version information */
  printk(KERN_NOTICE "%s", version);
#endif
} /* wv_init_info */

/********************* IOCTL, STATS & RECONFIG *********************/
/*
 * We found here routines that are called by Linux on different
 * occasions after the configuration and not for transmitting data
 * These may be called when the user use ifconfig, /proc/net/dev
 * or wireless extensions
 */

/*------------------------------------------------------------------*/
/*
 * Get the current Ethernet statistics. This may be called with the
 * card open or closed.
 * Used when the user read /proc/net/dev
 */
static en_stats	*
wavelan_get_stats(device *	dev)
{
#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <>wavelan_get_stats()\n", dev->name);
#endif

  return(&((net_local *) dev->priv)->stats);
}

/*------------------------------------------------------------------*/
/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void
wavelan_set_multicast_list(device *	dev)
{
  net_local *	lp = (net_local *) dev->priv;

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_set_multicast_list()\n", dev->name);
#endif

#ifdef DEBUG_IOCTL_INFO
  printk(KERN_DEBUG "%s: wavelan_set_multicast_list(): setting Rx mode %02X to %d addresses.\n",
	 dev->name, dev->flags, dev->mc_count);
#endif

  /* Are we asking for promiscuous mode,
   * or all multicast addresses (we don't have that!)
   * or too many multicast addresses for the hardware filter? */
  if((dev->flags & IFF_PROMISC) ||
     (dev->flags & IFF_ALLMULTI) ||
     (dev->mc_count > I82586_MAX_MULTICAST_ADDRESSES))
    {
      /*
       * Enable promiscuous mode: receive all packets.
       */
      if(!lp->promiscuous)
	{
	  lp->promiscuous = 1;
	  lp->mc_count = 0;

	  wv_82586_reconfig(dev);

	  /* Tell the kernel that we are doing a really bad job. */
	  dev->flags |= IFF_PROMISC;
	}
    }
  else
    /* Are there multicast addresses to send? */
    if(dev->mc_list != (struct dev_mc_list *) NULL)
      {
	/*
	 * Disable promiscuous mode, but receive all packets
	 * in multicast list
	 */
#ifdef MULTICAST_AVOID
	if(lp->promiscuous ||
	   (dev->mc_count != lp->mc_count))
#endif
	  {
	    lp->promiscuous = 0;
	    lp->mc_count = dev->mc_count;

	    wv_82586_reconfig(dev);
	  }
      }
    else
      {
	/*
	 * Switch to normal mode: disable promiscuous mode and 
	 * clear the multicast list.
	 */
	if(lp->promiscuous || lp->mc_count == 0)
	  {
	    lp->promiscuous = 0;
	    lp->mc_count = 0;

	    wv_82586_reconfig(dev);
	  }
      }
#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_set_multicast_list()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * This function doesn't exist.
 * (Note : it was a nice way to test the reconfigure stuff...)
 */
#ifdef SET_MAC_ADDRESS
static int
wavelan_set_mac_address(device *	dev,
			void *		addr)
{
  struct sockaddr *	mac = addr;

  /* Copy the address. */
  memcpy(dev->dev_addr, mac->sa_data, WAVELAN_ADDR_SIZE);

  /* Reconfigure the beast. */
  wv_82586_reconfig(dev);

  return 0;
}
#endif	/* SET_MAC_ADDRESS */

#ifdef WIRELESS_EXT	/* if wireless extensions exist in the kernel */

/*------------------------------------------------------------------*/
/*
 * Frequency setting (for hardware capable of it)
 * It's a bit complicated and you don't really want to look into it.
 * (called in wavelan_ioctl)
 */
static inline int
wv_set_frequency(u_long		ioaddr,	/* I/O port of the card */
		 iw_freq *	frequency)
{
  const int	BAND_NUM = 10;	/* Number of bands */
  long		freq = 0L;	/* offset to 2.4 GHz in .5 MHz */
#ifdef DEBUG_IOCTL_INFO
  int		i;
#endif

  /* Setting by frequency */
  /* Theoretically, you may set any frequency between
   * the two limits with a 0.5 MHz precision. In practice,
   * I don't want you to have trouble with local regulations.
   */
  if((frequency->e == 1) &&
     (frequency->m >= (int) 2.412e8) && (frequency->m <= (int) 2.487e8))
    {
      freq = ((frequency->m / 10000) - 24000L) / 5;
    }

  /* Setting by channel (same as wfreqsel) */
  /* Warning: each channel is 22 MHz wide, so some of the channels
   * will interfere. */
  if((frequency->e == 0) &&
     (frequency->m >= 0) && (frequency->m < BAND_NUM))
    {
      /* Get frequency offset. */
      freq = channel_bands[frequency->m] >> 1;
    }

  /* Verify that the frequency is allowed. */
  if(freq != 0L)
    {
      u_short	table[10];	/* Authorized frequency table */

      /* Read the frequency table. */
      fee_read(ioaddr, 0x71, table, 10);

#ifdef DEBUG_IOCTL_INFO
      printk(KERN_DEBUG "Frequency table: ");
      for(i = 0; i < 10; i++)
	{
	  printk(" %04X",
		 table[i]);
	}
      printk("\n");
#endif

      /* Look in the table to see whether the frequency is allowed. */
      if(!(table[9 - ((freq - 24) / 16)] &
	   (1 << ((freq - 24) % 16))))
	return -EINVAL;		/* not allowed */
    }
  else
    return -EINVAL;

  /* if we get a usable frequency */
  if(freq != 0L)
    {
      unsigned short	area[16];
      unsigned short	dac[2];
      unsigned short	area_verify[16];
      unsigned short	dac_verify[2];
      /* Corresponding gain (in the power adjust value table)
       * See AT&T WaveLAN Data Manual, REF 407-024689/E, page 3-8
       * and WCIN062D.DOC, page 6.2.9. */
      unsigned short	power_limit[] = { 40, 80, 120, 160, 0 };
      int		power_band = 0;		/* Selected band */
      unsigned short	power_adjust;		/* Correct value */

      /* Search for the gain. */
      power_band = 0;
      while((freq > power_limit[power_band]) &&
	    (power_limit[++power_band] != 0))
	;

      /* Read the first area. */
      fee_read(ioaddr, 0x00, area, 16);

      /* Read the DAC. */
      fee_read(ioaddr, 0x60, dac, 2);

      /* Read the new power adjust value. */
      fee_read(ioaddr, 0x6B - (power_band >> 1), &power_adjust, 1);
      if(power_band & 0x1)
	power_adjust >>= 8;
      else
	power_adjust &= 0xFF;

#ifdef DEBUG_IOCTL_INFO
      printk(KERN_DEBUG "WaveLAN EEPROM Area 1: ");
      for(i = 0; i < 16; i++)
	{
	  printk(" %04X",
		 area[i]);
	}
      printk("\n");

      printk(KERN_DEBUG "WaveLAN EEPROM DAC: %04X %04X\n",
	     dac[0], dac[1]);
#endif

      /* Frequency offset (for info only) */
      area[0] = ((freq << 5) & 0xFFE0) | (area[0] & 0x1F);

      /* Receiver Principle main divider coefficient */
      area[3] = (freq >> 1) + 2400L - 352L;
      area[2] = ((freq & 0x1) << 4) | (area[2] & 0xFFEF);

      /* Transmitter Main divider coefficient */
      area[13] = (freq >> 1) + 2400L;
      area[12] = ((freq & 0x1) << 4) | (area[2] & 0xFFEF);

      /* Other parts of the area are flags, bit streams or unused. */

      /* Set the value in the DAC. */
      dac[1] = ((power_adjust >> 1) & 0x7F) | (dac[1] & 0xFF80);
      dac[0] = ((power_adjust & 0x1) << 4) | (dac[0] & 0xFFEF);

      /* Write the first area. */
      fee_write(ioaddr, 0x00,
		area, 16);

      /* Write the DAC. */
      fee_write(ioaddr, 0x60,
		dac, 2);

      /* We now should verify here that the writing of the EEPROM went OK. */

      /* Reread the first area. */
      fee_read(ioaddr, 0x00, area_verify, 16);

      /* Reread the DAC. */
      fee_read(ioaddr, 0x60, dac_verify, 2);

      /* Compare. */
      if(memcmp(area, area_verify, 16 * 2) ||
	 memcmp(dac, dac_verify, 2 * 2))
	{
#ifdef DEBUG_IOCTL_ERROR
	  printk(KERN_INFO "WaveLAN: wv_set_frequency: unable to write new frequency to EEPROM(?).\n");
#endif
	  return -EOPNOTSUPP;
	}

      /* We must download the frequency parameters to the
       * synthesizers (from the EEPROM - area 1)
       * Note: as the EEPROM is automatically decremented, we set the end
       * if the area... */
      mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), 0x0F);
      mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl),
	      MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD);

      /* Wait until the download is finished. */
      fee_wait(ioaddr, 100, 100);

      /* We must now download the power adjust value (gain) to
       * the synthesizers (from the EEPROM - area 7 - DAC). */
      mmc_out(ioaddr, mmwoff(0, mmw_fee_addr), 0x61);
      mmc_out(ioaddr, mmwoff(0, mmw_fee_ctrl),
	      MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD);

      /* Wait for the download to finish. */
      fee_wait(ioaddr, 100, 100);

#ifdef DEBUG_IOCTL_INFO
      /* Verification of what we have done */

      printk(KERN_DEBUG "WaveLAN EEPROM Area 1: ");
      for(i = 0; i < 16; i++)
	{
	  printk(" %04X",
		 area_verify[i]);
	}
      printk("\n");

      printk(KERN_DEBUG "WaveLAN EEPROM DAC:  %04X %04X\n",
	     dac_verify[0], dac_verify[1]);
#endif

      return 0;
    }
  else
    return -EINVAL;		/* Bah, never get there... */
}

/*------------------------------------------------------------------*/
/*
 * Give the list of available frequencies.
 */
static inline int
wv_frequency_list(u_long	ioaddr,	/* I/O port of the card */
		  iw_freq *	list,	/* List of frequencies to fill */
		  int		max)	/* Maximum number of frequencies */
{
  u_short	table[10];	/* Authorized frequency table */
  long		freq = 0L;	/* offset to 2.4 GHz in .5 MHz + 12 MHz */
  int		i;		/* index in the table */
  int		c = 0;		/* Channel number */

  /* Read the frequency table. */
  fee_read(ioaddr, 0x71 /* frequency table */, table, 10);

  /* Check all frequencies. */
  i = 0;
  for(freq = 0; freq < 150; freq++)
    /* Look in the table if the frequency is allowed */
    if(table[9 - (freq / 16)] & (1 << (freq % 16)))
      {
	/* Compute approximate channel number */
	while((((channel_bands[c] >> 1) - 24) < freq) &&
	      (c < NELS(channel_bands)))
	  c++;
	list[i].i = c;	/* Set the list index */

	/* put in the list */
	list[i].m = (((freq + 24) * 5) + 24000L) * 10000;
	list[i++].e = 1;

	/* Check number. */
	if(i >= max)
	  return(i);
      }

  return(i);
}

#ifdef WIRELESS_SPY
/*------------------------------------------------------------------*/
/*
 * Gather wireless spy statistics:  for each packet, compare the source
 * address with our list, and if they match, get the statistics.
 * Sorry, but this function really needs the wireless extensions.
 */
static inline void
wl_spy_gather(device *	dev,
	      u_char *	mac,		/* MAC address */
	      u_char *	stats)		/* Statistics to gather */
{
  net_local *	lp = (net_local *) dev->priv;
  int		i;

  /* Check all addresses. */
  for(i = 0; i < lp->spy_number; i++)
    /* If match */
    if(!memcmp(mac, lp->spy_address[i], WAVELAN_ADDR_SIZE))
      {
	/* Update statistics */
	lp->spy_stat[i].qual = stats[2] & MMR_SGNL_QUAL;
	lp->spy_stat[i].level = stats[0] & MMR_SIGNAL_LVL;
	lp->spy_stat[i].noise = stats[1] & MMR_SILENCE_LVL;
	lp->spy_stat[i].updated = 0x7;
      }
}
#endif	/* WIRELESS_SPY */

#ifdef HISTOGRAM
/*------------------------------------------------------------------*/
/*
 * This function calculates a histogram of the signal level.
 * As the noise is quite constant, it's like doing it on the SNR.
 * We have defined a set of interval (lp->his_range), and each time
 * the level goes in that interval, we increment the count (lp->his_sum).
 * With this histogram you may detect if one WaveLAN is really weak,
 * or you may also calculate the mean and standard deviation of the level.
 */
static inline void
wl_his_gather(device *	dev,
	      u_char *	stats)		/* Statistics to gather */
{
  net_local *	lp = (net_local *) dev->priv;
  u_char	level = stats[0] & MMR_SIGNAL_LVL;
  int		i;

  /* Find the correct interval. */
  i = 0;
  while((i < (lp->his_number - 1)) && (level >= lp->his_range[i++]))
    ;

  /* Increment interval counter. */
  (lp->his_sum[i])++;
}
#endif	/* HISTOGRAM */

/*------------------------------------------------------------------*/
/*
 * Perform ioctl for configuration and information.
 * It is here that the wireless extensions are treated (iwconfig).
 */
static int
wavelan_ioctl(struct net_device *	dev,	/* device on which the ioctl is applied */
	      struct ifreq *	rq,	/* data passed */
	      int		cmd)	/* ioctl number */
{
  u_long		ioaddr = dev->base_addr;
  net_local *		lp = (net_local *)dev->priv;	/* lp is not unused */
  struct iwreq *	wrq = (struct iwreq *) rq;
  psa_t			psa;
  mm_t			m;
  unsigned long		x;
  int			ret = 0;

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_ioctl(cmd=0x%X)\n", dev->name, cmd);
#endif

  /* Disable interrupts and save flags. */
  x = wv_splhi();

  /* Look what is the request */
  switch(cmd)
    {
      /* --------------- WIRELESS EXTENSIONS --------------- */

    case SIOCGIWNAME:
      strcpy(wrq->u.name, "WaveLAN");
      break;

    case SIOCSIWNWID:
      /* Set NWID in WaveLAN. */
      if(wrq->u.nwid.on)
	{
	  /* Set NWID in psa. */
	  psa.psa_nwid[0] = (wrq->u.nwid.nwid & 0xFF00) >> 8;
	  psa.psa_nwid[1] = wrq->u.nwid.nwid & 0xFF;
	  psa.psa_nwid_select = 0x01;
	  psa_write(ioaddr, lp->hacr, (char *)psa.psa_nwid - (char *)&psa,
		    (unsigned char *)psa.psa_nwid, 3);

	  /* Set NWID in mmc. */
	  m.w.mmw_netw_id_l = wrq->u.nwid.nwid & 0xFF;
	  m.w.mmw_netw_id_h = (wrq->u.nwid.nwid & 0xFF00) >> 8;
	  mmc_write(ioaddr, (char *)&m.w.mmw_netw_id_l - (char *)&m,
		    (unsigned char *)&m.w.mmw_netw_id_l, 2);
	  mmc_out(ioaddr, mmwoff(0, mmw_loopt_sel), 0x00);
	}
      else
	{
	  /* Disable NWID in the psa. */
	  psa.psa_nwid_select = 0x00;
	  psa_write(ioaddr, lp->hacr,
		    (char *)&psa.psa_nwid_select - (char *)&psa,
		    (unsigned char *)&psa.psa_nwid_select, 1);

	  /* Disable NWID in the mmc (no filtering). */
	  mmc_out(ioaddr, mmwoff(0, mmw_loopt_sel), MMW_LOOPT_SEL_DIS_NWID);
	}
      /* update the Wavelan checksum */
      update_psa_checksum(dev, ioaddr, lp->hacr);
      break;

    case SIOCGIWNWID:
      /* Read the NWID. */
      psa_read(ioaddr, lp->hacr, (char *)psa.psa_nwid - (char *)&psa,
	       (unsigned char *)psa.psa_nwid, 3);
      wrq->u.nwid.nwid = (psa.psa_nwid[0] << 8) + psa.psa_nwid[1];
      wrq->u.nwid.on = psa.psa_nwid_select;
      break;

    case SIOCSIWFREQ:
      /* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable). */
      if(!(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) &
	   (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
	ret = wv_set_frequency(ioaddr, &(wrq->u.freq));
      else
	ret = -EOPNOTSUPP;
      break;

    case SIOCGIWFREQ:
      /* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable).
       * Does it work for everybody, especially old cards? */
      if(!(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) &
	   (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
	{
	  unsigned short	freq;

	  /* Ask the EEPROM to read the frequency from the first area. */
	  fee_read(ioaddr, 0x00, &freq, 1);
	  wrq->u.freq.m = ((freq >> 5) * 5 + 24000L) * 10000;
	  wrq->u.freq.e = 1;
	}
      else
	{
	  psa_read(ioaddr, lp->hacr, (char *)&psa.psa_subband - (char *)&psa,
		   (unsigned char *)&psa.psa_subband, 1);

	  if(psa.psa_subband <= 4)
	    {
	      wrq->u.freq.m = fixed_bands[psa.psa_subband];
	      wrq->u.freq.e = (psa.psa_subband != 0);
	    }
	  else
	    ret = -EOPNOTSUPP;
	}
      break;

    case SIOCSIWSENS:
      /* Set the level threshold. */
      /* We should complain loudly if wrq->u.sens.fixed = 0, because we
       * can't set auto mode... */
      psa.psa_thr_pre_set = wrq->u.sens.value & 0x3F;
      psa_write(ioaddr, lp->hacr, (char *)&psa.psa_thr_pre_set - (char *)&psa,
	       (unsigned char *) &psa.psa_thr_pre_set, 1);
      /* update the Wavelan checksum */
      update_psa_checksum(dev, ioaddr, lp->hacr);
      mmc_out(ioaddr, mmwoff(0, mmw_thr_pre_set), psa.psa_thr_pre_set);
      break;

    case SIOCGIWSENS:
      /* Read the level threshold. */
      psa_read(ioaddr, lp->hacr, (char *)&psa.psa_thr_pre_set - (char *)&psa,
	       (unsigned char *) &psa.psa_thr_pre_set, 1);
      wrq->u.sens.value = psa.psa_thr_pre_set & 0x3F;
      wrq->u.sens.fixed = 1;
      break;

     case SIOCSIWENCODE:
       /* Set encryption key. */
       if(!mmc_encr(ioaddr))
	 {
	   ret = -EOPNOTSUPP;
	   break;
	 }

       if(wrq->u.encoding.method)
	 {	/* Enable encryption. */
	   int		i;
	   long long	key = wrq->u.encoding.code;

	   for(i = 7; i >= 0; i--)
	     {
	       psa.psa_encryption_key[i] = key & 0xFF;
	       key >>= 8;
	     }
           psa.psa_encryption_select = 1;
	   psa_write(ioaddr, lp->hacr,
		     (char *) &psa.psa_encryption_select - (char *) &psa,
		     (unsigned char *) &psa.psa_encryption_select, 8+1);

           mmc_out(ioaddr, mmwoff(0, mmw_encr_enable),
		   MMW_ENCR_ENABLE_EN | MMW_ENCR_ENABLE_MODE);
           mmc_write(ioaddr, mmwoff(0, mmw_encr_key),
		     (unsigned char *) &psa.psa_encryption_key, 8);
	 }
       else
	 {	/* Disable encryption. */
	   psa.psa_encryption_select = 0;
	   psa_write(ioaddr, lp->hacr,
		     (char *) &psa.psa_encryption_select - (char *) &psa,
		     (unsigned char *) &psa.psa_encryption_select, 1);

	   mmc_out(ioaddr, mmwoff(0, mmw_encr_enable), 0);
	 }
       /* update the Wavelan checksum */
       update_psa_checksum(dev, ioaddr, lp->hacr);
       break;

     case SIOCGIWENCODE:
       /* Read the encryption key. */
       if(!mmc_encr(ioaddr))
	 {
	   ret = -EOPNOTSUPP;
	   break;
	 }

       /* Only super-user can see encryption key. */
       if(!suser())
	 {
	   ret = -EPERM;
	   break;
	 }
       else
	 {
	   int		i;
	   long long	key = 0;

	   psa_read(ioaddr, lp->hacr,
		    (char *) &psa.psa_encryption_select - (char *) &psa,
		    (unsigned char *) &psa.psa_encryption_select, 1+8);
	   for(i = 0; i < 8; i++)
	     {
	       key <<= 8;
	       key += psa.psa_encryption_key[i];
	     }
	   wrq->u.encoding.code = key;

	   /* encryption is enabled */
	   if(psa.psa_encryption_select)
	     wrq->u.encoding.method = mmc_encr(ioaddr);
	   else
	     wrq->u.encoding.method = 0;
	 }
       break;

    case SIOCGIWRANGE:
      /* basic checking */
      if(wrq->u.data.pointer != (caddr_t) 0)
	{
	  struct iw_range	range;

	  /* Set the length (useless:  it's constant). */
	  wrq->u.data.length = sizeof(struct iw_range);

	  /* Set information in the range struct.  */
	  range.throughput = 1.6 * 1000 * 1000;	/* don't argue on this ! */
	  range.min_nwid = 0x0000;
	  range.max_nwid = 0xFFFF;

	  /* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable). */
	  if(!(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) &
	       (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
	    {
	      range.num_channels = 10;
	      range.num_frequency = wv_frequency_list(ioaddr, range.freq,
						      IW_MAX_FREQUENCIES);
	    }
	  else
	    range.num_channels = range.num_frequency = 0;

	  range.sensitivity = 0x3F;
	  range.max_qual.qual = MMR_SGNL_QUAL;
	  range.max_qual.level = MMR_SIGNAL_LVL;
	  range.max_qual.noise = MMR_SILENCE_LVL;

	  range.num_bitrates = 1;
	  range.bitrate[0] = 2000000;	/* 2 Mb/s */

	  /* Copy structure to the user buffer. */
	  if (copy_to_user(wrq->u.data.pointer, &range, sizeof(struct iw_range)))
	  	ret = -EFAULT;
	}
      break;

    case SIOCGIWPRIV:
      /* Basic checking */
      if(wrq->u.data.pointer != (caddr_t) 0)
	{
	  struct iw_priv_args	priv[] =
	  {	/* cmd,		set_args,	get_args,	name */
	    { SIOCSIPQTHR, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, 0, "setqualthr" },
	    { SIOCGIPQTHR, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, "getqualthr" },

	    { SIOCSIPHISTO, IW_PRIV_TYPE_BYTE | 16,	0, "sethisto" },
	    { SIOCGIPHISTO, 0,	    IW_PRIV_TYPE_INT | 16, "gethisto" },
	  };

	  /* Set the number of available ioctls. */
	  wrq->u.data.length = 4;

	  /* Copy structure to the user buffer. */
	  if (copy_to_user(wrq->u.data.pointer, (u_char *) priv, sizeof(priv)))
	  	ret = -EFAULT;
	}
      break;

#ifdef WIRELESS_SPY
    case SIOCSIWSPY:
      /* Set the spy list */

      /* Check the number of addresses. */
      if(wrq->u.data.length > IW_MAX_SPY)
	{
	  ret = -E2BIG;
	  break;
	}
      lp->spy_number = wrq->u.data.length;

      /* Are there are addresses to copy? */
      if(lp->spy_number > 0)
	{
	  struct sockaddr	address[IW_MAX_SPY];
	  int			i;

	  /* Copy addresses to the driver. */
	  if (copy_from_user(address, wrq->u.data.pointer, sizeof(struct sockaddr) * lp->spy_number)) {
	  	ret = -EFAULT;
	  	break;
	  }

	  /* Copy addresses to the lp structure. */
	  for(i = 0; i < lp->spy_number; i++)
	    {
	      memcpy(lp->spy_address[i], address[i].sa_data,
		     WAVELAN_ADDR_SIZE);
	    }

	  /* Reset structure. */
	  memset(lp->spy_stat, 0x00, sizeof(iw_qual) * IW_MAX_SPY);

#ifdef DEBUG_IOCTL_INFO
	  printk(KERN_DEBUG "SetSpy:  set of new addresses is: \n");
	  for(i = 0; i < wrq->u.data.length; i++)
	    printk(KERN_DEBUG "%02X:%02X:%02X:%02X:%02X:%02X \n",
		   lp->spy_address[i][0],
		   lp->spy_address[i][1],
		   lp->spy_address[i][2],
		   lp->spy_address[i][3],
		   lp->spy_address[i][4],
		   lp->spy_address[i][5]);
#endif	/* DEBUG_IOCTL_INFO */
	}

      break;

    case SIOCGIWSPY:
      /* Get the spy list and spy stats. */

      /* Set the number of addresses */
      wrq->u.data.length = lp->spy_number;

      /* Does the user want to have the addresses back? */
      if((lp->spy_number > 0) && (wrq->u.data.pointer != (caddr_t) 0))
	{
	  struct sockaddr	address[IW_MAX_SPY];
	  int			i;

	  /* Copy addresses from the lp structure. */
	  for(i = 0; i < lp->spy_number; i++)
	    {
	      memcpy(address[i].sa_data, lp->spy_address[i],
		     WAVELAN_ADDR_SIZE);
	      address[i].sa_family = AF_UNIX;
	    }

	  /* Copy addresses to the user buffer. */
	  if (copy_to_user(wrq->u.data.pointer, address, sizeof(struct sockaddr) * lp->spy_number)) {
	  	ret = -EFAULT;
	  	break;
	  }
	  	
	  /* Copy stats to the user buffer (just after). */
	  if (copy_to_user(wrq->u.data.pointer +
		       (sizeof(struct sockaddr) * lp->spy_number),
		       lp->spy_stat, sizeof(iw_qual) * lp->spy_number)) {
		       		ret = -EFAULT;
		       		break;
	  }

	  /* Reset updated flags. */
	  for(i = 0; i < lp->spy_number; i++)
	    lp->spy_stat[i].updated = 0x0;
	}	/* if(pointer != NULL) */

      break;
#endif	/* WIRELESS_SPY */

      /* ------------------ PRIVATE IOCTL ------------------ */

    case SIOCSIPQTHR:
      if(!suser())
        {
	  ret = -EPERM;
	  break;
	}
      psa.psa_quality_thr = *(wrq->u.name) & 0x0F;
      psa_write(ioaddr, lp->hacr, (char *)&psa.psa_quality_thr - (char *)&psa,
	       (unsigned char *)&psa.psa_quality_thr, 1);
      /* update the Wavelan checksum */
      update_psa_checksum(dev, ioaddr, lp->hacr);
      mmc_out(ioaddr, mmwoff(0, mmw_quality_thr), psa.psa_quality_thr);
      break;

    case SIOCGIPQTHR:
      psa_read(ioaddr, lp->hacr, (char *)&psa.psa_quality_thr - (char *)&psa,
	       (unsigned char *)&psa.psa_quality_thr, 1);
      *(wrq->u.name) = psa.psa_quality_thr & 0x0F;
      break;

#ifdef HISTOGRAM
    case SIOCSIPHISTO:
      /* Verify that the user is root. */
      if(!suser())
        {
	  ret = -EPERM;
	  break;
	}

      /* Check the number of intervals. */
      if(wrq->u.data.length > 16)
	{
	  ret = -E2BIG;
	  break;
	}
      lp->his_number = wrq->u.data.length;

      /* Are there addresses to copy? */
      if(lp->his_number > 0)
	{
	  /* Copy interval ranges to the driver */
	  if (copy_from_user(lp->his_range, wrq->u.data.pointer, sizeof(char) * lp->his_number)) {
	  	ret = -EFAULT;
	  	break;
	  }

	  /* Reset structure. */
	  memset(lp->his_sum, 0x00, sizeof(long) * 16);
	}
      break;

    case SIOCGIPHISTO:
      /* Set the number of intervals. */
      wrq->u.data.length = lp->his_number;

      /* Give back the distribution statistics */
      if((lp->his_number > 0) && (wrq->u.data.pointer != (caddr_t) 0))
	{
	  /* Copy data to the user buffer. */
	  if (copy_to_user(wrq->u.data.pointer, lp->his_sum, sizeof(long) * lp->his_number)) 
			ret = -EFAULT;
			
	}	/* if(pointer != NULL) */
      break;
#endif	/* HISTOGRAM */

      /* ------------------- OTHER IOCTL ------------------- */

    default:
      ret = -EOPNOTSUPP;
    }

  /* Enable interrupts and restore flags. */
  wv_splx(x);

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_ioctl()\n", dev->name);
#endif
  return ret;
}

/*------------------------------------------------------------------*/
/*
 * Get wireless statistics.
 * Called by /proc/net/wireless
 */
static iw_stats *
wavelan_get_wireless_stats(device *	dev)
{
  u_long		ioaddr = dev->base_addr;
  net_local *		lp = (net_local *) dev->priv;
  mmr_t			m;
  iw_stats *		wstats;
  unsigned long		x;

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_get_wireless_stats()\n", dev->name);
#endif

  /* Disable interrupts and save flags. */
  x = wv_splhi();

  if(lp == (net_local *) NULL)
    return (iw_stats *) NULL;
  wstats = &lp->wstats;

  /* Get data from the mmc. */
  mmc_out(ioaddr, mmwoff(0, mmw_freeze), 1);

  mmc_read(ioaddr, mmroff(0, mmr_dce_status), &m.mmr_dce_status, 1);
  mmc_read(ioaddr, mmroff(0, mmr_wrong_nwid_l), &m.mmr_wrong_nwid_l, 2);
  mmc_read(ioaddr, mmroff(0, mmr_thr_pre_set), &m.mmr_thr_pre_set, 4);

  mmc_out(ioaddr, mmwoff(0, mmw_freeze), 0);

  /* Copy data to wireless stuff. */
  wstats->status = m.mmr_dce_status & MMR_DCE_STATUS;
  wstats->qual.qual = m.mmr_sgnl_qual & MMR_SGNL_QUAL;
  wstats->qual.level = m.mmr_signal_lvl & MMR_SIGNAL_LVL;
  wstats->qual.noise = m.mmr_silence_lvl & MMR_SILENCE_LVL;
  wstats->qual.updated = (((m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) >> 7) |
			  ((m.mmr_signal_lvl & MMR_SIGNAL_LVL_VALID) >> 6) |
			  ((m.mmr_silence_lvl & MMR_SILENCE_LVL_VALID) >> 5));
  wstats->discard.nwid += (m.mmr_wrong_nwid_h << 8) | m.mmr_wrong_nwid_l;
  wstats->discard.code = 0L;
  wstats->discard.misc = 0L;

  /* Enable interrupts and restore flags. */
  wv_splx(x);

#ifdef DEBUG_IOCTL_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_get_wireless_stats()\n", dev->name);
#endif
  return &lp->wstats;
}
#endif	/* WIRELESS_EXT */

/************************* PACKET RECEPTION *************************/
/*
 * This part deals with receiving the packets.
 * The interrupt handler gets an interrupt when a packet has been
 * successfully received and calls this part.
 */

/*------------------------------------------------------------------*/
/*
 * This routine does the actual copying of data (including the Ethernet
 * header structure) from the WaveLAN card to an sk_buff chain that
 * will be passed up to the network interface layer. NOTE: we
 * currently don't handle trailer protocols (neither does the rest of
 * the network interface), so if that is needed, it will (at least in
 * part) be added here.  The contents of the receive ring buffer are
 * copied to a message chain that is then passed to the kernel.
 *
 * Note: if any errors occur, the packet is "dropped on the floor".
 * (called by wv_packet_rcv())
 */
static inline void
wv_packet_read(device *		dev,
	       u_short		buf_off,
	       int		sksize)
{
  net_local *		lp = (net_local *) dev->priv;
  u_long		ioaddr = dev->base_addr;
  struct sk_buff *	skb;

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: ->wv_packet_read(0x%X, %d)\n",
	 dev->name, buf_off, sksize);
#endif

  /* Allocate buffer for the data */
  if((skb = dev_alloc_skb(sksize)) == (struct sk_buff *) NULL)
    {
#ifdef DEBUG_RX_ERROR
      printk(KERN_INFO "%s: wv_packet_read(): could not alloc_skb(%d, GFP_ATOMIC).\n",
	     dev->name, sksize);
#endif
      lp->stats.rx_dropped++;
      return;
    }

  skb->dev = dev;

  /* Copy the packet to the buffer. */
  obram_read(ioaddr, buf_off, skb_put(skb, sksize), sksize);
  skb->protocol=eth_type_trans(skb, dev);

#ifdef DEBUG_RX_INFO
  wv_packet_info(skb->mac.raw, sksize, dev->name, "wv_packet_read");
#endif	/* DEBUG_RX_INFO */

  /* Statistics-gathering and associated stuff.
   * It seem a bit messy with all the define, but it's really simple... */
#if defined(WIRELESS_SPY) || defined(HISTOGRAM)
  if(
#ifdef WIRELESS_SPY
     (lp->spy_number > 0) ||
#endif	/* WIRELESS_SPY */
#ifdef HISTOGRAM
     (lp->his_number > 0) ||
#endif	/* HISTOGRAM */
     0)
    {
      u_char	stats[3];	/* signal level, noise level, signal quality */

      /* Read signal level, silence level and signal quality bytes. */
      /* Note: in the PCMCIA hardware, these are part of the frame.  It seems
       * that for the ISA hardware, it's nowhere to be found in the frame,
       * so I'm obliged to do this (it has a side effect on /proc/net/wireless).
       * Any ideas?
       */
      mmc_out(ioaddr, mmwoff(0, mmw_freeze), 1);
      mmc_read(ioaddr, mmroff(0, mmr_signal_lvl), stats, 3);
      mmc_out(ioaddr, mmwoff(0, mmw_freeze), 0);

#ifdef DEBUG_RX_INFO
      printk(KERN_DEBUG "%s: wv_packet_read(): Signal level %d/63, Silence level %d/63, signal quality %d/16\n",
	     dev->name, stats[0] & 0x3F, stats[1] & 0x3F, stats[2] & 0x0F);
#endif

      /* Spying stuff */
#ifdef WIRELESS_SPY
      wl_spy_gather(dev, skb->mac.raw + WAVELAN_ADDR_SIZE, stats);
#endif	/* WIRELESS_SPY */
#ifdef HISTOGRAM
      wl_his_gather(dev, stats);
#endif	/* HISTOGRAM */
    }
#endif	/* defined(WIRELESS_SPY) || defined(HISTOGRAM) */

  /*
   * Hand the packet to the network module.
   */
  netif_rx(skb);

  /* Keep statistics up to date */
  lp->stats.rx_packets++;
  lp->stats.rx_bytes += skb->len;

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: <-wv_packet_read()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * Transfer as many packets as we can
 * from the device RAM.
 * Called by the interrupt handler.
 */
static inline void
wv_receive(device *	dev)
{
  u_long	ioaddr = dev->base_addr;
  net_local *	lp = (net_local *)dev->priv;
  fd_t		fd;
  rbd_t		rbd;
  int		nreaped = 0;

#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: ->wv_receive()\n", dev->name);
#endif

  /* Loop on each received packet. */
  for(;;)
    {
      obram_read(ioaddr, lp->rx_head, (unsigned char *) &fd, sizeof(fd));

      /* Note about the status :
       * It start up to be 0 (the value we set). Then, when the RU
       * grab the buffer to prepare for reception, it sets the
       * FD_STATUS_B flag. When the RU has finished receiving the
       * frame, it clears FD_STATUS_B, set FD_STATUS_C to indicate
       * completion and set the other flags to indicate the eventual
       * errors. FD_STATUS_OK indicates that the reception was OK.
       */

      /* If the current frame is not complete, we have reached the end. */
      if((fd.fd_status & FD_STATUS_C) != FD_STATUS_C)
	break;		/* This is how we exit the loop. */

      nreaped++;

      /* Check whether frame was correctly received. */
      if((fd.fd_status & FD_STATUS_OK) == FD_STATUS_OK)
	{
	  /* Does the frame contain a pointer to the data?  Let's check. */
	  if(fd.fd_rbd_offset != I82586NULL)
	    {
	      /* Read the receive buffer descriptor */
	      obram_read(ioaddr, fd.fd_rbd_offset,
			 (unsigned char *) &rbd, sizeof(rbd));

#ifdef DEBUG_RX_ERROR
	      if((rbd.rbd_status & RBD_STATUS_EOF) != RBD_STATUS_EOF)
		printk(KERN_INFO "%s: wv_receive(): missing EOF flag.\n",
		       dev->name);

	      if((rbd.rbd_status & RBD_STATUS_F) != RBD_STATUS_F)
		printk(KERN_INFO "%s: wv_receive(): missing F flag.\n",
		       dev->name);
#endif	/* DEBUG_RX_ERROR */

	      /* Read the packet and transmit to Linux */
	      wv_packet_read(dev, rbd.rbd_bufl,
			     rbd.rbd_status & RBD_STATUS_ACNT);
	    }
#ifdef DEBUG_RX_ERROR
	  else	/* if frame has no data */
	    printk(KERN_INFO "%s: wv_receive(): frame has no data.\n",
		   dev->name);
#endif
	}
      else	/* If reception was no successful */
	{
	  lp->stats.rx_errors++;

#ifdef DEBUG_RX_INFO
	  printk(KERN_DEBUG "%s: wv_receive(): frame not received successfully (%X).\n",
		 dev->name, fd.fd_status);
#endif

#ifdef DEBUG_RX_ERROR
	  if((fd.fd_status & FD_STATUS_S6) != 0)
	    printk(KERN_INFO "%s: wv_receive(): no EOF flag.\n", dev->name);
#endif

	  if((fd.fd_status & FD_STATUS_S7) != 0)
	    {
	      lp->stats.rx_length_errors++;
#ifdef DEBUG_RX_FAIL
	      printk(KERN_DEBUG "%s: wv_receive(): frame too short.\n",
		     dev->name);
#endif
	    }

	  if((fd.fd_status & FD_STATUS_S8) != 0)
	    {
	      lp->stats.rx_over_errors++;
#ifdef DEBUG_RX_FAIL
	      printk(KERN_DEBUG "%s: wv_receive(): rx DMA overrun.\n",
		     dev->name);
#endif
	    }

	  if((fd.fd_status & FD_STATUS_S9) != 0)
	    {
	      lp->stats.rx_fifo_errors++;
#ifdef DEBUG_RX_FAIL
	      printk(KERN_DEBUG "%s: wv_receive(): ran out of resources.\n",
		     dev->name);
#endif
	    }

	  if((fd.fd_status & FD_STATUS_S10) != 0)
	    {
	      lp->stats.rx_frame_errors++;
#ifdef DEBUG_RX_FAIL
	      printk(KERN_DEBUG "%s: wv_receive(): alignment error.\n",
		     dev->name);
#endif
	    }

	  if((fd.fd_status & FD_STATUS_S11) != 0)
	    {
	      lp->stats.rx_crc_errors++;
#ifdef DEBUG_RX_FAIL
	      printk(KERN_DEBUG "%s: wv_receive(): CRC error.\n", dev->name);
#endif
	    }
	}

      fd.fd_status = 0;
      obram_write(ioaddr, fdoff(lp->rx_head, fd_status),
		  (unsigned char *) &fd.fd_status, sizeof(fd.fd_status));

      fd.fd_command = FD_COMMAND_EL;
      obram_write(ioaddr, fdoff(lp->rx_head, fd_command),
		  (unsigned char *) &fd.fd_command, sizeof(fd.fd_command));

      fd.fd_command = 0;
      obram_write(ioaddr, fdoff(lp->rx_last, fd_command),
		  (unsigned char *) &fd.fd_command, sizeof(fd.fd_command));

      lp->rx_last = lp->rx_head;
      lp->rx_head = fd.fd_link_offset;
    }	/* for(;;) -> loop on all frames */

#ifdef DEBUG_RX_INFO
  if(nreaped > 1)
    printk(KERN_DEBUG "%s: wv_receive(): reaped %d\n", dev->name, nreaped);
#endif
#ifdef DEBUG_RX_TRACE
  printk(KERN_DEBUG "%s: <-wv_receive()\n", dev->name);
#endif
}

/*********************** PACKET TRANSMISSION ***********************/
/*
 * This part deals with sending packets through the WaveLAN.
 *
 */

/*------------------------------------------------------------------*/
/*
 * This routine fills in the appropriate registers and memory
 * locations on the WaveLAN card and starts the card off on
 * the transmit.
 *
 * The principle:
 * Each block contains a transmit command, a NOP command,
 * a transmit block descriptor and a buffer.
 * The CU read the transmit block which point to the tbd,
 * read the tbd and the content of the buffer.
 * When it has finish with it, it goes to the next command
 * which in our case is the NOP. The NOP points on itself,
 * so the CU stop here.
 * When we add the next block, we modify the previous nop
 * to make it point on the new tx command.
 * Simple, isn't it ?
 *
 * (called in wavelan_packet_xmit())
 */
static inline void
wv_packet_write(device *	dev,
		void *	buf,
		short	length)
{
  net_local *		lp = (net_local *) dev->priv;
  u_long		ioaddr = dev->base_addr;
  unsigned short	txblock;
  unsigned short	txpred;
  unsigned short	tx_addr;
  unsigned short	nop_addr;
  unsigned short	tbd_addr;
  unsigned short	buf_addr;
  ac_tx_t		tx;
  ac_nop_t		nop;
  tbd_t			tbd;
  int			clen = length;
  unsigned long		x;

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: ->wv_packet_write(%d)\n", dev->name, length);
#endif

  /* Do we need some padding? */
  if(clen < ETH_ZLEN)
    clen = ETH_ZLEN;

  x = wv_splhi();

  /* Calculate addresses of next block and previous block. */
  txblock = lp->tx_first_free;
  txpred = txblock - TXBLOCKZ;
  if(txpred < OFFSET_CU)
    txpred += NTXBLOCKS * TXBLOCKZ;
  lp->tx_first_free += TXBLOCKZ;
  if(lp->tx_first_free >= OFFSET_CU + NTXBLOCKS * TXBLOCKZ)
    lp->tx_first_free -= NTXBLOCKS * TXBLOCKZ;

/*
if (lp->tx_n_in_use > 0)
	printk("%c", "0123456789abcdefghijk"[lp->tx_n_in_use]);
*/

  lp->tx_n_in_use++;

  /* Calculate addresses of the different parts of the block. */
  tx_addr = txblock;
  nop_addr = tx_addr + sizeof(tx);
  tbd_addr = nop_addr + sizeof(nop);
  buf_addr = tbd_addr + sizeof(tbd);

  /*
   * Transmit command
   */
  tx.tx_h.ac_status = 0;
  obram_write(ioaddr, toff(ac_tx_t, tx_addr, tx_h.ac_status),
	      (unsigned char *) &tx.tx_h.ac_status,
	      sizeof(tx.tx_h.ac_status));

  /*
   * NOP command
   */
  nop.nop_h.ac_status = 0;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_status),
	      (unsigned char *) &nop.nop_h.ac_status,
	      sizeof(nop.nop_h.ac_status));
  nop.nop_h.ac_link = nop_addr;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_link),
	      (unsigned char *) &nop.nop_h.ac_link,
	      sizeof(nop.nop_h.ac_link));

  /*
   * Transmit buffer descriptor
   */
  tbd.tbd_status = TBD_STATUS_EOF | (TBD_STATUS_ACNT & clen);
  tbd.tbd_next_bd_offset = I82586NULL;
  tbd.tbd_bufl = buf_addr;
  tbd.tbd_bufh = 0;
  obram_write(ioaddr, tbd_addr, (unsigned char *)&tbd, sizeof(tbd));

  /*
   * Data
   */
  obram_write(ioaddr, buf_addr, buf, length);

  /*
   * Overwrite the predecessor NOP link
   * so that it points to this txblock.
   */
  nop_addr = txpred + sizeof(tx);
  nop.nop_h.ac_status = 0;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_status),
	      (unsigned char *)&nop.nop_h.ac_status,
	      sizeof(nop.nop_h.ac_status));
  nop.nop_h.ac_link = txblock;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_link),
	      (unsigned char *) &nop.nop_h.ac_link,
	      sizeof(nop.nop_h.ac_link));

  /* Keep stats up to date. */
  lp->stats.tx_bytes += length;

  /* If watchdog not already active, activate it... */
  if(lp->watchdog.prev == (timer_list *) NULL)
    {
      /* Set timer to expire in WATCHDOG_JIFFIES. */
      lp->watchdog.expires = jiffies + WATCHDOG_JIFFIES;
      add_timer(&lp->watchdog);
    }

  if(lp->tx_first_in_use == I82586NULL)
    lp->tx_first_in_use = txblock;

  if(lp->tx_n_in_use < NTXBLOCKS - 1)
    dev->tbusy = 0;

  wv_splx(x);

#ifdef DEBUG_TX_INFO
  wv_packet_info((u_char *) buf, length, dev->name, "wv_packet_write");
#endif	/* DEBUG_TX_INFO */

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: <-wv_packet_write()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * This routine is called when we want to send a packet (NET3 callback)
 * In this routine, we check if the harware is ready to accept
 * the packet.  We also prevent reentrance.  Then we call the function
 * to send the packet.
 */
static int
wavelan_packet_xmit(struct sk_buff *	skb,
		    device *		dev)
{
  net_local *	lp = (net_local *)dev->priv;

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_packet_xmit(0x%X)\n", dev->name,
	 (unsigned) skb);
#endif

  /* This flag indicate that the hardware can't perform a transmission.
   * Theoretically, NET3 checks it before sending a packet to the driver,
   * but in fact it never does that and pools continuously.
   * As the watchdog will abort overly long transmissions, we are quite safe.
   */
  if(dev->tbusy)
    return 1;

  /*
   * Block a timer-based transmit from overlapping.
   * In other words, prevent reentering this routine.
   */
  if(test_and_set_bit(0, (void *)&dev->tbusy) != 0)
#ifdef DEBUG_TX_ERROR
    printk(KERN_INFO "%s: Transmitter access conflict.\n", dev->name);
#endif
  else
    {
      /* If somebody has asked to reconfigure the controller, 
       * we can do it now.
       */
      if(lp->reconfig_82586)
	{
	  wv_82586_config(dev);
	  if(dev->tbusy)
	    return 1;
	}

#ifdef DEBUG_TX_ERROR
      if(skb->next)
	printk(KERN_INFO "skb has next\n");
#endif

      wv_packet_write(dev, skb->data, skb->len);
    }

  dev_kfree_skb(skb);

#ifdef DEBUG_TX_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_packet_xmit()\n", dev->name);
#endif
  return 0;
}

/*********************** HARDWARE CONFIGURATION ***********************/
/*
 * This part does the real job of starting and configuring the hardware.
 */

/*--------------------------------------------------------------------*/
/*
 * Routine to initialize the Modem Management Controller.
 * (called by wv_hw_reset())
 */
static inline int
wv_mmc_init(device *	dev)
{
  u_long	ioaddr = dev->base_addr;
  net_local *	lp = (net_local *)dev->priv;
  psa_t		psa;
  mmw_t		m;
  int		configured;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_mmc_init()\n", dev->name);
#endif

  /* Read the parameter storage area. */
  psa_read(ioaddr, lp->hacr, 0, (unsigned char *) &psa, sizeof(psa));

#ifdef USE_PSA_CONFIG
  configured = psa.psa_conf_status & 1;
#else
  configured = 0;
#endif

  /* Is the PSA is not configured */
  if(!configured)
    {
      /* User will be able to configure NWID later (with iwconfig). */
      psa.psa_nwid[0] = 0;
      psa.psa_nwid[1] = 0;

      /* no NWID checking since NWID is not set */
      psa.psa_nwid_select = 0;

      /* Disable encryption */
      psa.psa_encryption_select = 0;

      /* Set to standard values:
       * 0x04 for AT,
       * 0x01 for MCA,
       * 0x04 for PCMCIA and 2.00 card (AT&T 407-024689/E document)
       */
      if (psa.psa_comp_number & 1)
	psa.psa_thr_pre_set = 0x01;
      else
	psa.psa_thr_pre_set = 0x04;
      psa.psa_quality_thr = 0x03;

      /* It is configured */
      psa.psa_conf_status |= 1;

#ifdef USE_PSA_CONFIG
      /* Write the psa. */
      psa_write(ioaddr, lp->hacr, (char *)psa.psa_nwid - (char *)&psa,
		(unsigned char *)psa.psa_nwid, 4);
      psa_write(ioaddr, lp->hacr, (char *)&psa.psa_thr_pre_set - (char *)&psa,
		(unsigned char *)&psa.psa_thr_pre_set, 1);
      psa_write(ioaddr, lp->hacr, (char *)&psa.psa_quality_thr - (char *)&psa,
		(unsigned char *)&psa.psa_quality_thr, 1);
      psa_write(ioaddr, lp->hacr, (char *)&psa.psa_conf_status - (char *)&psa,
		(unsigned char *)&psa.psa_conf_status, 1);
      /* update the Wavelan checksum */
      update_psa_checksum(dev, ioaddr, lp->hacr);
#endif
    }

  /* Zero the mmc structure. */
  memset(&m, 0x00, sizeof(m));

  /* Copy PSA info to the mmc. */
  m.mmw_netw_id_l = psa.psa_nwid[1];
  m.mmw_netw_id_h = psa.psa_nwid[0];
  
  if(psa.psa_nwid_select & 1)
    m.mmw_loopt_sel = 0x00;
  else
    m.mmw_loopt_sel = MMW_LOOPT_SEL_DIS_NWID;

  memcpy(&m.mmw_encr_key, &psa.psa_encryption_key, 
	 sizeof(m.mmw_encr_key));

  if(psa.psa_encryption_select)
    m.mmw_encr_enable = MMW_ENCR_ENABLE_EN | MMW_ENCR_ENABLE_MODE;
  else
    m.mmw_encr_enable = 0;

  m.mmw_thr_pre_set = psa.psa_thr_pre_set & 0x3F;
  m.mmw_quality_thr = psa.psa_quality_thr & 0x0F;

  /*
   * Set default modem control parameters.
   * See NCR document 407-0024326 Rev. A.
   */
  m.mmw_jabber_enable = 0x01;
  m.mmw_freeze = 0;
  m.mmw_anten_sel = MMW_ANTEN_SEL_ALG_EN;
  m.mmw_ifs = 0x20;
  m.mmw_mod_delay = 0x04;
  m.mmw_jam_time = 0x38;

  m.mmw_des_io_invert = 0;
  m.mmw_decay_prm = 0;
  m.mmw_decay_updat_prm = 0;

  /* Write all info to MMC. */
  mmc_write(ioaddr, 0, (u_char *)&m, sizeof(m));

  /* The following code starts the modem of the 2.00 frequency
   * selectable cards at power on.  It's not strictly needed for the
   * following boots.
   * The original patch was by Joe Finney for the PCMCIA driver, but
   * I've cleaned it up a bit and added documentation.
   * Thanks to Loeke Brederveld from Lucent for the info.
   */

  /* Attempt to recognise 2.00 cards (2.4 GHz frequency selectable)
   * Does it work for everybody, especially old cards? */
  /* Note: WFREQSEL verifies that it is able to read a sensible
   * frequency from EEPROM (address 0x00) and that MMR_FEE_STATUS_ID
   * is 0xA (Xilinx version) or 0xB (Ariadne version).
   * My test is more crude but does work. */
  if(!(mmc_in(ioaddr, mmroff(0, mmr_fee_status)) &
       (MMR_FEE_STATUS_DWLD | MMR_FEE_STATUS_BUSY)))
    {
      /* We must download the frequency parameters to the
       * synthesizers (from the EEPROM - area 1)
       * Note: as the EEPROM is automatically decremented, we set the end
       * if the area... */
      m.mmw_fee_addr = 0x0F;
      m.mmw_fee_ctrl = MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD;
      mmc_write(ioaddr, (char *)&m.mmw_fee_ctrl - (char *)&m,
		(unsigned char *)&m.mmw_fee_ctrl, 2);

      /* Wait until the download is finished. */
      fee_wait(ioaddr, 100, 100);

#ifdef DEBUG_CONFIG_INFO
      /* The frequency was in the last word downloaded. */
      mmc_read(ioaddr, (char *)&m.mmw_fee_data_l - (char *)&m,
	       (unsigned char *)&m.mmw_fee_data_l, 2);

      /* Print some info for the user. */
      printk(KERN_DEBUG "%s: WaveLAN 2.00 recognised (frequency select).  Current frequency = %ld\n",
	     dev->name,
	     ((m.mmw_fee_data_h << 4) |
	      (m.mmw_fee_data_l >> 4)) * 5 / 2 + 24000L);
#endif

      /* We must now download the power adjust value (gain) to
       * the synthesizers (from the EEPROM - area 7 - DAC). */
      m.mmw_fee_addr = 0x61;
      m.mmw_fee_ctrl = MMW_FEE_CTRL_READ | MMW_FEE_CTRL_DWLD;
      mmc_write(ioaddr, (char *)&m.mmw_fee_ctrl - (char *)&m,
		(unsigned char *)&m.mmw_fee_ctrl, 2);

      /* Wait until the download is finished. */
    }	/* if 2.00 card */

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_mmc_init()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Construct the fd and rbd structures.
 * Start the receive unit.
 * (called by wv_hw_reset())
 */
static inline int
wv_ru_start(device *	dev)
{
  net_local *	lp = (net_local *) dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_short	scb_cs;
  fd_t		fd;
  rbd_t		rbd;
  u_short	rx;
  u_short	rx_next;
  int		i;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_ru_start()\n", dev->name);
#endif

  obram_read(ioaddr, scboff(OFFSET_SCB, scb_status), (unsigned char *)&scb_cs, sizeof(scb_cs));
  if((scb_cs & SCB_ST_RUS) == SCB_ST_RUS_RDY)
    return 0;

  lp->rx_head = OFFSET_RU;

  for(i = 0, rx = lp->rx_head; i < NRXBLOCKS; i++, rx = rx_next)
    {
      rx_next = (i == NRXBLOCKS - 1) ? lp->rx_head : rx + RXBLOCKZ;

      fd.fd_status = 0;
      fd.fd_command = (i == NRXBLOCKS - 1) ? FD_COMMAND_EL : 0;
      fd.fd_link_offset = rx_next;
      fd.fd_rbd_offset = rx + sizeof(fd);
      obram_write(ioaddr, rx, (unsigned char *)&fd, sizeof(fd));

      rbd.rbd_status = 0;
      rbd.rbd_next_rbd_offset = I82586NULL;
      rbd.rbd_bufl = rx + sizeof(fd) + sizeof(rbd);
      rbd.rbd_bufh = 0;
      rbd.rbd_el_size = RBD_EL | (RBD_SIZE & MAXDATAZ);
      obram_write(ioaddr, rx + sizeof(fd),
		  (unsigned char *) &rbd, sizeof(rbd));

      lp->rx_last = rx;
    }

  obram_write(ioaddr, scboff(OFFSET_SCB, scb_rfa_offset),
	      (unsigned char *) &lp->rx_head, sizeof(lp->rx_head));

  scb_cs = SCB_CMD_RUC_GO;
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *) &scb_cs, sizeof(scb_cs));

  set_chan_attn(ioaddr, lp->hacr);

  for(i = 1000; i > 0; i--)
    {
      obram_read(ioaddr, scboff(OFFSET_SCB, scb_command),
		 (unsigned char *) &scb_cs, sizeof(scb_cs));
      if (scb_cs == 0)
	break;

      udelay(10);
    }

  if(i <= 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wavelan_ru_start(): board not accepting command.\n",
	     dev->name);
#endif
      return -1;
    }

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_ru_start()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Initialise the transmit blocks.
 * Start the command unit executing the NOP
 * self-loop of the first transmit block.
 *
 * Here we create the list of send buffers used to transmit packets
 * between the PC and the command unit. For each buffer, we create a
 * buffer descriptor (pointing on the buffer), a transmit command
 * (pointing to the buffer descriptor) and a NOP command.
 * The transmit command is linked to the NOP, and the NOP to itself.
 * When we will have finished executing the transmit command, we will
 * then loop on the NOP. By releasing the NOP link to a new command,
 * we may send another buffer.
 *
 * (called by wv_hw_reset())
 */
static inline int
wv_cu_start(device *	dev)
{
  net_local *	lp = (net_local *) dev->priv;
  u_long	ioaddr = dev->base_addr;
  int		i;
  u_short	txblock;
  u_short	first_nop;
  u_short	scb_cs;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_cu_start()\n", dev->name);
#endif

  lp->tx_first_free = OFFSET_CU;
  lp->tx_first_in_use = I82586NULL;

  for(i = 0, txblock = OFFSET_CU;
      i < NTXBLOCKS;
      i++, txblock += TXBLOCKZ)
    {
      ac_tx_t		tx;
      ac_nop_t		nop;
      tbd_t		tbd;
      unsigned short	tx_addr;
      unsigned short	nop_addr;
      unsigned short	tbd_addr;
      unsigned short	buf_addr;

      tx_addr = txblock;
      nop_addr = tx_addr + sizeof(tx);
      tbd_addr = nop_addr + sizeof(nop);
      buf_addr = tbd_addr + sizeof(tbd);

      tx.tx_h.ac_status = 0;
      tx.tx_h.ac_command = acmd_transmit | AC_CFLD_I;
      tx.tx_h.ac_link = nop_addr;
      tx.tx_tbd_offset = tbd_addr;
      obram_write(ioaddr, tx_addr, (unsigned char *) &tx, sizeof(tx));

      nop.nop_h.ac_status = 0;
      nop.nop_h.ac_command = acmd_nop;
      nop.nop_h.ac_link = nop_addr;
      obram_write(ioaddr, nop_addr, (unsigned char *) &nop, sizeof(nop));

      tbd.tbd_status = TBD_STATUS_EOF;
      tbd.tbd_next_bd_offset = I82586NULL;
      tbd.tbd_bufl = buf_addr;
      tbd.tbd_bufh = 0;
      obram_write(ioaddr, tbd_addr, (unsigned char *) &tbd, sizeof(tbd));
    }

  first_nop = OFFSET_CU + (NTXBLOCKS - 1) * TXBLOCKZ + sizeof(ac_tx_t);
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_cbl_offset),
	      (unsigned char *) &first_nop, sizeof(first_nop));

  scb_cs = SCB_CMD_CUC_GO;
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *) &scb_cs, sizeof(scb_cs));

  set_chan_attn(ioaddr, lp->hacr);

  for(i = 1000; i > 0; i--)
    {
      obram_read(ioaddr, scboff(OFFSET_SCB, scb_command),
		 (unsigned char *) &scb_cs, sizeof(scb_cs));
      if (scb_cs == 0)
	break;

      udelay(10);
    }

  if(i <= 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wavelan_cu_start(): board not accepting command.\n",
	     dev->name);
#endif
      return -1;
    }

  lp->tx_n_in_use = 0;
  dev->tbusy = 0;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_cu_start()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * This routine does a standard configuration of the WaveLAN 
 * controller (i82586).
 *
 * It initialises the scp, iscp and scb structure
 * The first two are just pointers to the next.
 * The last one is used for basic configuration and for basic
 * communication (interrupt status).
 *
 * (called by wv_hw_reset())
 */
static inline int
wv_82586_start(device *	dev)
{
  net_local *	lp = (net_local *) dev->priv;
  u_long	ioaddr = dev->base_addr;
  scp_t		scp;		/* system configuration pointer */
  iscp_t	iscp;		/* intermediate scp */
  scb_t		scb;		/* system control block */
  ach_t		cb;		/* Action command header */
  u_char	zeroes[512];
  int		i;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_82586_start()\n", dev->name);
#endif

  /*
   * Clear the onboard RAM.
   */
  memset(&zeroes[0], 0x00, sizeof(zeroes));
  for(i = 0; i < I82586_MEMZ; i += sizeof(zeroes))
    obram_write(ioaddr, i, &zeroes[0], sizeof(zeroes));

  /*
   * Construct the command unit structures:
   * scp, iscp, scb, cb.
   */
  memset(&scp, 0x00, sizeof(scp));
  scp.scp_sysbus = SCP_SY_16BBUS;
  scp.scp_iscpl = OFFSET_ISCP;
  obram_write(ioaddr, OFFSET_SCP, (unsigned char *)&scp, sizeof(scp));

  memset(&iscp, 0x00, sizeof(iscp));
  iscp.iscp_busy = 1;
  iscp.iscp_offset = OFFSET_SCB;
  obram_write(ioaddr, OFFSET_ISCP, (unsigned char *)&iscp, sizeof(iscp));

  /* Our first command is to reset the i82586. */
  memset(&scb, 0x00, sizeof(scb));
  scb.scb_command = SCB_CMD_RESET;
  scb.scb_cbl_offset = OFFSET_CU;
  scb.scb_rfa_offset = OFFSET_RU;
  obram_write(ioaddr, OFFSET_SCB, (unsigned char *)&scb, sizeof(scb));

  set_chan_attn(ioaddr, lp->hacr);

  /* Wait for command to finish. */
  for(i = 1000; i > 0; i--)
    {
      obram_read(ioaddr, OFFSET_ISCP, (unsigned char *) &iscp, sizeof(iscp));

      if(iscp.iscp_busy == (unsigned short) 0)
	break;

      udelay(10);
    }

  if(i <= 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wv_82586_start(): iscp_busy timeout.\n",
	     dev->name);
#endif
      return -1;
    }

  /* Check command completion. */
  for(i = 15; i > 0; i--)
    {
      obram_read(ioaddr, OFFSET_SCB, (unsigned char *) &scb, sizeof(scb));

      if (scb.scb_status == (SCB_ST_CX | SCB_ST_CNA))
	break;

      udelay(10);
    }

  if (i <= 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wv_82586_start(): status: expected 0x%02x, got 0x%02x.\n",
	     dev->name, SCB_ST_CX | SCB_ST_CNA, scb.scb_status);
#endif
      return -1;
    }

  wv_ack(dev);

  /* Set the action command header. */
  memset(&cb, 0x00, sizeof(cb));
  cb.ac_command = AC_CFLD_EL | (AC_CFLD_CMD & acmd_diagnose);
  cb.ac_link = OFFSET_CU;
  obram_write(ioaddr, OFFSET_CU, (unsigned char *)&cb, sizeof(cb));

  if(wv_synchronous_cmd(dev, "diag()") == -1)
    return -1;

  obram_read(ioaddr, OFFSET_CU, (unsigned char *)&cb, sizeof(cb));
  if(cb.ac_status & AC_SFLD_FAIL)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wv_82586_start(): i82586 Self Test failed.\n",
	     dev->name);
#endif
      return -1;
    }

#ifdef DEBUG_I82586_SHOW
  wv_scb_show(ioaddr);
#endif

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_82586_start()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * This routine does a standard configuration of the WaveLAN
 * controller (i82586).
 *
 * This routine is a violent hack. We use the first free transmit block
 * to make our configuration. In the buffer area, we create the three
 * configuration commands (linked). We make the previous NOP point to
 * the beginning of the buffer instead of the tx command. After, we go
 * as usual to the NOP command.
 * Note that only the last command (mc_set) will generate an interrupt.
 *
 * (called by wv_hw_reset(), wv_82586_reconfig())
 */
static void
wv_82586_config(device *	dev)
{
  net_local *		lp = (net_local *) dev->priv;
  u_long		ioaddr = dev->base_addr;
  unsigned short	txblock;
  unsigned short	txpred;
  unsigned short	tx_addr;
  unsigned short	nop_addr;
  unsigned short	tbd_addr;
  unsigned short	cfg_addr;
  unsigned short	ias_addr;
  unsigned short	mcs_addr;
  ac_tx_t		tx;
  ac_nop_t		nop;
  ac_cfg_t		cfg;		/* Configure action */
  ac_ias_t		ias;		/* IA-setup action */
  ac_mcs_t		mcs;		/* Multicast setup */
  struct dev_mc_list *	dmi;
  unsigned long		x;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_82586_config()\n", dev->name);
#endif

  x = wv_splhi();

  /* Calculate addresses of next block and previous block. */
  txblock = lp->tx_first_free;
  txpred = txblock - TXBLOCKZ;
  if(txpred < OFFSET_CU)
    txpred += NTXBLOCKS * TXBLOCKZ;
  lp->tx_first_free += TXBLOCKZ;
  if(lp->tx_first_free >= OFFSET_CU + NTXBLOCKS * TXBLOCKZ)
    lp->tx_first_free -= NTXBLOCKS * TXBLOCKZ;

  lp->tx_n_in_use++;

  /* Calculate addresses of the different parts of the block. */
  tx_addr = txblock;
  nop_addr = tx_addr + sizeof(tx);
  tbd_addr = nop_addr + sizeof(nop);
  cfg_addr = tbd_addr + sizeof(tbd_t);	/* beginning of the buffer */
  ias_addr = cfg_addr + sizeof(cfg);
  mcs_addr = ias_addr + sizeof(ias);

  /*
   * Transmit command
   */
  tx.tx_h.ac_status = 0xFFFF;	/* Fake completion value */
  obram_write(ioaddr, toff(ac_tx_t, tx_addr, tx_h.ac_status),
	      (unsigned char *) &tx.tx_h.ac_status,
	      sizeof(tx.tx_h.ac_status));

  /*
   * NOP command
   */
  nop.nop_h.ac_status = 0;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_status),
	      (unsigned char *) &nop.nop_h.ac_status,
	      sizeof(nop.nop_h.ac_status));
  nop.nop_h.ac_link = nop_addr;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_link),
	      (unsigned char *) &nop.nop_h.ac_link,
	      sizeof(nop.nop_h.ac_link));

  /* Create a configure action. */
  memset(&cfg, 0x00, sizeof(cfg));

  /*
   * For Linux we invert AC_CFG_ALOC() so as to conform
   * to the way that net packets reach us from above.
   * (See also ac_tx_t.)
   *
   * Updated from Wavelan Manual WCIN085B
   */
  cfg.cfg_byte_cnt = AC_CFG_BYTE_CNT(sizeof(ac_cfg_t) - sizeof(ach_t));
  cfg.cfg_fifolim = AC_CFG_FIFOLIM(4);
  cfg.cfg_byte8 = AC_CFG_SAV_BF(1) |
		  AC_CFG_SRDY(0);
  cfg.cfg_byte9 = AC_CFG_ELPBCK(0) |
		  AC_CFG_ILPBCK(0) |
		  AC_CFG_PRELEN(AC_CFG_PLEN_2) |
		  AC_CFG_ALOC(1) |
		  AC_CFG_ADDRLEN(WAVELAN_ADDR_SIZE);
  cfg.cfg_byte10 = AC_CFG_BOFMET(1) |
		   AC_CFG_ACR(6) |
		   AC_CFG_LINPRIO(0);
  cfg.cfg_ifs = 0x20;
  cfg.cfg_slotl = 0x0C;
  cfg.cfg_byte13 = AC_CFG_RETRYNUM(15) |
		   AC_CFG_SLTTMHI(0);
  cfg.cfg_byte14 = AC_CFG_FLGPAD(0) |
		   AC_CFG_BTSTF(0) |
		   AC_CFG_CRC16(0) |
		   AC_CFG_NCRC(0) |
		   AC_CFG_TNCRS(1) |
		   AC_CFG_MANCH(0) |
		   AC_CFG_BCDIS(0) |
		   AC_CFG_PRM(lp->promiscuous);
  cfg.cfg_byte15 = AC_CFG_ICDS(0) |
		   AC_CFG_CDTF(0) |
		   AC_CFG_ICSS(0) |
		   AC_CFG_CSTF(0);
/*
  cfg.cfg_min_frm_len = AC_CFG_MNFRM(64);
*/
  cfg.cfg_min_frm_len = AC_CFG_MNFRM(8);

  cfg.cfg_h.ac_command = (AC_CFLD_CMD & acmd_configure);
  cfg.cfg_h.ac_link = ias_addr;
  obram_write(ioaddr, cfg_addr, (unsigned char *)&cfg, sizeof(cfg));

  /* Set up the MAC address */
  memset(&ias, 0x00, sizeof(ias));
  ias.ias_h.ac_command = (AC_CFLD_CMD & acmd_ia_setup);
  ias.ias_h.ac_link = mcs_addr;
  memcpy(&ias.ias_addr[0], (unsigned char *)&dev->dev_addr[0], sizeof(ias.ias_addr));
  obram_write(ioaddr, ias_addr, (unsigned char *)&ias, sizeof(ias));

  /* Initialize adapter's Ethernet multicast addresses */
  memset(&mcs, 0x00, sizeof(mcs));
  mcs.mcs_h.ac_command = AC_CFLD_I | (AC_CFLD_CMD & acmd_mc_setup);
  mcs.mcs_h.ac_link = nop_addr;
  mcs.mcs_cnt = WAVELAN_ADDR_SIZE * lp->mc_count;
  obram_write(ioaddr, mcs_addr, (unsigned char *)&mcs, sizeof(mcs));

  /* Any address to set? */
  if(lp->mc_count)
    {
      for(dmi=dev->mc_list; dmi; dmi=dmi->next)
	outsw(PIOP1(ioaddr), (u_short *) dmi->dmi_addr,
	      WAVELAN_ADDR_SIZE >> 1);

#ifdef DEBUG_CONFIG_INFO
      printk(KERN_DEBUG "%s: wv_82586_config(): set %d multicast addresses:\n",
	     dev->name, lp->mc_count);
      for(dmi=dev->mc_list; dmi; dmi=dmi->next)
	printk(KERN_DEBUG " %02x:%02x:%02x:%02x:%02x:%02x\n",
	       dmi->dmi_addr[0], dmi->dmi_addr[1], dmi->dmi_addr[2],
	       dmi->dmi_addr[3], dmi->dmi_addr[4], dmi->dmi_addr[5] );
#endif
    }

  /*
   * Overwrite the predecessor NOP link
   * so that it points to the configure action.
   */
  nop_addr = txpred + sizeof(tx);
  nop.nop_h.ac_status = 0;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_status),
	      (unsigned char *)&nop.nop_h.ac_status,
	      sizeof(nop.nop_h.ac_status));
  nop.nop_h.ac_link = cfg_addr;
  obram_write(ioaddr, toff(ac_nop_t, nop_addr, nop_h.ac_link),
	      (unsigned char *) &nop.nop_h.ac_link,
	      sizeof(nop.nop_h.ac_link));

  /* If watchdog not already active, activate it... */
  if(lp->watchdog.prev == (timer_list *) NULL)
    {
      /* set timer to expire in WATCHDOG_JIFFIES */
      lp->watchdog.expires = jiffies + WATCHDOG_JIFFIES;
      add_timer(&lp->watchdog);
    }

  lp->reconfig_82586 = 0;

  if(lp->tx_first_in_use == I82586NULL)
    lp->tx_first_in_use = txblock;

  if(lp->tx_n_in_use < NTXBLOCKS - 1)
    dev->tbusy = 0;

  wv_splx(x);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_82586_config()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * This routine, called by wavelan_close(), gracefully stops the 
 * WaveLAN controller (i82586).
 */
static inline void
wv_82586_stop(device *	dev)
{
  net_local *	lp = (net_local *) dev->priv;
  u_long	ioaddr = dev->base_addr;
  u_short	scb_cmd;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_82586_stop()\n", dev->name);
#endif

  /* Suspend both command unit and receive unit. */
  scb_cmd = (SCB_CMD_CUC & SCB_CMD_CUC_SUS) | (SCB_CMD_RUC & SCB_CMD_RUC_SUS);
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *)&scb_cmd, sizeof(scb_cmd));
  set_chan_attn(ioaddr, lp->hacr);

  /* No more interrupts */
  wv_ints_off(dev);

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_82586_stop()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * Totally reset the WaveLAN and restart it.
 * Performs the following actions:
 *	1. A power reset (reset DMA)
 *	2. Initialize the radio modem (using wv_mmc_init)
 *	3. Reset & Configure LAN controller (using wv_82586_start)
 *	4. Start the LAN controller's command unit
 *	5. Start the LAN controller's receive unit
 */
static int
wv_hw_reset(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;
  u_long	ioaddr = dev->base_addr;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: ->wv_hw_reset(dev=0x%x)\n", dev->name,
	 (unsigned int)dev);
#endif

  /* If watchdog was activated, kill it! */
  if(lp->watchdog.prev != (timer_list *) NULL)
    del_timer(&lp->watchdog);

  /* Increase the number of resets done. */
  lp->nresets++;

  wv_hacr_reset(ioaddr);
  lp->hacr = HACR_DEFAULT;

  if((wv_mmc_init(dev) < 0) ||
     (wv_82586_start(dev) < 0))
    return -1;

  /* Enable the card to send interrupts. */
  wv_ints_on(dev);

  /* Start card functions */
  if(wv_cu_start(dev) < 0)
    return -1;

  /* Setup the controller and parameters */
  wv_82586_config(dev);

  /* Finish configuration with the receive unit */
  if(wv_ru_start(dev) < 0)
    return -1;

#ifdef DEBUG_CONFIG_TRACE
  printk(KERN_DEBUG "%s: <-wv_hw_reset()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Check if there is a WaveLAN at the specific base address.
 * As a side effect, this reads the MAC address.
 * (called in wavelan_probe() and init_module())
 */
static int
wv_check_ioaddr(u_long		ioaddr,
		u_char *	mac)
{
  int		i;		/* Loop counter */

  /* Check if the base address if available. */
  if(check_region(ioaddr, sizeof(ha_t)))
    return  EADDRINUSE;		/* ioaddr already used */

  /* Reset host interface */
  wv_hacr_reset(ioaddr);

  /* Read the MAC address from the parameter storage area. */
  psa_read(ioaddr, HACR_DEFAULT, psaoff(0, psa_univ_mac_addr),
	   mac, 6);

  /*
   * Check the first three octets of the address for the manufacturer's code.
   * Note: if this can't find your WaveLAN card, you've got a
   * non-NCR/AT&T/Lucent ISA card.  See wavelan.p.h for detail on
   * how to configure your card.
   */
  for(i = 0; i < (sizeof(MAC_ADDRESSES) / sizeof(char) / 3); i++)
    if((mac[0] == MAC_ADDRESSES[i][0]) &&
       (mac[1] == MAC_ADDRESSES[i][1]) &&
       (mac[2] == MAC_ADDRESSES[i][2]))
      return 0;

#ifdef DEBUG_CONFIG_INFO
  printk(KERN_WARNING "WaveLAN (0x%3X): your MAC address might be %02X:%02X:%02X.\n",
	 ioaddr, mac[0], mac[1], mac[2]);
#endif
    return ENODEV;
}

/************************ INTERRUPT HANDLING ************************/

/*
 * This function is the interrupt handler for the WaveLAN card. This
 * routine will be called whenever: 
 */
static void
wavelan_interrupt(int			irq,
		  void *		dev_id,
		  struct pt_regs *	regs)
{
  device *	dev;
  u_long	ioaddr;
  net_local *	lp;
  u_short	hasr;
  u_short	status;
  u_short	ack_cmd;

  dev = dev_id;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_interrupt()\n", dev->name);
#endif

  lp = (net_local *) dev->priv;
  ioaddr = dev->base_addr;

  /* Prevent reentrance. What should we do here? */
#ifdef DEBUG_INTERRUPT_ERROR
  if(dev->interrupt)
    printk(KERN_INFO "%s: wavelan_interrupt(): Re-entering the interrupt handler.\n",
	   dev->name);
#endif
  dev->interrupt = 1;

  if((hasr = hasr_read(ioaddr)) & HASR_MMC_INTR)
    {
      u_char	dce_status;

      /*
       * Interrupt from the modem management controller.
       * This will clear it -- ignored for now.
       */
      mmc_read(ioaddr, mmroff(0, mmr_dce_status), &dce_status, sizeof(dce_status));
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_interrupt(): unexpected mmc interrupt: status 0x%04x.\n",
	     dev->name, dce_status);
#endif
    }

  if((hasr & HASR_82586_INTR) == 0)
    {
      dev->interrupt = 0;
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_interrupt(): interrupt not coming from i82586\n",
	     dev->name);
#endif
      return;
    }

  /* Read interrupt data. */
  obram_read(ioaddr, scboff(OFFSET_SCB, scb_status),
	     (unsigned char *) &status, sizeof(status));

  /*
   * Acknowledge the interrupt(s).
   */
  ack_cmd = status & SCB_ST_INT;
  obram_write(ioaddr, scboff(OFFSET_SCB, scb_command),
	      (unsigned char *) &ack_cmd, sizeof(ack_cmd));
  set_chan_attn(ioaddr, lp->hacr);

#ifdef DEBUG_INTERRUPT_INFO
  printk(KERN_DEBUG "%s: wavelan_interrupt(): status 0x%04x.\n",
	 dev->name, status);
#endif

  /* Command completed. */
  if((status & SCB_ST_CX) == SCB_ST_CX)
    {
#ifdef DEBUG_INTERRUPT_INFO
      printk(KERN_DEBUG "%s: wavelan_interrupt(): command completed.\n",
	     dev->name);
#endif
      wv_complete(dev, ioaddr, lp);

      /* If watchdog was activated, kill it ! */
      if(lp->watchdog.prev != (timer_list *) NULL)
	del_timer(&lp->watchdog);
      if(lp->tx_n_in_use > 0)
	{
	  /* set timer to expire in WATCHDOG_JIFFIES */
	  lp->watchdog.expires = jiffies + WATCHDOG_JIFFIES;
	  add_timer(&lp->watchdog);
	}
    }

  /* Frame received. */
  if((status & SCB_ST_FR) == SCB_ST_FR)
    {
#ifdef DEBUG_INTERRUPT_INFO
      printk(KERN_DEBUG "%s: wavelan_interrupt(): received packet.\n",
	     dev->name);
#endif
      wv_receive(dev);
    }

  /* Check the state of the command unit. */
  if(((status & SCB_ST_CNA) == SCB_ST_CNA) ||
     (((status & SCB_ST_CUS) != SCB_ST_CUS_ACTV) && dev->start))
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_interrupt(): CU inactive -- restarting\n",
	     dev->name);
#endif
      wv_hw_reset(dev);
    }

  /* Check the state of the command unit. */
  if(((status & SCB_ST_RNR) == SCB_ST_RNR) ||
     (((status & SCB_ST_RUS) != SCB_ST_RUS_RDY) && dev->start))
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_interrupt(): RU not ready -- restarting\n",
	     dev->name);
#endif
      wv_hw_reset(dev);
    }

  dev->interrupt = 0;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_interrupt()\n", dev->name);
#endif
}

/*------------------------------------------------------------------*/
/*
 * Watchdog: when we start a transmission, we set a timer in the
 * kernel.  If the transmission completes, this timer is disabled. If
 * the timer expires, we try to unlock the hardware.
 *
 * Note: this watchdog doesn't work on the same principle as the
 * watchdog in the previous version of the ISA driver. I made it this
 * way because the overhead of add_timer() and del_timer() is nothing
 * and because it avoids calling the watchdog, saving some CPU.
 */
static void
wavelan_watchdog(u_long		a)
{
  device *		dev;
  net_local *		lp;
  u_long		ioaddr;
  unsigned long		x;
  unsigned int		nreaped;

  dev = (device *) a;
  ioaddr = dev->base_addr;
  lp = (net_local *) dev->priv;

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_watchdog()\n", dev->name);
#endif

#ifdef DEBUG_INTERRUPT_ERROR
  printk(KERN_INFO "%s: wavelan_watchdog: watchdog timer expired\n",
	 dev->name);
#endif

  x = wv_splhi();

  dev = (device *) a;
  ioaddr = dev->base_addr;
  lp = (net_local *) dev->priv;

  if(lp->tx_n_in_use <= 0)
    {
      wv_splx(x);
      return;
    }

  nreaped = wv_complete(dev, ioaddr, lp);

#ifdef DEBUG_INTERRUPT_INFO
  printk(KERN_DEBUG "%s: wavelan_watchdog(): %d reaped, %d remain.\n",
	 dev->name, nreaped, lp->tx_n_in_use);
#endif

#ifdef DEBUG_PSA_SHOW
  {
    psa_t		psa;
    psa_read(dev, 0, (unsigned char *) &psa, sizeof(psa));
    wv_psa_show(&psa);
  }
#endif
#ifdef DEBUG_MMC_SHOW
  wv_mmc_show(dev);
#endif
#ifdef DEBUG_I82586_SHOW
  wv_cu_show(dev);
#endif

  /* If no buffer has been freed */
  if(nreaped == 0)
    {
#ifdef DEBUG_INTERRUPT_ERROR
      printk(KERN_INFO "%s: wavelan_watchdog(): cleanup failed, trying reset\n",
	     dev->name);
#endif
      wv_hw_reset(dev);
    }
  else
    /* Reset watchdog for next transmission. */
    if(lp->tx_n_in_use > 0)
      {
	/* set timer to expire in WATCHDOG_JIFFIES */
	lp->watchdog.expires = jiffies + WATCHDOG_JIFFIES;
	add_timer(&lp->watchdog);
      }

  wv_splx(x);

#ifdef DEBUG_INTERRUPT_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_watchdog()\n", dev->name);
#endif
}

/********************* CONFIGURATION CALLBACKS *********************/
/*
 * Here are the functions called by the Linux networking code (NET3)
 * for initialization, configuration and deinstallations of the 
 * WaveLAN ISA hardware.
 */

/*------------------------------------------------------------------*/
/*
 * Configure and start up the WaveLAN PCMCIA adaptor.
 * Called by NET3 when it "opens" the device.
 */
static int
wavelan_open(device *	dev)
{
  u_long	x;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_open(dev=0x%x)\n", dev->name,
	 (unsigned int) dev);
#endif

  /* Check irq */
  if(dev->irq == 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_WARNING "%s: wavelan_open(): no IRQ\n", dev->name);
#endif
      return -ENXIO;
    }

  if(request_irq(dev->irq, &wavelan_interrupt, 0, "WaveLAN", dev) != 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_WARNING "%s: wavelan_open(): invalid IRQ\n", dev->name);
#endif
      return -EAGAIN;
    }

  x = wv_splhi();
  if(wv_hw_reset(dev) != -1)
    {
      dev->interrupt = 0;
      dev->start = 1;
    }
  else
    {
      free_irq(dev->irq, dev);
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wavelan_open(): impossible to start the card\n",
	     dev->name);
#endif
      return -EAGAIN;
    }
  wv_splx(x);

  MOD_INC_USE_COUNT;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_open()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Shut down the WaveLAN ISA card.
 * Called by NET3 when it "closes" the device.
 */
static int
wavelan_close(device *	dev)
{
  net_local *	lp = (net_local *)dev->priv;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_close(dev=0x%x)\n", dev->name,
	 (unsigned int) dev);
#endif

  /* Don't do the job twice. */
  if(dev->start == 0)
    return 0;

  dev->tbusy = 1;
  dev->start = 0;

  /* If watchdog was activated, kill it! */
  if(lp->watchdog.prev != (timer_list *) NULL)
    del_timer(&lp->watchdog);

  /*
   * Flush the Tx and disable Rx.
   */
  wv_82586_stop(dev);

  free_irq(dev->irq, dev);

  MOD_DEC_USE_COUNT;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_close()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Probe an I/O address, and if the WaveLAN is there configure the
 * device structure
 * (called by wavelan_probe() and via init_module()).
 */
static int __init 
wavelan_config(device *	dev)
{
  u_long	ioaddr = dev->base_addr;
  u_char	irq_mask;
  int		irq;
  net_local *	lp;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_config(dev=0x%x, ioaddr=0x%x)\n", dev->name,
	 (unsigned int)dev, ioaddr);
#endif

  /* Check IRQ argument on command line. */
  if(dev->irq != 0)
    {
      irq_mask = wv_irq_to_psa(dev->irq);

      if(irq_mask == 0)
	{
#ifdef DEBUG_CONFIG_ERROR
	  printk(KERN_WARNING "%s: wavelan_config(): invalid IRQ %d ignored.\n",
		 dev->name, dev->irq);
#endif
	  dev->irq = 0;
	}
      else
	{
#ifdef DEBUG_CONFIG_INFO
	  printk(KERN_DEBUG "%s: wavelan_config(): changing IRQ to %d\n",
		 dev->name, dev->irq);
#endif
	  psa_write(ioaddr, HACR_DEFAULT,
		    psaoff(0, psa_int_req_no), &irq_mask, 1);
	  /* update the Wavelan checksum */
	  update_psa_checksum(dev, ioaddr, HACR_DEFAULT);
	  wv_hacr_reset(ioaddr);
	}
    }

  psa_read(ioaddr, HACR_DEFAULT, psaoff(0, psa_int_req_no), &irq_mask, 1);
  if((irq = wv_psa_to_irq(irq_mask)) == -1)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_INFO "%s: wavelan_config(): could not wavelan_map_irq(%d).\n",
	     dev->name, irq_mask);
#endif
      return EAGAIN;
    }

  dev->irq = irq;

  request_region(ioaddr, sizeof(ha_t), "wavelan");

  dev->mem_start = 0x0000;
  dev->mem_end = 0x0000;
  dev->if_port = 0;

  /* Initialize device structures */
  dev->priv = kmalloc(sizeof(net_local), GFP_KERNEL);
  if(dev->priv == NULL)
    return -ENOMEM;
  memset(dev->priv, 0x00, sizeof(net_local));
  lp = (net_local *)dev->priv;

  /* Back link to the device structure. */
  lp->dev = dev;
  /* Add the device at the beginning of the linked list. */
  lp->next = wavelan_list;
  wavelan_list = lp;

  lp->hacr = HACR_DEFAULT;

  lp->watchdog.function = wavelan_watchdog;
  lp->watchdog.data = (unsigned long) dev;
  lp->promiscuous = 0;
  lp->mc_count = 0;

  /*
   * Fill in the fields of the device structure
   * with generic Ethernet values.
   */
  ether_setup(dev);

  dev->open = wavelan_open;
  dev->stop = wavelan_close;
  dev->hard_start_xmit = wavelan_packet_xmit;
  dev->get_stats = wavelan_get_stats;
  dev->set_multicast_list = &wavelan_set_multicast_list;
#ifdef SET_MAC_ADDRESS
  dev->set_mac_address = &wavelan_set_mac_address;
#endif	/* SET_MAC_ADDRESS */

#ifdef WIRELESS_EXT	/* if wireless extension exists in the kernel */
  dev->do_ioctl = wavelan_ioctl;
  dev->get_wireless_stats = wavelan_get_wireless_stats;
#endif

  dev->mtu = WAVELAN_MTU;

  /* Display nice information. */
  wv_init_info(dev);

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: <-wavelan_config()\n", dev->name);
#endif
  return 0;
}

/*------------------------------------------------------------------*/
/*
 * Check for a network adaptor of this type.  Return '0' iff one 
 * exists.  There seem to be different interpretations of
 * the initial value of dev->base_addr.
 * We follow the example in drivers/net/ne.c.
 * (called in "Space.c")
 */
int __init 
wavelan_probe(device *	dev)
{
  short		base_addr;
  mac_addr	mac;		/* MAC address (check existence of WaveLAN) */
  int		i;
  int		r;

#ifdef DEBUG_CALLBACK_TRACE
  printk(KERN_DEBUG "%s: ->wavelan_probe(dev=0x%x (base_addr=0x%x))\n",
	 dev->name, (unsigned int)dev, (unsigned int)dev->base_addr);
#endif

#ifdef	STRUCT_CHECK
  if (wv_struct_check() != (char *) NULL)
    {
      printk(KERN_WARNING "%s: wavelan_probe(): structure/compiler botch: \"%s\"\n",
	     dev->name, wv_struct_check());
      return ENODEV;
    }
#endif	/* STRUCT_CHECK */

  /* Check the value of the command line parameter for base address. */
  base_addr = dev->base_addr;

  /* Don't probe at all. */
  if(base_addr < 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_WARNING "%s: wavelan_probe(): invalid base address\n",
	     dev->name);
#endif
      return ENXIO;
    }

  /* Check a single specified location. */
  if(base_addr > 0x100)
    {
      /* Check if there is something at this base address */
      if((r = wv_check_ioaddr(base_addr, mac)) == 0)
	{
	  memcpy(dev->dev_addr, mac, 6);	/* Copy MAC address. */
	  r = wavelan_config(dev);
	}

#ifdef DEBUG_CONFIG_INFO
      if(r != 0)
	printk(KERN_DEBUG "%s: wavelan_probe(): no device at specified base address (0x%X) or address already in use\n",
	       dev->name, base_addr);
#endif

#ifdef DEBUG_CALLBACK_TRACE
      printk(KERN_DEBUG "%s: <-wavelan_probe()\n", dev->name);
#endif
      return r;
    }

  /* Scan all possible addresses of the WaveLAN hardware. */
  for(i = 0; i < NELS(iobase); i++)
    {
      /* Check whether there is something at this base address. */
      if(wv_check_ioaddr(iobase[i], mac) == 0)
	{
	  dev->base_addr = iobase[i];		/* Copy base address. */
	  memcpy(dev->dev_addr, mac, 6);	/* Copy MAC address. */
	  if(wavelan_config(dev) == 0)
	    {
#ifdef DEBUG_CALLBACK_TRACE
	      printk(KERN_DEBUG "%s: <-wavelan_probe()\n", dev->name);
#endif
	      return 0;
	    }
	}
    }

  /* We may have touched base_addr.  Another driver may not like it. */
  dev->base_addr = base_addr;

#ifdef DEBUG_CONFIG_INFO
  printk(KERN_DEBUG "%s: wavelan_probe(): no device found\n",
	 dev->name);
#endif

  return ENODEV;
}

/****************************** MODULE ******************************/
/*
 * Module entry point: insertion and removal
 */

#ifdef	MODULE
/*------------------------------------------------------------------*/
/*
 * Insertion of the module
 * I'm now quite proud of the multi-device support.
 */
int
init_module(void)
{
  mac_addr	mac;		/* MAC address (check WaveLAN existence) */
  int		ret = -EIO;	/* Return error if no cards found */
  int		i;

#ifdef DEBUG_MODULE_TRACE
  printk(KERN_DEBUG "-> init_module()\n");
#endif

  /* If probing is asked */
  if(io[0] == 0)
    {
#ifdef DEBUG_CONFIG_ERROR
      printk(KERN_WARNING "WaveLAN init_module(): doing device probing (bad !)\n");
      printk(KERN_WARNING "Specify base addresses while loading module to correct the problem\n");
#endif

      /* Copy the basic set of address to be probed. */
      for(i = 0; i < NELS(iobase); i++)
	io[i] = iobase[i];
    }


  /* Loop on all possible base addresses. */
  i = -1;
  while((io[++i] != 0) && (i < NELS(io)))
    {
      /* Check if there is something at this base address. */
      if(wv_check_ioaddr(io[i], mac) == 0)
	{
	  device *	dev;

	  /* Create device and set basic arguments. */
	  dev = kmalloc(sizeof(struct net_device), GFP_KERNEL);
	  if(dev==NULL)
	  {
	  	ret = -ENOMEM;
	  	break;
	  }
	  memset(dev, 0x00, sizeof(struct net_device));
	  dev->name = name[i];
	  dev->base_addr = io[i];
	  dev->irq = irq[i];
	  dev->init = &wavelan_config;
	  memcpy(dev->dev_addr, mac, 6);	/* Copy MAC address. */

	  /* Try to create the device. */
	  if(register_netdev(dev) != 0)
	    {
	      /* Deallocate everything. */
	      /* Note: if dev->priv is mallocated, there is no way to fail. */
	      kfree_s(dev, sizeof(struct net_device));
	    }
	  else
	    {
              /* If at least one device OK, we do not fail */
              ret = 0;
	    }
	}	/* if there is something at the address */
    }		/* Loop on all addresses. */

#ifdef DEBUG_CONFIG_ERROR
  if(wavelan_list == (net_local *) NULL)
    printk(KERN_WARNING "WaveLAN init_module(): no device found\n");
#endif

#ifdef DEBUG_MODULE_TRACE
  printk(KERN_DEBUG "<- init_module()\n");
#endif
  return ret;
}

/*------------------------------------------------------------------*/
/*
 * Removal of the module
 */
void
cleanup_module(void)
{
#ifdef DEBUG_MODULE_TRACE
  printk(KERN_DEBUG "-> cleanup_module()\n");
#endif

  /* Loop on all devices and release them. */
  while(wavelan_list != (net_local *) NULL)
    {
      device *	dev = wavelan_list->dev;

#ifdef DEBUG_CONFIG_INFO
      printk(KERN_DEBUG "%s: cleanup_module(): removing device at 0x%x\n",
	     dev->name, (unsigned int) dev);
#endif

      /* Release the ioport region. */
      release_region(dev->base_addr, sizeof(ha_t));

      /* Definitely remove the device. */
      unregister_netdev(dev);

      /* Unlink the device. */
      wavelan_list = wavelan_list->next;

      /* Free pieces. */
      kfree_s(dev->priv, sizeof(struct net_local));
      kfree_s(dev, sizeof(struct net_device));
    }

#ifdef DEBUG_MODULE_TRACE
  printk(KERN_DEBUG "<- cleanup_module()\n");
#endif
}
#endif	/* MODULE */

/*
 * This software may only be used and distributed
 * according to the terms of the GNU Public License.
 *
 * This software was developed as a component of the
 * Linux operating system.
 * It is based on other device drivers and information
 * either written or supplied by:
 *	Ajay Bakre (bakre@paul.rutgers.edu),
 *	Donald Becker (becker@cesdis.gsfc.nasa.gov),
 *	Loeke Brederveld (Loeke.Brederveld@Utrecht.NCR.com),
 *	Anders Klemets (klemets@it.kth.se),
 *	Vladimir V. Kolpakov (w@stier.koenig.ru),
 *	Marc Meertens (Marc.Meertens@Utrecht.NCR.com),
 *	Pauline Middelink (middelin@polyware.iaf.nl),
 *	Robert Morris (rtm@das.harvard.edu),
 *	Jean Tourrilhes (jt@hplb.hpl.hp.com),
 *	Girish Welling (welling@paul.rutgers.edu),
 *
 * Thanks go also to:
 *	James Ashton (jaa101@syseng.anu.edu.au),
 *	Alan Cox (iialan@iiit.swan.ac.uk),
 *	Allan Creighton (allanc@cs.usyd.edu.au),
 *	Matthew Geier (matthew@cs.usyd.edu.au),
 *	Remo di Giovanni (remo@cs.usyd.edu.au),
 *	Eckhard Grah (grah@wrcs1.urz.uni-wuppertal.de),
 *	Vipul Gupta (vgupta@cs.binghamton.edu),
 *	Mark Hagan (mhagan@wtcpost.daytonoh.NCR.COM),
 *	Tim Nicholson (tim@cs.usyd.edu.au),
 *	Ian Parkin (ian@cs.usyd.edu.au),
 *	John Rosenberg (johnr@cs.usyd.edu.au),
 *	George Rossi (george@phm.gov.au),
 *	Arthur Scott (arthur@cs.usyd.edu.au),
 *	Peter Storey,
 * for their assistance and advice.
 *
 * Please send bug reports, updates, comments to:
 *
 * Bruce Janson                                    Email:  bruce@cs.usyd.edu.au
 * Basser Department of Computer Science           Phone:  +61-2-9351-3423
 * University of Sydney, N.S.W., 2006, AUSTRALIA   Fax:    +61-2-9351-3838
 */
