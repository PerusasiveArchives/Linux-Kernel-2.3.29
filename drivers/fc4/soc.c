/* soc.c: Sparc SUNW,soc (Serial Optical Channel) Fibre Channel Sbus adapter support.
 *
 * Copyright (C) 1996,1997,1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1997,1998 Jirka Hanika (geo@ff.cuni.cz)
 *
 * Sources:
 *	Fibre Channel Physical & Signaling Interface (FC-PH), dpANS, 1994
 *	dpANS Fibre Channel Protocol for SCSI (X3.269-199X), Rev. 012, 1995
 *
 * Supported hardware:
 *      Tested on SOC sbus card bought with SS1000 in Linux running on SS5 and Ultra1. 
 *      For SOC sbus cards, you have to make sure your FCode is 1.52 or later.
 *      If you have older FCode, you should try to upgrade or get SOC microcode from Sun
 *      (the microcode is present in Solaris soc driver as well). In that case you need
 *      to #define HAVE_SOC_UCODE and format the microcode into soc_asm.c. For the exact
 *      format mail me and I will tell you. I cannot offer you the actual microcode though,
 *      unless Sun confirms they don't mind.
 */

static char *version =
        "soc.c:v1.3 9/Feb/99 Jakub Jelinek (jj@ultra.linux.cz), Jirka Hanika (geo@ff.cuni.cz)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>
#include <asm/irq.h>

/* #define SOCDEBUG */
/* #define HAVE_SOC_UCODE */

#include "fcp_impl.h"
#include "soc.h"
#ifdef HAVE_SOC_UCODE
#include "soc_asm.h"
#endif

#define soc_printk printk ("soc%d: ", s->soc_no); printk 

#ifdef SOCDEBUG
#define SOD(x)  soc_printk x;
#else
#define SOD(x)
#endif

#define for_each_soc(s) for (s = socs; s; s = s->next)
struct soc *socs = NULL;

static inline void soc_disable(struct soc *s)
{
	s->regs->imask = 0; s->regs->cmd = SOC_CMD_SOFT_RESET;
}

static inline void soc_enable(struct soc *s)
{
	SOD(("enable %08x\n", s->cfg))
	s->regs->sae = 0; s->regs->cfg = s->cfg;
	s->regs->cmd = SOC_CMD_RSP_QALL; 
	SOC_SETIMASK(s, SOC_IMASK_RSP_QALL | SOC_IMASK_SAE);
	SOD(("imask %08lx %08lx\n", s->imask, s->regs->imask));
}

static void soc_reset(fc_channel *fc)
{
	soc_port *port = (soc_port *)fc;
	struct soc *s = port->s;
	
	/* FIXME */
	soc_disable(s);
	s->req[0].seqno = 1;
	s->req[1].seqno = 1;
	s->rsp[0].seqno = 1;
	s->rsp[1].seqno = 1;
	s->req[0].in = 0;
	s->req[1].in = 0;
	s->rsp[0].in = 0;
	s->rsp[1].in = 0;
	s->req[0].out = 0;
	s->req[1].out = 0;
	s->rsp[0].out = 0;
	s->rsp[1].out = 0;

	/* FIXME */
	soc_enable(s);
}

static void inline soc_solicited (struct soc *s)
{
	fc_hdr fchdr;
	soc_rsp *hwrsp;
	soc_cq *sw_cq;
	int token;
	int status;
	fc_channel *fc;

	sw_cq = &s->rsp[SOC_SOLICITED_RSP_Q];

	if (sw_cq->pool == NULL)
		sw_cq->pool =
			(soc_req *)(s->xram + (xram_get_32low ((xram_p)&sw_cq->hw_cq->address) / sizeof(u16)));
	sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
	SOD (("soc_solicited, %d packets arrived\n", (sw_cq->in - sw_cq->out) & sw_cq->last))
	for (;;) {
		hwrsp = (soc_rsp *)sw_cq->pool + sw_cq->out;
		token = xram_get_32low ((xram_p)&hwrsp->shdr.token);
		status = xram_get_32low ((xram_p)&hwrsp->status);
		fc = (fc_channel *)(&s->port[(token >> 11) & 1]);
		
		if (status == SOC_OK)
			fcp_receive_solicited(fc, token >> 12, token & ((1 << 11) - 1), FC_STATUS_OK, NULL);
		else {
			xram_copy_from(&fchdr, (xram_p)&hwrsp->fchdr, sizeof(fchdr));
			/* We have intentionally defined FC_STATUS_* constants to match SOC_* constants, otherwise
			   we'd have to translate status */
			fcp_receive_solicited(fc, token >> 12, token & ((1 << 11) - 1), status, &fchdr);
		}
			
		if (++sw_cq->out > sw_cq->last) {
			sw_cq->seqno++;
			sw_cq->out = 0;
		}
		
		if (sw_cq->out == sw_cq->in) {
			sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
			if (sw_cq->out == sw_cq->in) {
				/* Tell the hardware about it */
				s->regs->cmd = (sw_cq->out << 24) | (SOC_CMD_RSP_QALL & ~(SOC_CMD_RSP_Q0 << SOC_SOLICITED_RSP_Q));
				/* Read it, so that we're sure it has been updated */
				s->regs->cmd;
				sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
				if (sw_cq->out == sw_cq->in)
					break;
			}
		}
	}
}

