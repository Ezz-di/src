/*	$OpenBSD: encap.h,v 1.6 1997/07/02 06:58:40 provos Exp $	*/

/*
 * The author of this code is John Ioannidis, ji@tla.org,
 * 	(except when noted otherwise).
 *
 * This code was written for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis, kermit@forthnet.gr.
 *
 * Copyright (C) 1995, 1996, 1997 by John Ioannidis and Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NEITHER AUTHOR MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * encap.h
 *
 * Declarations useful in the encapsulation code.
 */

/*
 * Definitions for encapsulation-related phenomena.
 *
 * A lot of encapsulation protocols (ipip, swipe, ip_encap, ipsp, etc.)
 * select their tunnel based on the destination (and sometimes the source)
 * of the packet. The encap address/protocol family provides a generic
 * mechanism for specifying tunnels.
 */

/*
 * A tunnel is characterized by which source/destination address pairs
 * (with netmasks) it is valid for (the "destination" as far as the
 * routing code is concerned), and what the source (local) and destination
 * (remote) endpoints of the tunnel, and the SPI, should be (the "gateway"
 * as far as the routing code is concerned.
 */
  
struct sockaddr_encap
{
    u_int8_t	sen_len;		/* length */
    u_int8_t	sen_family;		/* AF_ENCAP */
    u_int16_t	sen_type;		/* see SENT_* */
    union
    {
	u_int8_t	Data[16];	/* other stuff mapped here */

	struct				/* SENT_IP4 */
	{
	    struct in_addr Src;
	    struct in_addr Dst;
	    u_int16_t Sport;
	    u_int16_t Dport;
	    u_int8_t Proto;
	    u_int8_t Filler[3];
	} Sip4;

	struct				/* SENT_IPSP */
	{
	    struct in_addr Dst;
	    u_int32_t Spi;
	    u_int8_t Filler[8];
	} Sipsp;
    } Sen;
};

#define PFENCAP_VERSION_0	0
#define PFENCAP_VERSION_1	1

#define sen_data	Sen.Data
#define sen_ip_src	Sen.Sip4.Src
#define sen_ip_dst	Sen.Sip4.Dst
#define sen_proto	Sen.Sip4.Proto
#define sen_sport	Sen.Sip4.Sport
#define sen_dport	Sen.Sip4.Dport
#define sen_ipsp_dst	Sen.Sipsp.Dst
#define sen_ipsp_spi	Sen.Sipsp.Spi

/*
 * The "type" is really part of the address as far as the routing
 * system is concerned. By using only one bit in the type field
 * for each type, we sort-of make sure that different types of
 * encapsulation addresses won't be matched against the wrong type.
 * 
 */

#define SENT_IP4	0x0001		/* data is two struct in_addr */
#define SENT_IPSP	0x0002		/* data as in IP4 plus SPI */

/*
 * SENT_HDRLEN is the length of the "header"
 * SENT_*_LEN are the lengths of various forms of sen_data
 * SENT_*_OFF are the offsets in the sen_data array of various fields
 */

#define SENT_HDRLEN	(2 * sizeof(u_int8_t) + sizeof(u_int16_t))

#define SENT_IP4_SRCOFF	(0)
#define SENT_IP4_DSTOFF (sizeof (struct in_addr))

#define SENT_IP4_LEN	20
#define SENT_IPSP_LEN	20

/*
 * Tunnel descriptors are setup and torn down using a socket of the 
 * AF_ENCAP domain. The following defines the messages that can
 * be sent down that socket.
 */
struct encap_msghdr
{
    u_int16_t	em_msglen;		/* message length */
    u_int8_t	em_version;		/* for future expansion */
    u_int8_t	em_type;		/* message type */
    u_int32_t   foo;                    /* Alignment to 64 bit */
    union
    {
	/* 
	 * This is used to set/change the attributes of an SPI. If oSrc and
	 * oDst are set to non-zero values, the SPI will also do IP-in-IP
	 * encapsulation (tunneling). If only one of them is set, an error
	 * is returned. Both zero implies transport mode.
	 */
	struct
	{
	    u_int32_t      Spi;		/* SPI */
	    int32_t        Alg;		/* Algorithm to use */
	    struct in_addr Dst;		/* Destination address */
	    struct in_addr Src;		/* This is used to set our source
					 * address when doing tunneling and
					 * the outgoing packet does not
					 * have a source address (is zero) */
	    struct in_addr oSrc;	/* Source... */
	    struct in_addr oDst;	/* ...and destination in outter IP
					 * header, if we're doing IP-in-IP */
	    u_int64_t      Relative_Hard; /* Expire relative to creation */
	    u_int64_t      Relative_Soft;
	    u_int64_t      First_Use_Hard; /* Expire relative to first use */
	    u_int64_t      First_Use_Soft;
	    u_int64_t      Expire_Hard;	/* Expire at fixed point in time */
	    u_int64_t      Expire_Soft;
	    u_int64_t      Bytes_Hard;	/* Expire after bytes recved/sent */
	    u_int64_t      Bytes_Soft;
	    u_int64_t      Packets_Hard; /* Expire after packets recved/sent */
	    u_int64_t      Packets_Soft;
	    int32_t	   TTL;		/* When tunneling, what TTL to use.
					 * If set to IP4_SAME_TTL, the ttl
					 * from the encapsulated packet will
					 * be copied. If set to IP4_DEFAULT_TTL,
					 * the system default TTL will be used.
					 * If set to anything else, then the
					 * ttl used will be TTL % 256 */
	    u_int16_t	   Sport; 	/* Source port, if applicable */
	    u_int16_t	   Dport;	/* Destination port, if applicable */
  	    u_int8_t	   Proto;	/* Protocol, if applicable */
	    u_int8_t	   foo[3];	/* Alignment */
	    u_int8_t       Dat[1];	/* Data */
	} Xfm;

