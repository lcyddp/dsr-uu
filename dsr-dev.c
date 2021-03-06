/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erikn@it.uu.se>
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <linux/random.h>
#include <linux/wireless.h>

#include "debug.h"
#include "dsr.h"
#include "neigh.h"
#include "dsr-pkt.h"
#include "dsr-opt.h"
#include "dsr-rreq.h"
#include "link-cache.h"
#include "dsr-srt.h"
#include "dsr-ack.h"
#include "send-buf.h"
#include "maint-buf.h"
#include "dsr-io.h"

/* Our dsr device */
static struct net_device *dsr_dev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/* dsr_node must be static on some older kernels, otherwise it segfaults on
 * module load */
static struct dsr_node *dsr_node;
#else
struct dsr_node *dsr_node;
#endif
static int rp_filter = 0;
static int forwarding = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define DSRUU_IN_DEV_SET_RPFILTER(in_dev, val) (in_dev->cnf.rp_filter = val)
#define DSRUU_IN_DEV_SET_FORWARD(in_dev, val) (in_dev->cnf.forwarding = val)
#else
#define DSRUU_IN_DEV_SET_RPFILTER(in_dev, val) (ipv4_devconf_set(in_dev, NET_IPV4_CONF_RP_FILTER, val))
#define DSRUU_IN_DEV_SET_FORWARD(in_dev, val) (ipv4_devconf_set(in_dev, NET_IPV4_CONF_FORWARDING, val))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
static int dsr_dev_llrecv(struct sk_buff *skb, struct net_device *indev,
			  struct packet_type *pt);
#else

static int dsr_dev_llrecv(struct sk_buff *skb, struct net_device *indev,
			  struct packet_type *pt, struct net_device *orig_dev);
#endif

static struct packet_type dsr_packet_type = {
	.type = __constant_htons(ETH_P_IP),
	.func = dsr_dev_llrecv,
};

struct sk_buff *dsr_skb_create(struct dsr_pkt *dp, struct net_device *dev)
{
	struct sk_buff *skb;
	char *buf;
	int ip_len;
	int tot_len;
	int dsr_opts_len = dsr_pkt_opts_len(dp);

	ip_len = dp->nh.iph->ihl << 2;

	tot_len = ip_len + dsr_opts_len + dp->payload_len;

	LOG_DBG("ip_len=%d dsr_opts_len=%d payload_len=%d tot_len=%d\n",
	      ip_len, dsr_opts_len, dp->payload_len, tot_len);
#ifdef KERNEL26
	skb = alloc_skb(tot_len + LL_RESERVED_SPACE(dev), GFP_ATOMIC);
#else
	skb = alloc_skb(dev->hard_header_len + 15 + tot_len, GFP_ATOMIC);
#endif
	if (!skb) {
		LOG_DBG("alloc_skb failed\n");
		return NULL;
	}

	SKB_SET_MAC_HDR(skb, 0);

	/* We align to 16 bytes, for ethernet: 2 bytes + 14 bytes
	 * header. This will move the skb->data pointer forward. */
#ifdef KERNEL26
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
#else
	skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
#endif
	SKB_SET_NETWORK_HDR(skb, 0);
	       
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	/* Copy in all the headers in the right order */
	buf = skb_put(skb, tot_len);

	memcpy(buf, dp->nh.raw, ip_len);

	/* For some reason the checksum has to be recalculated here, at least
	 * when there is a record route IP option */
	ip_send_check((struct iphdr *)buf);

	buf += ip_len;

	/* Add DSR header if it exists */
	if (dsr_opts_len) {
		memcpy(buf, dp->dh.raw, dsr_opts_len);
		buf += dsr_opts_len;
	}

	/* Add payload */
	if (dp->payload_len && dp->payload)
		memcpy(buf, dp->payload, dp->payload_len);

	return skb;
}