static void inline soc_request (struct soc *s, u32 cmd)
{
	SOC_SETIMASK(s, s->imask & ~(cmd & SOC_CMD_REQ_QALL));
	SOD(("imask %08lx %08lx\n", s->imask, s->regs->imask));

	SOD(("Queues available %08x OUT %X %X\n", cmd, xram_get_8((xram_p)&s->req[0].hw_cq->out), xram_get_8((xram_p)&s->req[0].hw_cq->out)))
	if (s->port[s->curr_port].fc.state != FC_STATE_OFFLINE) {
		fcp_queue_empty ((fc_channel *)&(s->port[s->curr_port]));
		if (((s->req[1].in + 1) & s->req[1].last) != (s->req[1].out))
			fcp_queue_empty ((fc_channel *)&(s->port[1 - s->curr_port]));
	} else
		fcp_queue_empty ((fc_channel *)&(s->port[1 - s->curr_port]));
	if (s->port[1 - s->curr_port].fc.state != FC_STATE_OFFLINE)
		s->curr_port ^= 1;
}

static void inline soc_unsolicited (struct soc *s)
{
	soc_rsp *hwrsp, *hwrspc;
	soc_cq *sw_cq;
	int count;
	int status;
	int flags;
	fc_channel *fc;

	sw_cq = &s->rsp[SOC_UNSOLICITED_RSP_Q];
	if (sw_cq->pool == NULL)
		sw_cq->pool =
			(soc_req *)(s->xram + (xram_get_32low ((xram_p)&sw_cq->hw_cq->address) / sizeof(u16)));

	sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
	SOD (("soc_unsolicited, %d packets arrived\n", (sw_cq->in - sw_cq->out) & sw_cq->last))
	while (sw_cq->in != sw_cq->out) {
		/* ...real work per entry here... */
		hwrsp = (soc_rsp *)sw_cq->pool + sw_cq->out;

		hwrspc = NULL;
		flags = hwrsp->shdr.flags;
		count = xram_get_8 ((xram_p)&hwrsp->count);
		fc = (fc_channel *)&s->port[flags & SOC_PORT_B];
		SOD(("FC %08lx fcp_state_change %08lx\n", (long)fc, (long)fc->fcp_state_change))
		
		if (count != 1) {
			/* Ugh, continuation entries */
			u8 in;

			if (count != 2) {
				printk("%s: Too many continuations entries %d\n", fc->name, count);
				goto update_out;
			}
			
			in = sw_cq->in;
			if (in < sw_cq->out) in += sw_cq->last + 1;
			if (in < sw_cq->out + 2) {
				/* Ask the hardware about it if they haven't arrived yet */
				s->regs->cmd = (sw_cq->out << 24) | (SOC_CMD_RSP_QALL & ~(SOC_CMD_RSP_Q0 << SOC_UNSOLICITED_RSP_Q));
				/* Read it, so that we're sure it has been updated */
				s->regs->cmd;
				sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
				in = sw_cq->in;
				if (in < sw_cq->out) in += sw_cq->last + 1;
				if (in < sw_cq->out + 2) /* Nothing came, let us wait */
					return;
			}
			if (sw_cq->out == sw_cq->last)
				hwrspc = (soc_rsp *)sw_cq->pool;
			else
				hwrspc = hwrsp + 1;
		}
		
		switch (flags & ~SOC_PORT_B) {
		case SOC_STATUS:
			status = xram_get_32low ((xram_p)&hwrsp->status);
			switch (status) {
			case SOC_ONLINE:
				SOD(("State change to ONLINE\n"));
				fcp_state_change(fc, FC_STATE_ONLINE);
				break;
			case SOC_OFFLINE:
				SOD(("State change to OFFLINE\n"));
				fcp_state_change(fc, FC_STATE_OFFLINE);
				break;
			default:
				printk ("%s: Unknown STATUS no %d\n", fc->name, status);
				break;
			}
			break;
		case (SOC_UNSOLICITED|SOC_FC_HDR):
			{
				int r_ctl = xram_get_8 ((xram_p)&hwrsp->fchdr);
				unsigned len;
				char buf[64];
				
				if ((r_ctl & 0xf0) == R_CTL_EXTENDED_SVC) {
					len = xram_get_32 ((xram_p)&hwrsp->shdr.bytecnt);
					if (len < 4 || !hwrspc)
						printk ("%s: Invalid R_CTL %02x continuation entries\n", fc->name, r_ctl);
					else {
						if (len > 60) len = 60;
						xram_copy_from (buf, (xram_p)hwrspc, (len + 3) & ~3);
						if (*(u32 *)buf == LS_DISPLAY) {
							int i;
							
							for (i = 4; i < len; i++)
								if (buf[i] == '\n') buf[i] = ' ';
							buf[len] = 0;
							printk ("%s message: %s\n", fc->name, buf + 4);
						} else
							printk ("%s: Unknown LS_CMD %02x\n", fc->name, buf[0]);
					}
				} else
					printk ("%s: Unsolicited R_CTL %02x not handled\n", fc->name, r_ctl);
			}
			break;
		default:
			printk ("%s: Unexpected flags %08x\n", fc->name, flags);
			break;
		}
update_out:
		if (++sw_cq->out > sw_cq->last) {
			sw_cq->seqno++;
			sw_cq->out = 0;
		}
		
		if (hwrspc) {
			if (++sw_cq->out > sw_cq->last) {
				sw_cq->seqno++;
				sw_cq->out = 0;
			}
		}
		
		if (sw_cq->out == sw_cq->in) {
			sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
			if (sw_cq->out == sw_cq->in) {
				/* Tell the hardware about it */
				s->regs->cmd = (sw_cq->out << 24) | (SOC_CMD_RSP_QALL & ~(SOC_CMD_RSP_Q0 << SOC_UNSOLICITED_RSP_Q));
				/* Read it, so that we're sure it has been updated */
				s->regs->cmd;
				sw_cq->in = xram_get_8 ((xram_p)&sw_cq->hw_cq->in);
			}
		}
	}
}