	/*
 	 * For expiration notifications, the kernel fills in
	 * Notification_Type, Spi and Dst. No direct response is expected.
	 *
 	 * For SA Requests, the kernel fills in
	 * Notification_Type, MsgID, Spi, Seclevel, Dst (and optionally
	 * Protocol, Src, Sport, Dport and UserID).
 	 *
	 * The response should have the same values in all the fields
	 * and:
	 * Spi/Spi2/Spi3 will hold the SPIs for the three seclevels
	 * UserID can optionally hold the peer's UserID (if applicable)
	 */
	struct				/* kernel->userland notifications */
	{
	    u_int32_t      Notification_Type;
	    u_int32_t      MsgID;	/* Request ID */
	    u_int32_t      Spi;		
	    u_int32_t      Spi2;
	    u_int32_t      Spi3;
	    u_int8_t       Seclevel[3];	/* see netinet/in_pcb.h */
	    u_int8_t       Protocol;	/* Transport mode for which protocol */
	    struct in_addr Dst;		/* Peer */
	    struct in_addr Src;		/* Might have our local address */
	    u_int16_t      Sport;	/* Source port */
            u_int16_t      Dport;	/* Destination port */
	    u_int8_t       UserID[1];	/* Might be used to indicate user */
	} Notify;

	/* Link two SPIs */
	struct
	{
	    u_int32_t        emr_spi;	/* SPI */
	    u_int32_t        emr_spi2;
	    struct in_addr   emr_dst;	/* Dest */
	    struct in_addr   emr_dst2;
	} Rel;

	/* For general use */
	struct 
	{
	    u_int32_t       emg_spi;
	    struct in_addr  emg_dst;
	} Gen;
    } Eu;
};

#define  NOTIFY_SOFT_EXPIRE     0	/* Soft expiration of SA */
#define  NOTIFY_HARD_EXPIRE     1	/* Hard expiration of SA */
#define  NOTIFY_REQUEST_SA      2	/* Establish an SA */

#define em_gen_spi        Eu.Gen.emg_spi
#define em_gen_dst        Eu.Gen.emg_dst

#define em_not_type       Eu.Notify.Notification_Type
#define em_not_spi        Eu.Notify.Spi
#define em_not_spi2       Eu.Notify.Spi2
#define em_not_spi3       Eu.Notify.Spi3
#define em_not_dst        Eu.Notify.Dst
#define em_not_seclevel   Eu.Notify.Seclevel
#define em_not_userid     Eu.Notify.UserID
#define em_not_msgid      Eu.Notify.MsgID
#define em_not_sport      Eu.Notify.Sport
#define em_not_dport      Eu.Notify.Dport
#define em_not_protocol   Eu.Notify.Protocol

#define em_spi	          Eu.Xfm.Spi
#define em_dst	          Eu.Xfm.Dst
#define em_src	          Eu.Xfm.Src
#define em_osrc           Eu.Xfm.oSrc
#define em_odst           Eu.Xfm.oDst
#define em_if	          Eu.Xfm.If
#define em_alg	          Eu.Xfm.Alg
#define em_dat	          Eu.Xfm.Dat
#define em_relative_hard  Eu.Xfm.Relative_Hard
#define em_relative_soft  Eu.Xfm.Relative_Soft
#define em_first_use_hard Eu.Xfm.First_Use_Hard
#define em_first_use_soft Eu.Xfm.First_Use_Soft
#define em_expire_hard    Eu.Xfm.Expire_Hard
#define em_expire_soft    Eu.Xfm.Expire_Soft
#define em_bytes_hard     Eu.Xfm.Bytes_Hard
#define em_bytes_soft     Eu.Xfm.Bytes_Soft
#define em_packets_hard   Eu.Xfm.Packets_Hard
#define em_packets_soft   Eu.Xfm.Packets_Soft
#define em_ttl		  Eu.Xfm.TTL
#define em_proto	  Eu.Xfm.Proto
#define em_sport	  Eu.Xfm.Sport
#define em_dport	  Eu.Xfm.Dport

#define em_rel_spi	  Eu.Rel.emr_spi
#define em_rel_spi2	  Eu.Rel.emr_spi2
#define em_rel_dst	  Eu.Rel.emr_dst
#define em_rel_dst2	  Eu.Rel.emr_dst2

#define EMT_SETSPI	1		/* Set SPI properties */
#define EMT_GRPSPIS	2		/* Group SPIs (output order)  */
#define EMT_DELSPI	3		/* delete an SPI */
#define EMT_DELSPICHAIN 4		/* delete an SPI chain starting from */
#define EMT_RESERVESPI  5		/* Give us an SPI */
#define EMT_ENABLESPI   6		/* Enable an SA */
#define EMT_DISABLESPI  7		/* Disable an SA */
#define EMT_NOTIFY      8		/* kernel->userland key mgmt not. */

/* Total packet lengths */
#define EMT_SETSPI_FLEN	      124
#define EMT_GRPSPIS_FLEN      24
#define EMT_GENLEN            16
#define EMT_DELSPI_FLEN       EMT_GENLEN
#define EMT_DELSPICHAIN_FLEN  EMT_GENLEN
#define EMT_ENABLESPI_FLEN    EMT_GENLEN
#define EMT_DISABLESPI_FLEN   EMT_GENLEN
#define EMT_RESERVESPI_FLEN   EMT_GENLEN
#define EMT_NOTIFY_FLEN       44

#ifdef _KERNEL
extern struct ifaddr *encap_findgwifa(struct sockaddr *);
extern struct ifnet enc_softc;
#endif
