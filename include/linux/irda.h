/*********************************************************************
 *                
 * Filename:      irda.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Mar  8 14:06:12 1999
 * Modified at:   Sun Oct 10 23:00:59 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef KERNEL_IRDA_H
#define KERNEL_IRDA_H

/* Hint bit positions for first hint byte */
#define HINT_PNP         0x01
#define HINT_PDA         0x02
#define HINT_COMPUTER    0x04
#define HINT_PRINTER     0x08
#define HINT_MODEM       0x10
#define HINT_FAX         0x20
#define HINT_LAN         0x40
#define HINT_EXTENSION   0x80

/* Hint bit positions for second hint byte (first extension byte) */
#define HINT_TELEPHONY   0x01
#define HINT_FILE_SERVER 0x02
#define HINT_COMM        0x04
#define HINT_MESSAGE     0x08
#define HINT_HTTP        0x10
#define HINT_OBEX        0x20

/* IrLMP character code values */
#define CS_ASCII         0x00
#define	CS_ISO_8859_1    0x01
#define	CS_ISO_8859_2    0x02
#define	CS_ISO_8859_3    0x03
#define	CS_ISO_8859_4    0x04
#define	CS_ISO_8859_5    0x05
#define	CS_ISO_8859_6    0x06
#define	CS_ISO_8859_7    0x07
#define	CS_ISO_8859_8    0x08
#define	CS_ISO_8859_9    0x09
#define CS_UNICODE       0xff

/* These are the currently known dongles */
typedef enum {
	IRDA_TEKRAM_DONGLE,
	IRDA_ESI_DONGLE,
	IRDA_ACTISYS_DONGLE,
	IRDA_ACTISYS_PLUS_DONGLE,
	IRDA_GIRBIL_DONGLE,
	IRDA_LITELINK_DONGLE,
	IRDA_AIRPORT_DONGLE,
} IRDA_DONGLE;

#define SOL_IRLMP      266 /* Same as SOL_IRDA for now */
#define SOL_IRTTP      266 /* Same as SOL_IRDA for now */

#define IRLMP_ENUMDEVICES        1
#define IRLMP_IAS_SET            2
#define IRLMP_IAS_QUERY          3
#define IRLMP_DISCOVERY_MASK_SET 4

#define IRTTP_QOS_SET            5
#define IRTTP_QOS_GET            6
#define IRTTP_MAX_SDU_SIZE       7

#define IAS_MAX_STRING         256
#define IAS_MAX_OCTET_STRING  1024
#define IAS_MAX_CLASSNAME       64
#define IAS_MAX_ATTRIBNAME     256

#define LSAP_ANY               0xff

struct sockaddr_irda {
	sa_family_t   sir_family;   /* AF_IRDA */
	unsigned char sir_lsap_sel; /* LSAP/TSAP selector */
	unsigned int  sir_addr;     /* Device address */
	char          sir_name[25]; /* Usually <service>:IrDA:TinyTP */
};

struct irda_device_info {
	unsigned int  saddr;        /* Address of remote device */
	unsigned int  daddr;        /* Link where it was discovered */
	char          info[22];     /* Description */
	unsigned char charset;      /* Charset used for description */
	unsigned char hints[2];     /* Hint bits */
};

struct irda_device_list {
       unsigned int len;
       struct irda_device_info dev[1];
};

struct irda_ias_set {
	char irda_class_name[IAS_MAX_CLASSNAME];
	char irda_attrib_name[IAS_MAX_ATTRIBNAME];
	unsigned int irda_attrib_type;
	union {
		unsigned int irda_attrib_int;
		struct {
			unsigned short len;
			u_char OctetSeq[IAS_MAX_OCTET_STRING];
		} irda_attrib_octet_seq;
		struct {
			unsigned char len;
			unsigned char charset;
			unsigned char string[IAS_MAX_STRING];
		} irda_attrib_string;
	} attribute;
};

/* Some private IOCTL's (max 16) */
#define SIOCSDONGLE    (SIOCDEVPRIVATE + 0)
#define SIOCGDONGLE    (SIOCDEVPRIVATE + 1)
#define SIOCSBANDWIDTH (SIOCDEVPRIVATE + 2)
#define SIOCSMEDIABUSY (SIOCDEVPRIVATE + 3)
#define SIOCGMEDIABUSY (SIOCDEVPRIVATE + 4)
#define SIOCGRECEIVING (SIOCDEVPRIVATE + 5)
#define SIOCSRAWMODE   (SIOCDEVPRIVATE + 6)
#define SIOCSDTRRTS    (SIOCDEVPRIVATE + 7)
#define SIOCGQOS       (SIOCDEVPRIVATE + 8)

/* No reason to include <linux/if.h> just because of this one ;-) */
#define IRNAMSIZ 16 

/* IrDA quality of service information (must not exceed 16 bytes) */
struct if_irda_qos {
	unsigned long  baudrate;
	unsigned short data_size;
	unsigned short window_size;
	unsigned short min_turn_time;
	unsigned short max_turn_time;
	unsigned char  add_bofs;
	unsigned char  link_disc;
};

/* For setting RTS and DTR lines of a dongle */
struct if_irda_line {
	unsigned char dtr;
	unsigned char rts;
};

/* IrDA interface configuration (data part must not exceed 16 bytes) */
struct if_irda_req {
	union {
		char ifrn_name[IRNAMSIZ];  /* if name, e.g. "irda0" */
	} ifr_ifrn;
	
	/* Data part */
	union {
		struct if_irda_line ifru_line;
		struct if_irda_qos  ifru_qos;
		unsigned short      ifru_flags;
		unsigned int        ifru_receiving;
		unsigned int        ifru_raw_mode;
		unsigned int        ifru_dongle;
	} ifr_ifru;
};

#define ifr_baudrate  ifr_ifru.ifru_qos.baudrate
#define ifr_receiving ifr_ifru.ifru_receiving 
#define ifr_dongle    ifr_ifru.ifru_dongle
#define ifr_raw_mode  ifr_ifru.ifru_raw_mode
#define ifr_dtr       ifr_ifru.ifru_line.dtr
#define ifr_rts       ifr_ifru.ifru_line.rts

#endif /* KERNEL_IRDA_H */




