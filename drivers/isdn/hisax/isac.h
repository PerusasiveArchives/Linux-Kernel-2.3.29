/* $Id: isac.h,v 1.5 1998/05/25 12:58:03 keil Exp $

 * isac.h   ISAC specific defines
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: isac.h,v $
 * Revision 1.5  1998/05/25 12:58:03  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 1.4  1997/10/29 19:09:34  keil
 * new L1
 *
 * Revision 1.3  1997/07/27 21:37:41  keil
 * T3 implemented; supervisor l1timer; B-channel TEST_LOOP
 *
 * Revision 1.2  1997/06/26 11:16:16  keil
 * first version
 *
 *
 */


/* All Registers original Siemens Spec  */

#define ISAC_MASK 0x20
#define ISAC_ISTA 0x20
#define ISAC_STAR 0x21
#define ISAC_CMDR 0x21
#define ISAC_EXIR 0x24
#define ISAC_ADF2 0x39
#define ISAC_SPCR 0x30
#define ISAC_ADF1 0x38
#define ISAC_CIR0 0x31
#define ISAC_CIX0 0x31
#define ISAC_CIR1 0x33
#define ISAC_CIX1 0x33
#define ISAC_STCR 0x37
#define ISAC_MODE 0x22
#define ISAC_RSTA 0x27
#define ISAC_RBCL 0x25
#define ISAC_RBCH 0x2A
#define ISAC_TIMR 0x23
#define ISAC_SQXR 0x3b
#define ISAC_MOSR 0x3a
#define ISAC_MOCR 0x3a
#define ISAC_MOR0 0x32
#define ISAC_MOX0 0x32
#define ISAC_MOR1 0x34
#define ISAC_MOX1 0x34

#define ISAC_RBCH_XAC 0x80

#define ISAC_CMD_TIM	0x0
#define ISAC_CMD_RS	0x1
#define ISAC_CMD_SCZ	0x4
#define ISAC_CMD_SSZ	0x2
#define ISAC_CMD_AR8	0x8
#define ISAC_CMD_AR10	0x9
#define ISAC_CMD_ARL	0xA
#define ISAC_CMD_DUI	0xF

#define ISAC_IND_RS	0x1
#define ISAC_IND_PU	0x7
#define ISAC_IND_DR	0x0
#define ISAC_IND_SD	0x2
#define ISAC_IND_DIS	0x3
#define ISAC_IND_EI	0x6
#define ISAC_IND_RSY	0x4
#define ISAC_IND_ARD	0x8
#define ISAC_IND_TI	0xA
#define ISAC_IND_ATI	0xB
#define ISAC_IND_AI8	0xC
#define ISAC_IND_AI10	0xD
#define ISAC_IND_DID	0xF

extern void ISACVersion(struct IsdnCardState *cs, char *s);
extern void initisac(struct IsdnCardState *cs);
extern void isac_interrupt(struct IsdnCardState *cs, u_char val);
extern void clear_pending_isac_ints(struct IsdnCardState *cs);