static void soc_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 cmd;
	unsigned long flags;
	register struct soc *s = (struct soc *)dev_id;

	spin_lock_irqsave(&io_request_lock, flags);
	cmd = s->regs->cmd;
	for (; (cmd = SOC_INTR (s, cmd)); cmd = s->regs->cmd) {
		if (cmd & SOC_CMD_RSP_Q1) soc_unsolicited (s);
		if (cmd & SOC_CMD_RSP_Q0) soc_solicited (s);
		if (cmd & SOC_CMD_REQ_QALL) soc_request (s, cmd);
	}
	spin_unlock_irqrestore(&io_request_lock, flags);
}

#define TOKEN(proto, port, token) (((proto)<<12)|(token)|(port))

static int soc_hw_enque (fc_channel *fc, fcp_cmnd *fcmd)
{
	soc_port *port = (soc_port *)fc;
	struct soc *s = port->s;
	int qno;
	soc_cq *sw_cq;
	int cq_next_in;
	soc_req *request;
	fc_hdr *fch;
	int i;

	if (fcmd->proto == TYPE_SCSI_FCP)
		qno = 1;
	else
		qno = 0;
	SOD(("Putting a FCP packet type %d into hw queue %d\n", fcmd->proto, qno))
	if (s->imask & (SOC_IMASK_REQ_Q0 << qno)) {
		SOD(("EIO %08x\n", s->imask))
		return -EIO;
	}
	sw_cq = s->req + qno;
	cq_next_in = (sw_cq->in + 1) & sw_cq->last;
	
	if (cq_next_in == sw_cq->out 
		    && cq_next_in == (sw_cq->out = xram_get_8((xram_p)&sw_cq->hw_cq->out))) {
		SOD(("%d IN %d OUT %d LAST %d\n", qno, sw_cq->in, sw_cq->out, sw_cq->last))
		SOC_SETIMASK(s, s->imask | (SOC_IMASK_REQ_Q0 << qno));
		SOD(("imask %08lx %08lx\n", s->imask, s->regs->imask));
		/* If queue is full, just say NO */
		return -EBUSY;
	}
	
	request = sw_cq->pool + sw_cq->in;
	fch = &request->fchdr;
	
	switch (fcmd->proto) {
	case TYPE_SCSI_FCP:
		request->shdr.token = TOKEN(TYPE_SCSI_FCP, port->mask, fcmd->token); 
		request->data[0].base = fc->dma_scsi_cmd + fcmd->token * sizeof(fcp_cmd);
		request->data[0].count = sizeof(fcp_cmd);
		request->data[1].base = fc->dma_scsi_rsp + fcmd->token * fc->rsp_size;
		request->data[1].count = fc->rsp_size;
		if (fcmd->data) {
			request->shdr.segcnt = 3;
			i = fc->scsi_cmd_pool[fcmd->token].fcp_data_len;
			request->shdr.bytecnt = i;
			request->data[2].base = fcmd->data;
			request->data[2].count = i;
			request->type = (fc->scsi_cmd_pool[fcmd->token].fcp_cntl & FCP_CNTL_WRITE) ?
				SOC_CQTYPE_IO_WRITE : SOC_CQTYPE_IO_READ;
		} else {
			request->shdr.segcnt = 2;
			request->shdr.bytecnt = 0;
			request->data[2].base = 0;
			request->data[2].count = 0;
			request->type = SOC_CQTYPE_SIMPLE;
		}
		FILL_FCHDR_RCTL_DID(fch, R_CTL_COMMAND, fc->did);
		FILL_FCHDR_SID(fch, fc->sid);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_SCSI_FCP, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		fch->param = 0;
		request->shdr.flags = port->flags;
		request->shdr.class = 2;
		break;
		
	case PROTO_OFFLINE:
		memset (request, 0, sizeof(*request));
		request->shdr.token = TOKEN(PROTO_OFFLINE, port->mask, fcmd->token); 
		request->type = SOC_CQTYPE_OFFLINE;
		FILL_FCHDR_RCTL_DID(fch, R_CTL_COMMAND, fc->did);
		FILL_FCHDR_SID(fch, fc->sid);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_SCSI_FCP, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		request->shdr.flags = port->flags;
		break;
		
	case PROTO_REPORT_AL_MAP:
		/* SOC only supports Point-to-Point topology, no FC-AL, sorry... */
		return -ENOSYS;

	default: 
		request->shdr.token = TOKEN(fcmd->proto, port->mask, fcmd->token);
		request->shdr.class = 2;
		request->shdr.flags = port->flags;
		memcpy (fch, &fcmd->fch, sizeof(fc_hdr));
		request->data[0].count = fcmd->cmdlen;
		request->data[1].count = fcmd->rsplen;
		request->type = fcmd->class;
		switch (fcmd->class) {
		case FC_CLASS_OUTBOUND:
			request->data[0].base = fcmd->cmd;
			request->data[0].count = fcmd->cmdlen;
			request->type = SOC_CQTYPE_OUTBOUND;
			request->shdr.bytecnt = fcmd->cmdlen;
			request->shdr.segcnt = 1;
			break;
		case FC_CLASS_INBOUND:
			request->data[0].base = fcmd->rsp;
			request->data[0].count = fcmd->rsplen;
			request->type = SOC_CQTYPE_INBOUND;
			request->shdr.bytecnt = 0;
			request->shdr.segcnt = 1;
			break;
		case FC_CLASS_SIMPLE:
			request->data[0].base = fcmd->cmd;
			request->data[1].base = fcmd->rsp;
			request->data[0].count = fcmd->cmdlen;
			request->data[1].count = fcmd->rsplen;
			request->type = SOC_CQTYPE_SIMPLE;
			request->shdr.bytecnt = fcmd->cmdlen;
			request->shdr.segcnt = 2;
			break;
		case FC_CLASS_IO_READ:
		case FC_CLASS_IO_WRITE:
			request->data[0].base = fcmd->cmd;
			request->data[1].base = fcmd->rsp;
			request->data[0].count = fcmd->cmdlen;
			request->data[1].count = fcmd->rsplen;
			request->type = (fcmd->class == FC_CLASS_IO_READ) ? SOC_CQTYPE_IO_READ : SOC_CQTYPE_IO_WRITE;
			if (fcmd->data) {
				request->data[2].base = fcmd->data;
				request->data[2].count = fcmd->datalen;
				request->shdr.bytecnt = fcmd->datalen;
				request->shdr.segcnt = 3;
			} else {
				request->shdr.bytecnt = 0;
				request->shdr.segcnt = 2;
			}
			break;
		}
		break;
	}

	request->count = 1;
	request->flags = 0;
	request->seqno = sw_cq->seqno;
	
	/* And now tell the SOC about it */

	if (++sw_cq->in > sw_cq->last) {
		sw_cq->in = 0;
		sw_cq->seqno++;
	}
	
	SOD(("Putting %08x into cmd\n", SOC_CMD_RSP_QALL | (sw_cq->in << 24) | (SOC_CMD_REQ_Q0 << qno)))
	
	s->regs->cmd = SOC_CMD_RSP_QALL | (sw_cq->in << 24) | (SOC_CMD_REQ_Q0 << qno);
	/* Read so that command is completed */	
	s->regs->cmd;
	
	return 0;
}

