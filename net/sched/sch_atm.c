/* net/sched/sch_atm.c - ATM VC selection "queueing discipline" */

/* Written 1998,1999 by Werner Almesberger, EPFL ICA */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/atmdev.h>
#include <linux/atmclip.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/file.h> /* for fput */
#include <net/pkt_sched.h>
#include <net/sock.h>


extern struct socket *sockfd_lookup(int fd, int *err); /* @@@ fix this */
#define sockfd_put(sock) fput((sock)->file)	/* @@@ copied because it's
						   __inline__ in socket.c */


#if 1 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0 /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif


/*
 * The ATM queuing discipline provides a framework for invoking classifiers
 * (aka "filters"), which in turn select classes of this queuing discipline.
 * Each class maps the flow(s) it is handling to a given VC. Multiple classes
 * may share the same VC.
 *
 * When creating a class, VCs are specified by passing the number of the open
 * socket descriptor by which the calling process references the VC. The kernel
 * keeps the VC open at least until all classes using it are removed.
 *
 * In this file, most functions are named atm_tc_* to avoid confusion with all
 * the atm_* in net/atm. This naming convention differs from what's used in the
 * rest of net/sched.
 *
 * Known bugs:
 *  - sometimes messes up the IP stack
 *  - any manipulations besides the few operations described in the README, are
 *    untested and likely to crash the system
 *  - should lock the flow while there is data in the queue (?)
 */


#define PRIV(sch) ((struct atm_qdisc_data *) (sch)->data)


struct atm_flow_data {
	struct Qdisc		*q;		/* FIFO, TBF, etc. */
	struct tcf_proto	*filter_list;
	struct atm_vcc		*vcc;		/* VCC; NULL if VCC is closed */
	struct socket		*sock;		/* for closing */
	u32			classid;	/* x:y type ID */
	int			ref;		/* reference count */
	struct tc_stats		stats;
	struct atm_flow_data	*next;
	struct atm_flow_data	*excess;	/* flow for excess traffic;
						   NULL to set CLP instead */
	int			hdr_len;
	unsigned char		hdr[0];		/* header data; MUST BE LAST */
};

struct atm_qdisc_data {
	struct atm_flow_data	link;		/* unclassified skbs go here */
	struct atm_flow_data	*flows;		/* NB: "link" is also on this
						   list */
};


/* ------------------------- Class/flow operations ------------------------- */


static int find_flow(struct atm_qdisc_data *qdisc,struct atm_flow_data *flow)
{
	struct atm_flow_data *walk;

	DPRINTK("find_flow(qdisc %p,flow %p)\n",qdisc,flow);
	for (walk = qdisc->flows; walk; walk = walk->next)
		if (walk == flow) return 1;
	DPRINTK("find_flow: not found\n");
	return 0;
}


static __inline__ struct atm_flow_data *lookup_flow(struct Qdisc *sch,
    u32 classid)
{
	struct atm_flow_data *flow;

        for (flow = PRIV(sch)->flows; flow; flow = flow->next)
		if (flow->classid == classid) break;
	return flow;
}


static int atm_tc_graft(struct Qdisc *sch,unsigned long arg,
    struct Qdisc *new,struct Qdisc **old)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) arg;

	DPRINTK("atm_tc_graft(sch %p,[qdisc %p],flow %p,new %p,old %p)\n",sch,
	    p,flow,new,old);
	if (!find_flow(p,flow)) return -EINVAL;
	if (!new) new = &noop_qdisc;
	*old = xchg(&flow->q,new);
	if (*old) qdisc_reset(*old);
        return 0;
}


static struct Qdisc *atm_tc_leaf(struct Qdisc *sch,unsigned long cl)
{
	struct atm_flow_data *flow = (struct atm_flow_data *) cl;

	DPRINTK("atm_tc_leaf(sch %p,flow %p)\n",sch,flow);
	return flow ? flow->q : NULL;
}


static unsigned long atm_tc_get(struct Qdisc *sch,u32 classid)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;

	DPRINTK("atm_tc_get(sch %p,[qdisc %p],classid %x)\n",sch,p,classid);
	flow = lookup_flow(sch,classid);
        if (flow) flow->ref++;
	DPRINTK("atm_tc_get: flow %p\n",flow);
	return (unsigned long) flow;
}


static unsigned long atm_tc_bind_filter(struct Qdisc *sch,
    unsigned long parent, u32 classid)
{
	return atm_tc_get(sch,classid);
}