int dsr_hw_header_create(struct dsr_pkt *dp, struct sk_buff *skb)
{

	struct sockaddr broadcast =
	    { AF_UNSPEC, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} };
	struct neighbor_info neigh_info;

	if (dp->dst.s_addr == DSR_BROADCAST)
		memcpy(neigh_info.hw_addr.sa_data, broadcast.sa_data, ETH_ALEN);
	else {
		/* Get hardware destination address */
		if (neigh_tbl_query(dp->nxt_hop, &neigh_info) < 0) {
			LOG_DBG("Could not get hardware address for next hop %s\n",
			     print_ip(dp->nxt_hop));
			return -1;
		}
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	if (skb->dev->hard_header) {
		skb->dev->hard_header(skb, skb->dev, ETH_P_IP,
				      neigh_info.hw_addr.sa_data, 0, skb->len);
	} else {
		LOG_DBG("Missing hard_header\n");
		return -1;
	}
#else
	dev_hard_header(skb, skb->dev, ETH_P_IP,
			neigh_info.hw_addr.sa_data, 0, skb->len);	
#endif
	return 0;
}

static int dsr_dev_inetaddr_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct in_device *indev;

	if (!ifa)
		return NOTIFY_DONE;

	indev = ifa->ifa_dev;

	if (!indev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		LOG_DBG("inetdev UP\n");

		if (indev->dev == dsr_dev) {
			struct dsr_node *dnode;
			struct in_addr addr, bc;

			dnode = netdev_priv(indev->dev);

			dsr_node_lock(dnode);
			dnode->ifaddr.s_addr = ifa->ifa_address;
			dnode->bcaddr.s_addr = ifa->ifa_broadcast;

			dnode->slave_indev = in_dev_get(dnode->slave_dev);

                        /* Disable rp_filter and enable forwarding */
                        if (dnode->slave_indev) {
                                rp_filter = IN_DEV_RPFILTER(dnode->slave_indev);
                                forwarding = IN_DEV_FORWARD(dnode->slave_indev);

				DSRUU_IN_DEV_SET_RPFILTER(dnode->slave_indev, 0);
                                DSRUU_IN_DEV_SET_FORWARD(dnode->slave_indev, 1);
                        }			
			dsr_node_unlock(dnode);
			
			addr.s_addr = ifa->ifa_address;
			bc.s_addr = ifa->ifa_broadcast;
			
			LOG_DBG("New ip=%s broadcast=%s\n",
			      print_ip(addr), print_ip(bc));
		}
		break;
	default:
		break;
	};
	return NOTIFY_DONE;
}

static int dsr_dev_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;
	struct dsr_node *dnode = netdev_priv(dsr_dev);
	int slave_change = 0;

	if (!dev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		LOG_DBG("Netdev register %s\n", dev->name);
		if (dnode->slave_dev == NULL && 
		    strcmp(dev->name, dnode->slave_ifname) == 0) {
			
			LOG_DBG("Slave dev %s up\n", dev->name);
			
			dsr_node_lock(dnode);
			dnode->slave_dev = dev;
			dev_hold(dev);
			dsr_node_unlock(dnode);

			/* Reduce the MTU to allow DSR options of 100
			 * bytes. If larger, drop or implement
			 * fragmentation... ;-) Alternatively find a
			 * way to dynamically reduce the data size of
			 * packets depending on the size of the DSR
			 * header. */
			dsr_dev->mtu = dev->mtu - DSR_OPTS_MAX_SIZE;
			
			LOG_DBG("Registering packet type\n");
			dsr_packet_type.func = dsr_dev_llrecv;
			dsr_packet_type.dev = dev;
			dev_add_pack(&dsr_packet_type);
			
			slave_change = 1;
		}

		if (slave_change)
			LOG_DBG("New DSR slave interface %s\n", dev->name);
		break;
	case NETDEV_CHANGE:
		LOG_DBG("Netdev change\n");
		break;
	case NETDEV_UP:
		LOG_DBG("Netdev up %s\n", dev->name);
		if (ConfVal(PromiscOperation) &&
		    dev == dsr_dev && dnode->slave_dev)
			dev_set_promiscuity(dnode->slave_dev, +1);
		break;
	case NETDEV_UNREGISTER:
		LOG_DBG("Netdev unregister %s\n", dev->name);
		
		dsr_node_lock(dnode);
		if (dev == dnode->slave_dev) {
			dev_remove_pack(&dsr_packet_type);
			dsr_packet_type.func = NULL;
			slave_change = 1;
			dev_put(dev);
			dnode->slave_dev = NULL;
		}
		dsr_node_unlock(dnode);

		if (slave_change)
			LOG_DBG("DSR slave interface %s unregisterd\n",
			      dev->name);
		break;
	case NETDEV_DOWN:
		LOG_DBG("Netdev down %s\n", dev->name);
		if (dev == dsr_dev) {
			if (dnode->slave_dev && ConfVal(PromiscOperation))
				dev_set_promiscuity(dnode->slave_dev, -1);

			dsr_node_lock(dnode);
                        if (dnode->slave_indev) {
				DSRUU_IN_DEV_SET_RPFILTER(dnode->slave_indev, rp_filter);
                                DSRUU_IN_DEV_SET_FORWARD(dnode->slave_indev, forwarding);
                                in_dev_put(dnode->slave_indev);
                                dnode->slave_indev = NULL;
                        }
                        dsr_node_unlock(dnode);
		} else if (dev == dnode->slave_dev && dnode->slave_indev) {
			dsr_node_lock(dnode);
			DSRUU_IN_DEV_SET_RPFILTER(dnode->slave_indev, rp_filter);
			DSRUU_IN_DEV_SET_FORWARD(dnode->slave_indev, forwarding);
			in_dev_put(dnode->slave_indev);
			dnode->slave_indev = NULL;
                        dsr_node_unlock(dnode);
		} 
		break;
	default:
		break;
	};

	return NOTIFY_DONE;
}