static inline void soc_download_fw(struct soc *s)
{
#ifdef HAVE_SOC_UCODE
	xram_copy_to (s->xram, soc_ucode, sizeof(soc_ucode));
	xram_bzero (s->xram + (sizeof(soc_ucode)/sizeof(u16)), 32768 - sizeof(soc_ucode));
#endif
}

/* Check for what the best SBUS burst we can use happens
 * to be on this machine.
 */
static inline void soc_init_bursts(struct soc *s, struct linux_sbus_device *sdev)
{
	int bsizes, bsizes_more;

	bsizes = (prom_getintdefault(sdev->prom_node,"burst-sizes",0xff) & 0xff);
	bsizes_more = (prom_getintdefault(sdev->my_bus->prom_node, "burst-sizes", 0xff) & 0xff);
	bsizes &= bsizes_more;
	if ((bsizes & 0x7f) == 0x7f)
		s->cfg = SOC_CFG_BURST_64;
	else if ((bsizes & 0x3f) == 0x3f) 
		s->cfg = SOC_CFG_BURST_32;
	else if ((bsizes & 0x1f) == 0x1f)
		s->cfg = SOC_CFG_BURST_16;
	else
		s->cfg = SOC_CFG_BURST_4;
}

static inline void soc_init(struct linux_sbus_device *sdev, int no)
{
	unsigned char tmp[60];
	int propl;
	struct soc *s;
	static unsigned version_printed = 0;
	soc_hw_cq cq[8];
	int size, i;
	int irq;
	
	s = kmalloc (sizeof (struct soc), GFP_KERNEL);
	if (!s) return;
	memset (s, 0, sizeof(struct soc));
	s->soc_no = no;

	SOD(("socs %08lx soc_intr %08lx soc_hw_enque %08x\n", (long)socs, (long)soc_intr, (long)soc_hw_enque))	
	if (version_printed++ == 0)
		printk (version);
#ifdef MODULE
	s->port[0].fc.module = &__this_module;
	s->port[1].fc.module = &__this_module;
#else
	s->port[0].fc.module = NULL;
	s->port[1].fc.module = NULL;
#endif
	                                	
	s->next = socs;
	socs = s;
	s->port[0].fc.dev = sdev;
	s->port[1].fc.dev = sdev;
	s->port[0].s = s;
	s->port[1].s = s;

	s->port[0].fc.next = &s->port[1].fc;

	/* World Wide Name of SOC */
	propl = prom_getproperty (sdev->prom_node, "soc-wwn", tmp, sizeof(tmp));
	if (propl != sizeof (fc_wwn)) {
		s->wwn.naaid = NAAID_IEEE;
		s->wwn.lo = 0x12345678;
	} else
		memcpy (&s->wwn, tmp, sizeof (fc_wwn));
		
	propl = prom_getproperty (sdev->prom_node, "port-wwns", tmp, sizeof(tmp));
	if (propl != 2 * sizeof (fc_wwn)) {
		s->port[0].fc.wwn_nport.naaid = NAAID_IEEE_EXT;
		s->port[0].fc.wwn_nport.hi = s->wwn.hi;
		s->port[0].fc.wwn_nport.lo = s->wwn.lo;
		s->port[1].fc.wwn_nport.naaid = NAAID_IEEE_EXT;
		s->port[1].fc.wwn_nport.nportid = 1;
		s->port[1].fc.wwn_nport.hi = s->wwn.hi;
		s->port[1].fc.wwn_nport.lo = s->wwn.lo;
	} else {
		memcpy (&s->port[0].fc.wwn_nport, tmp, sizeof (fc_wwn));
		memcpy (&s->port[1].fc.wwn_nport, tmp + sizeof (fc_wwn), sizeof (fc_wwn));
	}
	memcpy (&s->port[0].fc.wwn_node, &s->wwn, sizeof (fc_wwn));
	memcpy (&s->port[1].fc.wwn_node, &s->wwn, sizeof (fc_wwn));
	SOD(("Got wwns %08x%08x ports %08x%08x and %08x%08x\n", 
		*(u32 *)&s->port[0].fc.wwn_nport, s->port[0].fc.wwn_nport.lo,
		*(u32 *)&s->port[0].fc.wwn_nport, s->port[0].fc.wwn_nport.lo,
		*(u32 *)&s->port[1].fc.wwn_nport, s->port[1].fc.wwn_nport.lo))
		
	s->port[0].fc.sid = 1;
	s->port[1].fc.sid = 17;
	s->port[0].fc.did = 2;
	s->port[1].fc.did = 18;
	
	s->port[0].fc.reset = soc_reset;
	s->port[1].fc.reset = soc_reset;
	
	/* Setup the reg property for this device. */
	prom_apply_sbus_ranges(sdev->my_bus, sdev->reg_addrs, sdev->num_registers, sdev);
                                                                                                                                               	
	if (sdev->num_registers == 1) {
		/* Probably SunFire onboard SOC */
		s->xram = (xram_p)
			sparc_alloc_io (sdev->reg_addrs [0].phys_addr, 0, 
				sdev->reg_addrs [0].reg_size, "soc_xram",
				sdev->reg_addrs [0].which_io, 0);
		s->regs = (struct soc_regs *)((char *)s->xram + 0x10000);
	} else {
		/* Probably SOC sbus card */
		s->xram = (xram_p)
			sparc_alloc_io (sdev->reg_addrs [1].phys_addr, 0, 
				sdev->reg_addrs [1].reg_size, "soc_xram",
				sdev->reg_addrs [1].which_io, 0);
		s->regs = (struct soc_regs *)
			sparc_alloc_io (sdev->reg_addrs [2].phys_addr, 0, 
				sdev->reg_addrs [2].reg_size, "soc_regs",
				sdev->reg_addrs [2].which_io, 0);
	}
	
	soc_init_bursts(s, sdev);
	
	SOD(("Disabling SOC\n"))
	
	soc_disable (s);
	
	irq = sdev->irqs[0];

	if (request_irq (irq, soc_intr, SA_SHIRQ, "SOC", (void *)s)) {
		soc_printk ("Cannot order irq %d to go\n", irq);
		socs = s->next;
		return;
	}

	SOD(("SOC uses IRQ%s\n", __irq_itoa(irq)))
	
	s->port[0].fc.irq = irq;
	s->port[1].fc.irq = irq;
	
	sprintf (s->port[0].fc.name, "soc%d port A", no);
	sprintf (s->port[1].fc.name, "soc%d port B", no);
	s->port[0].flags = SOC_FC_HDR | SOC_PORT_A;
	s->port[1].flags = SOC_FC_HDR | SOC_PORT_B;
	s->port[1].mask = (1 << 11);
	
	s->port[0].fc.hw_enque = soc_hw_enque;
	s->port[1].fc.hw_enque = soc_hw_enque;
	
	soc_download_fw (s);
	
	SOD(("Downloaded firmware\n"))

	/* Now setup xram circular queues */
	memset (cq, 0, sizeof(cq));
	
	size = (SOC_CQ_REQ0_SIZE + SOC_CQ_REQ1_SIZE) * sizeof(soc_req);
	s->req[0].pool = (soc_req *) sparc_dvma_malloc (size, "SOC request queues", &cq[0].address);
	s->req[1].pool = s->req[0].pool + SOC_CQ_REQ0_SIZE;
	
	s->req[0].hw_cq = (soc_hw_cq *)(s->xram + SOC_CQ_REQ_OFFSET);
	s->req[1].hw_cq = (soc_hw_cq *)(s->xram + SOC_CQ_REQ_OFFSET + sizeof(soc_hw_cq) / sizeof(u16));
	s->rsp[0].hw_cq = (soc_hw_cq *)(s->xram + SOC_CQ_RSP_OFFSET);
	s->rsp[1].hw_cq = (soc_hw_cq *)(s->xram + SOC_CQ_RSP_OFFSET + sizeof(soc_hw_cq) / sizeof(u16));
	
	cq[1].address = cq[0].address + (SOC_CQ_REQ0_SIZE * sizeof(soc_req));
	cq[4].address = 1;
	cq[5].address = 1;
	cq[0].last = SOC_CQ_REQ0_SIZE - 1;
	cq[1].last = SOC_CQ_REQ1_SIZE - 1;
	cq[4].last = SOC_CQ_RSP0_SIZE - 1;
	cq[5].last = SOC_CQ_RSP1_SIZE - 1;
	for (i = 0; i < 8; i++)
		cq[i].seqno = 1;
	
	s->req[0].last = SOC_CQ_REQ0_SIZE - 1;
	s->req[1].last = SOC_CQ_REQ1_SIZE - 1;
	s->rsp[0].last = SOC_CQ_RSP0_SIZE - 1;
	s->rsp[1].last = SOC_CQ_RSP1_SIZE - 1;
	
	s->req[0].seqno = 1;
	s->req[1].seqno = 1;
	s->rsp[0].seqno = 1;
	s->rsp[1].seqno = 1;

	xram_copy_to (s->xram + SOC_CQ_REQ_OFFSET, cq, sizeof(cq));
	
	/* Make our sw copy of SOC service parameters */
	xram_copy_from (s->serv_params, s->xram + 0x140, sizeof (s->serv_params));
	
	s->port[0].fc.common_svc = (common_svc_parm *)s->serv_params;
	s->port[0].fc.class_svcs = (svc_parm *)(s->serv_params + 0x20);
	s->port[1].fc.common_svc = (common_svc_parm *)&s->serv_params;
	s->port[1].fc.class_svcs = (svc_parm *)(s->serv_params + 0x20);
	
	soc_enable (s);
	
	SOD(("Enabled SOC\n"))
}

