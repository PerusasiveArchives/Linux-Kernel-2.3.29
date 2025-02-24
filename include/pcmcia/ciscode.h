/*
 * ciscode.h 1.39 1999/10/25 20:23:17
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_CISCODE_H
#define _LINUX_CISCODE_H

/* Manufacturer and Product ID codes */

#define MANFID_3COM			0x0101
#define PRODID_3COM_3CXEM556		0x0035
#define PRODID_3COM_3CCFEM556		0x0556
#define PRODID_3COM_3C562		0x0562

#define MANFID_ACCTON			0x01bf
#define PRODID_ACCTON_EN2226		0x010a

#define MANFID_ADAPTEC			0x012f
#define PRODID_ADAPTEC_SCSI		0x0001

#define MANFID_ATT			0xffff
#define PRODID_ATT_KIT			0x0100

#define MANFID_CONTEC			0xc001

#define MANFID_FUJITSU			0x0004
#define PRODID_FUJITSU_MBH10302		0x0004
#define PRODID_FUJITSU_MBH10304		0x1003
#define PRODID_FUJITSU_LA501		0x2000

#define MANFID_IBM			0x00a4
#define PRODID_IBM_HOME_AND_AWAY	0x002e

#define MANFID_INTEL			0x0089
#define PRODID_INTEL_DUAL_RS232		0x0301
#define PRODID_INTEL_2PLUS		0x8422

#define MANFID_LINKSYS			0x0143
#define PRODID_LINKSYS_PCMLM28		0xc0ab
#define PRODID_LINKSYS_3400		0x3341

#define MANFID_MEGAHERTZ		0x0102
#define PRODID_MEGAHERTZ_VARIOUS	0x0000
#define PRODID_MEGAHERTZ_EM3288		0x0006

#define MANFID_MACNICA			0xc00b

#define MANFID_MOTOROLA			0x0109
#define PRODID_MOTOROLA_MARINER		0x0501

#define MANFID_NATINST			0x010b
#define PRODID_NATINST_QUAD_RS232	0xd180

#define MANFID_NEW_MEDIA		0x0057

#define MANFID_OLICOM			0x0121
#define PRODID_OLICOM_OC2231		0x3122
#define PRODID_OLICOM_OC2232		0x3222

#define MANFID_OMEGA			0x0137
#define PRODID_OMEGA_QSP_100		0x0025

#define MANFID_OSITECH			0x0140
#define PRODID_OSITECH_JACK_144		0x0001
#define PRODID_OSITECH_JACK_288		0x0002
#define PRODID_OSITECH_JACK_336		0x0007
#define PRODID_OSITECH_SEVEN		0x0008

#define MANFID_PSION			0x016c

#define MANFID_QUATECH			0x0137
#define PRODID_QUATECH_SPP100		0x0003
#define PRODID_QUATECH_DUAL_RS232	0x0012
#define PRODID_QUATECH_DUAL_RS232_D1	0x0007
#define PRODID_QUATECH_QUAD_RS232	0x001b

#define MANFID_SMC			0x0108
#define PRODID_SMC_ETHER		0x0105

#define MANFID_SOCKET			0x0104
#define PRODID_SOCKET_DUAL_RS232	0x0006
#define PRODID_SOCKET_LPE		0x000d

#define MANFID_SUNDISK			0x0045

#define MANFID_TDK			0x0105

#define MANFID_XIRCOM			0x0105

#endif /* _LINUX_CISCODE_H */