static int dsr_dev_start_xmit(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev);

static int dsr_dev_set_address(struct net_device *dev, void *p)
{
	struct sockaddr *sa = p;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, sa->sa_data, ETH_ALEN);
	return 0;
}

static int dsr_dev_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int dsr_dev_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static void dsr_dev_uninit(struct net_device *dev)
{
	struct dsr_node *dnode = netdev_priv(dev);

	dsr_node_lock(dnode);
	
	if (dnode->slave_dev)
		dev_put(dnode->slave_dev);

	if (dnode->slave_indev)
		in_dev_put(dnode->slave_indev);

	dsr_node_unlock(dnode);

	if (dsr_packet_type.func) {
		LOG_DBG("Removing pack\n");
		dev_remove_pack(&dsr_packet_type);
		dsr_packet_type.func = NULL;
	}

	dev_put(dev);
	dsr_node = NULL;
}

/* fake multicast ability */
/*
static void dsr_dev_set_multicast_list(struct net_device *dev)
{
}
*/
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
static const struct net_device_ops dsr_netdev_ops = {
	.ndo_get_stats = dsr_dev_get_stats,
	.ndo_uninit = dsr_dev_uninit,
	.ndo_open = dsr_dev_open,
	.ndo_stop = dsr_dev_stop,
	.ndo_start_xmit = dsr_dev_start_xmit,
	.ndo_set_mac_address = dsr_dev_set_address,
	/* .ndo_set_multicast_list = dsr_dev_set_multicast_list, */
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int dsr_dev_setup(struct net_device *dev)
#else
static void dsr_dev_setup(struct net_device *dev)
#endif
{
	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);

	/* Set device operations */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
	dev->netdev_ops = &dsr_netdev_ops;
#else
	dev->get_stats = dsr_dev_get_stats;
	dev->uninit = dsr_dev_uninit;
	dev->open = dsr_dev_open;
	dev->stop = dsr_dev_stop;
        dev->set_multicast_list = dsr_dev_set_multicast_list;
	dev->hard_start_xmit = dsr_dev_start_xmit;
	dev->set_mac_address = dsr_dev_set_address;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(dev);
#endif
	//dev->destructor = dsr_dev_free;

	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	get_random_bytes(dev->dev_addr, 6);
	//random_ether_addr(dev->dev_addr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	return 0;
#endif
}