/*
 * atm_tc_put handles all destructions, including the ones that are explicitly
 * requested (atm_tc_destroy, etc.). The assumption here is that we never drop
 * anything that still seems to be in use.
 */

static void atm_tc_put(struct Qdisc *sch, unsigned long cl)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) cl;
	struct atm_flow_data **prev;
	struct tcf_proto *filter;

	DPRINTK("atm_tc_put(sch %p,[qdisc %p],flow %p)\n",sch,p,flow);
	if (--flow->ref) return;
	DPRINTK("atm_tc_put: destroying\n");
	for (prev = &p->flows; *prev; prev = &(*prev)->next)
		if (*prev == flow) break;
	if (!*prev) {
		printk(KERN_CRIT "atm_tc_put: class %p not found\n",flow);
		return;
	}
	*prev = flow->next;
	DPRINTK("atm_tc_put: qdisc %p\n",flow->q);
	qdisc_destroy(flow->q);
	while ((filter = flow->filter_list)) {
		DPRINTK("atm_tc_put: destroying filter %p\n",filter);
		flow->filter_list = filter->next;
		DPRINTK("atm_tc_put: filter %p\n",filter);
		filter->ops->destroy(filter);
	}
	if (flow->sock) {
		DPRINTK("atm_tc_put: f_count %d\n",file_count(flow->sock->file));
		sockfd_put(flow->sock);
	}
	if (flow->excess) atm_tc_put(sch,(unsigned long) flow->excess);
	if (flow != &p->link) kfree(flow);
	/*
	 * If flow == &p->link, the qdisc no longer works at this point and
	 * needs to be removed. (By the caller of atm_tc_put.)
	 */
}


static int atm_tc_change(struct Qdisc *sch, u32 classid, u32 parent,
    struct rtattr **tca, unsigned long *arg)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) *arg;
	struct atm_flow_data *excess = NULL;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_ATM_MAX];
	struct socket *sock;
	int fd,error,hdr_len;
	void *hdr;

	DPRINTK("atm_tc_change(sch %p,[qdisc %p],classid %x,parent %x,"
	    "flow %p,opt %p)\n",sch,p,classid,parent,flow,opt);
	/*
	 * The concept of parents doesn't apply for this qdisc.
	 */
	if (parent && parent != TC_H_ROOT && parent != sch->handle)
		return -EINVAL;
	/*
	 * ATM classes cannot be changed. In order to change properties of the
	 * ATM connection, that socket needs to be modified directly (via the
	 * native ATM API. In order to send a flow to a different VC, the old
	 * class needs to be removed and a new one added. (This may be changed
	 * later.)
	 */
	if (flow) return -EBUSY;
	if (opt == NULL || rtattr_parse(tb,TCA_ATM_MAX,RTA_DATA(opt),
	    RTA_PAYLOAD(opt))) return -EINVAL;
	if (!tb[TCA_ATM_FD-1] || RTA_PAYLOAD(tb[TCA_ATM_FD-1]) < sizeof(fd))
		return -EINVAL;
	fd = *(int *) RTA_DATA(tb[TCA_ATM_FD-1]);
	DPRINTK("atm_tc_change: fd %d\n",fd);
	if (tb[TCA_ATM_HDR-1]) {
		hdr_len = RTA_PAYLOAD(tb[TCA_ATM_HDR-1]);
		hdr = RTA_DATA(tb[TCA_ATM_HDR-1]);
	}
	else {
		hdr_len = RFC1483LLC_LEN;
		hdr = NULL; /* default LLC/SNAP for IP */
	}
	if (!tb[TCA_ATM_EXCESS-1]) excess = NULL;
	else {
		if (RTA_PAYLOAD(tb[TCA_ATM_EXCESS-1]) != sizeof(u32))
			return -EINVAL;
		excess = (struct atm_flow_data *) atm_tc_get(sch,
		    *(u32 *) RTA_DATA(tb[TCA_ATM_EXCESS-1]));
		if (!excess) return -ENOENT;
	}
	DPRINTK("atm_tc_change: type %d, payload %d, hdr_len %d\n",
	    opt->rta_type,RTA_PAYLOAD(opt),hdr_len);
	if (!(sock = sockfd_lookup(fd,&error))) return error; /* f_count++ */
	DPRINTK("atm_tc_change: f_count %d\n",file_count(sock->file));
        if (sock->ops->family != PF_ATMSVC && sock->ops->family != PF_ATMPVC) {
		error = -EPROTOTYPE;
                goto err_out;
	}
	/* @@@ should check if the socket is really operational or we'll crash
	   on vcc->dev->ops->send */
	if (classid) {
		if (TC_H_MAJ(classid ^ sch->handle)) {
			DPRINTK("atm_tc_change: classid mismatch\n");
			error = -EINVAL;
			goto err_out;
		}
		if (find_flow(p,flow)) {
			error = -EEXIST;
			goto err_out;
		}
	}
	else {
		int i;
		unsigned long cl;

		for (i = 1; i < 0x8000; i++) {
			classid = TC_H_MAKE(sch->handle,0x8000 | i);
			if (!(cl = atm_tc_get(sch,classid))) break;
			atm_tc_put(sch,cl);
		}
	}
	DPRINTK("atm_tc_change: new id %x\n",classid);
	flow = kmalloc(sizeof(struct atm_flow_data)+hdr_len,GFP_KERNEL);
	DPRINTK("atm_tc_change: flow %p\n",flow);
	if (!flow) {
		error = -ENOBUFS;
		goto err_out;
	}
	memset(flow,0,sizeof(*flow));
	flow->filter_list = NULL;
	if (!(flow->q = qdisc_create_dflt(sch->dev,&pfifo_qdisc_ops)))
		flow->q = &noop_qdisc;
	DPRINTK("atm_tc_change: qdisc %p\n",flow->q);
	flow->sock = sock;
        flow->vcc = ATM_SD(sock); /* speedup */
        DPRINTK("atm_tc_change: vcc %p\n",flow->vcc);
	flow->classid = classid;
	flow->ref = 1;
	flow->excess = excess;
	flow->next = p->link.next;
	p->link.next = flow;
	flow->hdr_len = hdr_len;
	if (hdr) memcpy(flow->hdr,hdr,hdr_len);
	else {
		memcpy(flow->hdr,llc_oui,sizeof(llc_oui));
		((u16 *) flow->hdr)[3] = htons(ETH_P_IP);
	}
	*arg = (unsigned long) flow;
	return 0;