#ifndef MODULE
int __init soc_probe(void)
#else
int init_module(void)
#endif
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = 0;
	struct soc *s;
	int cards = 0;

	for_each_sbus(bus) {
		for_each_sbusdev(sdev, bus) {
			if(!strcmp(sdev->prom_name, "SUNW,soc")) {
				soc_init(sdev, cards);
				cards++;
			}
		}
	}
	if (!cards) return -EIO;

	for_each_soc(s)
		if (s->next)
			s->port[1].fc.next = &s->next->port[0].fc;
	fcp_init (&socs->port[0].fc);
	return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE
void cleanup_module(void)
{
	struct soc *s;
	int irq;
	struct linux_sbus_device *sdev;
	
	for_each_soc(s) {
		irq = s->port[0].fc.irq;
		disable_irq (irq);
		free_irq (irq, s);

		fcp_release(&(s->port[0].fc), 2);

		sdev = s->port[0].fc.dev;
		if (sdev->num_registers == 1)
			sparc_free_io ((char *)s->xram, sdev->reg_addrs [0].reg_size);
		else {
			sparc_free_io ((char *)s->xram, sdev->reg_addrs [1].reg_size);
			sparc_free_io ((char *)s->regs, sdev->reg_addrs [2].reg_size);
		}
		/* FIXME: sparc_dvma_free() ??? */
	}
}
#endif