#ifdef ENABLE_DEBUG
static char *pkt_type_str[] = {
	"PACKET_HOST",
	"PACKET_BROADCAST",
	"PACKET_MULTICAST",
	"PACKET_OTHERHOST",
	"PACKET_OUTGOING",
	"PACKET_LOOPBACK",
	"PACKET_FASTROUTE",
        NULL
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
static int dsr_dev_llrecv(struct sk_buff *skb,
			  struct net_device *indev, 
			  struct packet_type *pt)
#else
static int dsr_dev_llrecv(struct sk_buff *skb,
			  struct net_device *indev, 
			  struct packet_type *pt, 
			  struct net_device *orig_dev)
#endif
{
	LOG_DBG("Packet recvd from ll skb->pkt_type is %s\n", 
	      (skb->pkt_type < 7 && skb->pkt_type >= 0) ? 
	      pkt_type_str[skb->pkt_type] : "unknown");

/* 	if (do_mackill(skb->mac.raw + ETH_ALEN)) { */
/* 		kfree_skb(skb); */
/* 		return 0; */
/* 	} */

	switch (skb->pkt_type) {
	case PACKET_HOST:
	case PACKET_BROADCAST:
	case PACKET_MULTICAST:
		dsr_ip_recv(skb);
		break;
	case PACKET_OTHERHOST:
	case PACKET_OUTGOING:
	case PACKET_LOOPBACK:
	case PACKET_FASTROUTE:
	default:
		dev_kfree_skb_any(skb);

	}
	return 0;
}

int dsr_dev_deliver(struct dsr_pkt *dp)
{
	struct sk_buff *skb = NULL;
	struct ethhdr *ethh;
	int len;

	if (!dp)
		return -1;

	/* Super ugly hack to fix record route options */
	if (SKB_NETWORK_HDR_IPH(dp->skb)->ihl > 5) {
		struct ip_options *opt = &(IPCB(dp->skb)->opt);
		unsigned char *ptr = SKB_NETWORK_HDR_RAW(dp->skb);
	
		if (opt->rr) {	
			struct ipopt {
				u_int8_t code;
				u_int8_t len;
				u_int8_t off;
			} *rr = (struct ipopt *)&ptr[opt->rr];
			
			if (rr->off < 32) {
				
				/* Remove the last recorded address since it will
				 * recorded again when passed up the IP stack for the
				 * second time on the virtual interface. */
				rr->off -= 4;
				rr->len -= 4;
				opt->optlen -= 4;
			}
			/* ip_send_check(dp->skb->nh.iph); */
		}
	}

	if (dp->dh.raw)
		len = dsr_opt_remove(dp);

	skb = dsr_skb_create(dp, dsr_dev);

	if (!skb) {
		LOG_DBG("Could not allocate skb\n");
		dsr_pkt_free(dp);
		return -1;
	}
	
	/* Need to make hardware header visible again since we are
	 * going down a layer... But this should already be set in
	 * dsr_skb_create() */
	/* skb->mac.raw = skb->data - dsr_dev->hard_header_len; */

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	
	ethh = (struct ethhdr *)SKB_MAC_HDR_RAW(skb);

	memcpy(ethh->h_dest, dsr_dev->dev_addr, ETH_ALEN);
	memset(ethh->h_source, 0, ETH_ALEN);
	ethh->h_proto = htons(ETH_P_IP);

	dsr_node_lock(dsr_node);
	dsr_node->stats.rx_packets++;
	dsr_node->stats.rx_bytes += skb->len;
	dsr_node_unlock(dsr_node);

	netif_rx(skb);

	dsr_pkt_free(dp);

	return 0;
}

int dsr_dev_xmit(struct dsr_pkt *dp)
{
	struct sk_buff *skb;
	struct net_device *slave_dev;
	struct in_addr dst;
	int res = -1;
	int len = 0;	

	if (!dp)
		return -1;

	if (dp->flags & PKT_REQUEST_ACK)
		maint_buf_add(dp);

	dsr_node_lock(dsr_node);

	if (dsr_node->slave_dev)
		slave_dev = dsr_node->slave_dev;
	else {
		dsr_node_unlock(dsr_node);
		goto out_err;
	}
	dsr_node_unlock(dsr_node);

	skb = dsr_skb_create(dp, slave_dev);

	if (!skb) {
		LOG_DBG("Could not create skb!\n");
		goto out_err;
	}

	/* Create hardware header */
	if (dsr_hw_header_create(dp, skb) < 0) {
		LOG_DBG("Could not create hardware header\n");
		dev_kfree_skb_any(skb);
		goto out_err;
	}
	
	len = skb->len;
	dst.s_addr = SKB_NETWORK_HDR_IPH(skb)->daddr;
	
	LOG_DBG("Sending %d bytes data_len=%d %s : %s\n",
	      len, skb->data_len,
	      print_eth(SKB_MAC_HDR_RAW(skb)),
	      print_ip(dst));
		
	/* TODO: Should consider using ip_finish_output instead */
	res = dev_queue_xmit(skb);

	if (res < 0)
		goto out_err;

	dsr_node_lock(dsr_node);
	dsr_node->stats.tx_packets++;
	dsr_node->stats.tx_bytes += len;
	dsr_node_unlock(dsr_node);

out_err:
	dsr_pkt_free(dp);

	return res;
}

/* Main receive function for packets originated in user space */
static int dsr_dev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsr_node *dnode = netdev_priv(dev);
	struct ethhdr *ethh;
	struct dsr_pkt *dp;
#ifdef ENABLE_DEBUG
	atomic_inc(&num_pkts);
#endif
	if (dnode->slave_dev == NULL) {
		dev_kfree_skb_any(skb);
		LOG_DBG("Packet dropped\n");
		return 0;
	}

	ethh = (struct ethhdr *)skb->data;

	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:

		LOG_DBG("dst=%s len=%d\n",
		      print_ip(*((struct in_addr *)&SKB_NETWORK_HDR_IPH(skb)->daddr)),
		      skb->len);

		dp = dsr_pkt_alloc(skb);
		
		if (!dp) {
			dev_kfree_skb_any(skb);
			return 0;
		}			

		dsr_start_xmit(dp);
		break;
	default:
		LOG_DBG("Unknown packet type, dropping...\n");
		dev_kfree_skb_any(skb);
	}
	return 0;
}