err_out:
	if (excess) atm_tc_put(sch,(unsigned long) excess);
	sockfd_put(sock);
	return error;
}


static int atm_tc_delete(struct Qdisc *sch,unsigned long arg)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) arg;

	DPRINTK("atm_tc_delete(sch %p,[qdisc %p],flow %p)\n",sch,p,flow);
	if (!find_flow(PRIV(sch),flow)) return -EINVAL;
	if (flow->filter_list || flow == &p->link) return -EBUSY;
	/*
	 * Reference count must be 2: one for "keepalive" (set at class
	 * creation), and one for the reference held when calling delete.
	 */
	if (flow->ref < 2) {
		printk(KERN_ERR "atm_tc_delete: flow->ref == %d\n",flow->ref);
		return -EINVAL;
	}
	if (flow->ref > 2) return -EBUSY; /* catch references via excess, etc.*/
	atm_tc_put(sch,arg);
	return 0;
}


static void atm_tc_walk(struct Qdisc *sch,struct qdisc_walker *walker)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;

	DPRINTK("atm_tc_walk(sch %p,[qdisc %p],walker %p)\n",sch,p,walker);
	if (walker->stop) return;
	for (flow = p->flows; flow; flow = flow->next) {
		if (walker->count >= walker->skip)
			if (walker->fn(sch,(unsigned long) flow,walker) < 0) {
				walker->stop = 1;
				break;
			}
		walker->count++;
	}
}


static struct tcf_proto **atm_tc_find_tcf(struct Qdisc *sch,unsigned long cl)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) cl;

	DPRINTK("atm_tc_find_tcf(sch %p,[qdisc %p],flow %p)\n",sch,p,flow);
        return flow ? &flow->filter_list : &p->link.filter_list;
}


/* --------------------------- Qdisc operations ---------------------------- */


