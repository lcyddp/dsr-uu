#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <linux/random.h>

#include "debug.h"
#include "dsr.h"
#include "kdsr.h"
#include "dsr-opt.h"
#include "dsr-rreq.h"
#include "dsr-rtc.h"
#include "dsr-srt.h"
#include "send-buf.h"

/* Our dsr device */
struct net_device *dsr_dev;
struct dsr_node *dsr_node;

static int dsr_dev_inetaddr_event(struct notifier_block *this, 
				  unsigned long event,
				  void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct in_device *indev;
	
	struct dsr_node *dnode;

	if (!ifa)
		return NOTIFY_DONE;
	
	indev = ifa->ifa_dev;

	switch (event) {
        case NETDEV_UP:
		DEBUG("Netdev UP\n");
		if (indev && indev->dev == dsr_dev) {
			
			dnode = indev->dev->priv;

			dsr_node_lock(dnode);
			dnode->ifaddr.s_addr = ifa->ifa_address;
			dnode->bcaddr.s_addr = ifa->ifa_broadcast;
			dsr_node_unlock(dnode);
			
			DEBUG("New ip=%s broadcast=%s\n", 
			      print_ip(ifa->ifa_address), 
			      print_ip(ifa->ifa_broadcast));
		}
		break;
        case NETDEV_DOWN:
		DEBUG("notifier down\n");
                break;
        default:
                break;
        };
	return NOTIFY_DONE;
}
static int dsr_dev_netdev_event(struct notifier_block *this,
                              unsigned long event, void *ptr)
{
        struct net_device *dev = (struct net_device *) ptr;
	struct dsr_node *dnode = dsr_dev->priv;

	if (!dev)
		return NOTIFY_DONE;

	switch (event) {
        case NETDEV_REGISTER:
		DEBUG("Netdev register %s\n", dev->name);
		if (dnode->slave_dev == NULL && dev->get_wireless_stats) {
			dsr_node_lock(dnode);
			dnode->slave_dev = dev;
			dsr_node_unlock(dnode);
			dev_hold(dnode->slave_dev);
			DEBUG("new dsr slave interface %s\n", dev->name);
		} 
		break;
	case NETDEV_CHANGE:
		DEBUG("Netdev change\n");
		break;
        case NETDEV_UP:
		DEBUG("Netdev up %s\n", dev->name);
		break;
        case NETDEV_UNREGISTER:
		DEBUG("Netdev unregister %s\n", dev->name); 
		if (dev == dnode->slave_dev) {
                        DEBUG("dsr slave interface %s went away\n", dev->name);
			dsr_node_lock(dnode);
			dev_put(dnode->slave_dev);
			dnode->slave_dev = NULL;
			dsr_node_unlock(dnode);
                }
		break;
        case NETDEV_DOWN:
		DEBUG("Netdev down %s\n", dev->name);
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

/* fake multicast ability */
static void set_multicast_list(struct net_device *dev)
{
}

#ifdef CONFIG_NET_FASTROUTE
static int dsr_dev_accept_fastpath(struct net_device *dev, struct dst_entry *dst)
{
	return -1;
}
#endif
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
	struct dsr_node *dnode = dev->priv;
	
	DEBUG("Calling dev_put on interfaces dnode->slave_dev=%u dsr_dev=%u\n",
	      (unsigned int)dnode->slave_dev, (unsigned int)dsr_dev);

	dsr_node_lock(dnode);
	if (dnode->slave_dev)
		dev_put(dnode->slave_dev);
	dsr_node_unlock(dnode);
	dev_put(dsr_dev);
	dsr_node = NULL;
}

static void __init dsr_dev_setup(struct net_device *dev)
{
	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	/* Initialize the device structure. */
	dev->get_stats = dsr_dev_get_stats;
	dev->uninit = dsr_dev_uninit;
	dev->open = dsr_dev_open;
	dev->stop = dsr_dev_stop;
	dev->hard_start_xmit = dsr_dev_start_xmit;
	dev->set_multicast_list = set_multicast_list;
	dev->set_mac_address = dsr_dev_set_address;
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = dsr_dev_accept_fastpath;
#endif

	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	SET_MODULE_OWNER(dev);
	//random_ether_addr(dev->dev_addr);
	get_random_bytes(dev->dev_addr, 6);
}


int dsr_dev_deliver(struct dsr_pkt *dp)
{	
	struct sk_buff *skb;
	struct ethhdr *ethh;
	
	skb = dsr_skb_create(dp, dsr_dev);

	if (!skb) {
		DEBUG("Could not allocate skb\n");
		return -1;
	}
	
	/* Need to make hardware header visible again since we are going down a
	 * layer */	

	skb->mac.raw = skb->data - dsr_dev->hard_header_len;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	ethh = (struct ethhdr *)skb->mac.raw;
	
	memcpy(ethh->h_dest, dsr_dev->dev_addr, ETH_ALEN);
	memset(ethh->h_source, 0, ETH_ALEN);
	ethh->h_proto = htons(ETH_P_IP);

	dsr_node_lock(dsr_node);
	dsr_node->stats.rx_packets++;
	dsr_node->stats.rx_bytes += skb->len;
	dsr_node_unlock(dsr_node);

	netif_rx(skb);
	
	return 0;
}

int dsr_dev_xmit(struct dsr_pkt *dp)
{
	struct sk_buff *skb;

	dsr_node_lock(dsr_node);
	skb = dsr_skb_create(dp, dsr_node->slave_dev);
	dsr_node_unlock(dsr_node);

	if (!skb) {
		DEBUG("Could not create skb!\n");
		return -1;
	}
       
	/* Create hardware header */
	if (dsr_hw_header_create(dp, skb) < 0) {
		DEBUG("Could not create hardware header\n");
		kfree_skb(skb);
		return -1;
	}
		
	dev_queue_xmit(skb);
	
	DEBUG("Sent %d bytes skb->data_len=%s headroom=%d tailroom=%d %u:%u %d\n", skb->len, skb->data_len, skb_headroom(skb), skb_tailroom(skb), skb->head, skb->tail, skb->tail - skb->head);

	dsr_node_lock(dsr_node);
	dsr_node->stats.tx_packets++;
	dsr_node->stats.tx_bytes+=skb->len;
	dsr_node_unlock(dsr_node);
	
	return 0;
}

/* Main receive function for packets originated in user space */
static int dsr_dev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	/* struct dsr_node *dnode = (struct dsr_node *)dev->priv; */
	struct ethhdr *ethh;
	struct dsr_pkt dp;
	int res = 0;
#ifdef DEBUG
	atomic_inc(&num_pkts);
#endif 		
	DEBUG("headroom=%d skb->data=%lu skb->nh.iph=%lu\n", 
	      skb_headroom(skb), (unsigned long)skb->data, 
	      (unsigned long)skb->nh.iph);
	
	memset(&dp, 0, sizeof(dp));
	
	dp.skb = skb;

	ethh = (struct ethhdr *)skb->data;
	
	dp.nh.iph = skb->nh.iph;
	dp.data = skb->data + dev->hard_header_len + (dp.nh.iph->ihl << 2);
	dp.data_len = skb->len - dev->hard_header_len - (dp.nh.iph->ihl << 2);
	
	dp.src.s_addr = skb->nh.iph->saddr;
	dp.dst.s_addr = skb->nh.iph->daddr;
	
	switch (ntohs(ethh->h_proto)) {
	case ETH_P_IP:
	    
		dp.srt = dsr_rtc_find(dp.src, dp.dst);
		
		if (dp.srt) {

			if (dsr_srt_add(&dp) < 0) {
				DEBUG("Could not add source route\n");
				break;
			}
			/* Send packet */
			dsr_dev_xmit(&dp);

			kfree(dp.srt);

		} else {			
			res = send_buf_enqueue_packet(&dp, skb, dsr_dev_xmit);
			
			if (res < 0) {
				DEBUG("Queueing failed!\n");
				break;
			}
			res = dsr_rreq_route_discovery(dp.dst);
			
			if (res < 0)
				DEBUG("RREQ Transmission failed...");

			return 0;
		}
		break;
	default:
		DEBUG("Unkown packet type\n");
	}
	kfree_skb(skb);	
	return 0;
}