static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev)
{
	struct dsr_node *dnode = netdev_priv(dev);
	return &dnode->stats;
}

static struct notifier_block netdev_notifier = {
      notifier_call:dsr_dev_netdev_event,
};

/* Notifier for inetaddr addition/deletion events.  */
static struct notifier_block inetaddr_notifier = {
	.notifier_call = dsr_dev_inetaddr_event,
};


int dsr_dev_init(char *ifname)
{
	int res = 0;
	struct dsr_node *dnode;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	dsr_dev = alloc_etherdev(sizeof(struct dsr_node));

	if (!dsr_dev)
		return -ENOMEM;
	
	dsr_dev->init = dsr_dev_setup;

	dev_alloc_name(dsr_dev, "dsr%d");
#else
	dsr_dev = alloc_netdev(sizeof(struct dsr_node), "dsr%d", dsr_dev_setup);

	if (!dsr_dev)
		return -ENOMEM;
#endif
	dnode = dsr_node = netdev_priv(dsr_dev);

	dsr_node_init(dnode, ifname);

	if (!ifname) {
		struct net_device *dev;
		int is_wireless = 0;
		
		read_lock(&dev_base_lock);
		
		for_each_netdev(&init_net, dev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
			if (dev->wireless_handlers)
				is_wireless = 1;
#else
			if (dev->get_wireless_stats)
				is_wireless = 1;
#endif
			if (is_wireless) {
				memcpy(dnode->slave_ifname, 
                                       dev->name, IFNAMSIZ);
				
				read_unlock(&dev_base_lock);
				goto dev_found;
			}
		}
		read_unlock(&dev_base_lock);
		
		LOG_DBG("No preferred slave device found\n");
		res = -1;
		goto cleanup_netdev;
	}
dev_found:
	LOG_DBG("Slave device is %s\n", dnode->slave_ifname);	
		
	res = register_netdev(dsr_dev);

	dsr_packet_type.func = NULL;

	if (res < 0)
		goto cleanup_netdev;

	res = register_netdevice_notifier(&netdev_notifier);

	if (res < 0)
		goto cleanup_netdev_register;

	res = register_inetaddr_notifier(&inetaddr_notifier);

	if (res < 0)
		goto cleanup_netdevice_notifier;

	/* We increment usage count since we hold a reference */
	dev_hold(dsr_dev);

	return 0;
 cleanup_netdevice_notifier:
	unregister_netdevice_notifier(&netdev_notifier);
 cleanup_netdev_register:
	unregister_netdev(dsr_dev);
 cleanup_netdev:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	free_netdev(dsr_dev);
#else
	kfree(dsr_dev);
#endif
	return res;
}

void __exit dsr_dev_cleanup(void)
{

	unregister_netdevice_notifier(&netdev_notifier);
	unregister_inetaddr_notifier(&inetaddr_notifier);
	unregister_netdev(dsr_dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	free_netdev(dsr_dev);
#else
	kfree(dsr_dev);
#endif
}