static int atm_tc_enqueue(struct sk_buff *skb,struct Qdisc *sch)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = NULL ; /* @@@ */
	struct tcf_result res;
	int result;
	int ret = NET_XMIT_POLICED;

	D2PRINTK("atm_tc_enqueue(skb %p,sch %p,[qdisc %p])\n",skb,sch,p);
	result = TC_POLICE_OK; /* be nice to gcc */
	if (TC_H_MAJ(skb->priority) != sch->handle ||
	    !(flow = (struct atm_flow_data *) atm_tc_get(sch,skb->priority)))
		for (flow = p->flows; flow; flow = flow->next)
			if (flow->filter_list) {
				result = tc_classify(skb,flow->filter_list,
				    &res);
				if (result < 0) continue;
				flow = (struct atm_flow_data *) res.class;
				if (!flow) flow = lookup_flow(sch,res.classid);
				break;
			}
	if (!flow) flow = &p->link;
	else {
		if (flow->vcc)
			ATM_SKB(skb)->atm_options = flow->vcc->atm_options;
			/*@@@ looks good ... but it's not supposed to work :-)*/
#ifdef CONFIG_NET_CLS_POLICE
		switch (result) {
			case TC_POLICE_SHOT:
				kfree_skb(skb);
				break;
			case TC_POLICE_RECLASSIFY:
				if (flow->excess) flow = flow->excess;
				else {
					ATM_SKB(skb)->atm_options |=
					    ATM_ATMOPT_CLP;
					break;
				}
				/* fall through */
			case TC_POLICE_OK:
				/* fall through */
			default:
				break;
		}
#endif
	}
	if (
#ifdef CONFIG_NET_CLS_POLICE
	    result == TC_POLICE_SHOT ||
#endif
	    (ret = flow->q->enqueue(skb,flow->q)) != 0) {
		sch->stats.drops++;
		if (flow) flow->stats.drops++;
		return ret;
	}
	sch->stats.bytes += skb->len;
	sch->stats.packets++;
	flow->stats.bytes += skb->len;
	flow->stats.packets++;
	sch->q.qlen++;
	return 0;
}


static struct sk_buff *atm_tc_dequeue(struct Qdisc *sch)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;
	struct sk_buff *skb;

	D2PRINTK("atm_tc_dequeue(sch %p,[qdisc %p])\n",sch,p);
	for (flow = p->link.next; flow; flow = flow->next)
		/*
		 * If traffic is properly shaped, this won't generate nasty
		 * little bursts. Otherwise, it may ... @@@
		 */
		while ((skb = flow->q->dequeue(flow->q))) {
			sch->q.qlen--;
			D2PRINTK("atm_tc_deqeueue: sending on class %p\n",flow);
			/* remove any LL header somebody else has attached */
			skb_pull(skb,(char *) skb->nh.iph-(char *) skb->data);
			if (skb_headroom(skb) < flow->hdr_len) {
				struct sk_buff *new;

				new = skb_realloc_headroom(skb,flow->hdr_len);
				dev_kfree_skb(skb);
				if (!new) continue;
				skb = new;
			}
			D2PRINTK("atm_tc_dequeue: ip %p, data %p\n",
			    skb->nh.iph,skb->data);
			ATM_SKB(skb)->vcc = flow->vcc;
			memcpy(skb_push(skb,flow->hdr_len),flow->hdr,
			    flow->hdr_len);
			atomic_add(skb->truesize,&flow->vcc->tx_inuse);
			ATM_SKB(skb)->iovcnt = 0;
			/* atm.atm_options are already set by atm_tc_enqueue */
			(void) flow->vcc->dev->ops->send(flow->vcc,skb);
		}
	skb = p->link.q->dequeue(p->link.q);
	if (skb) sch->q.qlen--;
	return skb;
}


static int atm_tc_drop(struct Qdisc *sch)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;

	DPRINTK("atm_tc_drop(sch %p,[qdisc %p])\n",sch,p);
	for (flow = p->flows; flow; flow = flow->next)
		if (flow->q->ops->drop && flow->q->ops->drop(flow->q))
			return 1;
	return 0;
}


static int atm_tc_init(struct Qdisc *sch,struct rtattr *opt)
{
	struct atm_qdisc_data *p = PRIV(sch);

	DPRINTK("atm_tc_init(sch %p,[qdisc %p],opt %p)\n",sch,p,opt);
	memset(p,0,sizeof(*p));
	p->flows = &p->link;
	if(!(p->link.q = qdisc_create_dflt(sch->dev,&pfifo_qdisc_ops)))
		p->link.q = &noop_qdisc;
	DPRINTK("atm_tc_init: link (%p) qdisc %p\n",&p->link,p->link.q);
	p->link.filter_list = NULL;
	p->link.vcc = NULL;
	p->link.sock = NULL;
	p->link.classid = sch->handle;
	p->link.ref = 1;
	p->link.next = NULL;
	MOD_INC_USE_COUNT;
	return 0;
}