static struct net_device_stats *dsr_dev_get_stats(struct net_device *dev)
{
	return &(((struct dsr_node*)dev->priv)->stats);
}

static struct notifier_block netdev_notifier = {
	notifier_call: dsr_dev_netdev_event,
};
/* Notifier for inetaddr addition/deletion events.  */
static struct notifier_block inetaddr_notifier = {
	.notifier_call = dsr_dev_inetaddr_event,
};

int __init dsr_dev_init(char *ifname)
{ 
	int res = 0;	
	struct dsr_node *dnode;

	dsr_dev = alloc_netdev(sizeof(struct dsr_node),
			       "dsr%d", dsr_dev_setup);

	if (!dsr_dev)
		return -ENOMEM;

	dnode = dsr_node = (struct dsr_node *)dsr_dev->priv;

	dsr_node_init(dnode);

	if (ifname) {
		dnode->slave_dev = dev_get_by_name(ifname);
		
		if (!dnode->slave_dev) {
			DEBUG("device %s not found\n", ifname);
			res = -1;
			goto cleanup_netdev;
		} 
		
		if (dnode->slave_dev == dsr_dev) {
			DEBUG("invalid slave device %s\n", ifname);
			res = -1;
			dev_put(dnode->slave_dev);
			goto cleanup_netdev;
		}	
	} else {
		read_lock(&dev_base_lock);
		for (dnode->slave_dev = dev_base; 
		     dnode->slave_dev != NULL; 
		     dnode->slave_dev = dnode->slave_dev->next) {
			
			if (dnode->slave_dev->get_wireless_stats)
				break;
		}
		read_unlock(&dev_base_lock);
		
		if (dnode->slave_dev) {
			dev_hold(dnode->slave_dev);
			DEBUG("wireless interface is %s\n", 
			      dnode->slave_dev->name);
		} else {
			DEBUG("No proper slave device found\n");
			res = -1;
			goto cleanup_netdev;
		}
	}
	
	DEBUG("Setting %s as slave interface\n", dnode->slave_dev->name);

	res = register_netdev(dsr_dev);

	if (res < 0)
		goto cleanup_netdev;

	res = register_netdevice_notifier(&netdev_notifier);
	
	if (res < 0)
		goto cleanup_netdev_register;
	
	res = register_inetaddr_notifier(&inetaddr_notifier);
		
	if (res < 0)
		goto cleanup_netdevice_notifier;
	/* We must increment usage count since we hold a reference */
	dev_hold(dsr_dev);
	return res;
 cleanup_netdevice_notifier:
	unregister_netdevice_notifier(&netdev_notifier);
 cleanup_netdev_register:
	unregister_netdev(dsr_dev);
 cleanup_netdev:
	free_netdev(dsr_dev);
	return res;
} 

void __exit dsr_dev_cleanup(void)
{
        unregister_netdevice_notifier(&netdev_notifier);
	unregister_inetaddr_notifier(&inetaddr_notifier);
	unregister_netdev(dsr_dev);
	free_netdev(dsr_dev);
}
