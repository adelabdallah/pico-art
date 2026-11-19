#pragma once

// Poll-mode lwIP for one TCP client and an mDNS hostname.
// Buffers stay small because the session carries keypresses and short lines.

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define SYS_LIGHTWEIGHT_PROT 0

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_IGMP 1
#define LWIP_MDNS_RESPONDER 1
#define LWIP_MDNS_SEARCH 0

#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000

#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 8
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#define PBUF_POOL_SIZE 8

#define TCP_MSS 512
#define TCP_WND (2 * TCP_MSS)
#define TCP_SND_BUF (2 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define LWIP_NUM_NETIF_CLIENT_DATA 2
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#define MDNS_MAX_SERVICES 1
#define MDNS_MAX_STORED_PKTS 2
#define MDNS_RESP_USENETIF_EXTCALLBACK 1

#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_STATS 0