static void atm_tc_reset(struct Qdisc *sch)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;

	DPRINTK("atm_tc_reset(sch %p,[qdisc %p])\n",sch,p);
	for (flow = p->flows; flow; flow = flow->next) qdisc_reset(flow->q);
	sch->q.qlen = 0;
}


static void atm_tc_destroy(struct Qdisc *sch)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow;

	DPRINTK("atm_tc_destroy(sch %p,[qdisc %p])\n",sch,p);
	/* races ? */
	while ((flow = p->flows)) {
		if (flow->ref > 1)
			printk(KERN_ERR "atm_destroy: %p->ref = %d\n",flow,
			    flow->ref);
		atm_tc_put(sch,(unsigned long) flow);
		if (p->flows == flow) {
			printk(KERN_ERR "atm_destroy: putting flow %p didn't "
			    "kill it\n",flow);
			p->flows = flow->next; /* brute force */
			break;
		}
	}
	MOD_DEC_USE_COUNT;
}


#ifdef CONFIG_RTNETLINK

static int atm_tc_dump_class(struct Qdisc *sch, unsigned long cl,
    struct sk_buff *skb, struct tcmsg *tcm)
{
	struct atm_qdisc_data *p = PRIV(sch);
	struct atm_flow_data *flow = (struct atm_flow_data *) cl;
	unsigned char *b = skb->tail;
	struct rtattr *rta;

	DPRINTK("atm_tc_dump_class(sch %p,[qdisc %p],flow %p,skb %p,tcm %p)\n",
	    sch,p,flow,skb,tcm);
	if (!find_flow(p,flow)) return -EINVAL;
	tcm->tcm_handle = flow->classid;
	rta = (struct rtattr *) b;
	RTA_PUT(skb,TCA_OPTIONS,0,NULL);
	RTA_PUT(skb,TCA_ATM_HDR,flow->hdr_len,flow->hdr);
	if (flow->vcc) {
		struct sockaddr_atmpvc pvc;
		int state;

		pvc.sap_family = AF_ATMPVC;
		pvc.sap_addr.itf = flow->vcc->dev ? flow->vcc->dev->number : -1;
		pvc.sap_addr.vpi = flow->vcc->vpi;
		pvc.sap_addr.vci = flow->vcc->vci;
		RTA_PUT(skb,TCA_ATM_ADDR,sizeof(pvc),&pvc);
		state = ATM_VF2VS(flow->vcc->flags);
		RTA_PUT(skb,TCA_ATM_STATE,sizeof(state),&state);
	}
	if (flow->excess)
		RTA_PUT(skb,TCA_ATM_EXCESS,sizeof(u32),&flow->classid);
	else {
		static u32 zero = 0;

		RTA_PUT(skb,TCA_ATM_EXCESS,sizeof(zero),&zero);
	}
	rta->rta_len = skb->tail-b;
	return skb->len;

rtattr_failure:
	skb_trim(skb,b-skb->data);
	return -1;
}

static int atm_tc_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	return 0;
}

#endif


static struct Qdisc_class_ops atm_class_ops =
{
	atm_tc_graft,			/* graft */
	atm_tc_leaf,			/* leaf */
	atm_tc_get,			/* get */
	atm_tc_put,			/* put */
	atm_tc_change,			/* change */
	atm_tc_delete,			/* delete */
	atm_tc_walk,			/* walk */

	atm_tc_find_tcf,		/* tcf_chain */
	atm_tc_bind_filter,		/* bind_tcf */
	atm_tc_put,			/* unbind_tcf */

#ifdef CONFIG_RTNETLINK
	atm_tc_dump_class,		/* dump */
#endif
};

struct Qdisc_ops atm_qdisc_ops =
{
	NULL,				/* next */
	&atm_class_ops,			/* cl_ops */
	"atm",
	sizeof(struct atm_qdisc_data),

	atm_tc_enqueue,			/* enqueue */
	atm_tc_dequeue,			/* dequeue */
	atm_tc_enqueue,			/* requeue; we're cheating a little */
	atm_tc_drop,			/* drop */

	atm_tc_init,			/* init */
	atm_tc_reset,			/* reset */
	atm_tc_destroy,			/* destroy */
	NULL,				/* change */

#ifdef CONFIG_RTNETLINK
	atm_tc_dump			/* dump */
#endif
};


#ifdef MODULE
int init_module(void)
{
	return register_qdisc(&atm_qdisc_ops);
}


void cleanup_module(void) 
{
	unregister_qdisc(&atm_qdisc_ops);
}
#endif
