#ifdef __KERNEL__
#include <linux/slab.h>
#include <net/ip.h>
#endif

#ifdef NS2
#include "ns-agent.h"
#endif

#include "dsr.h"
#include "dsr-srt.h"
#include "dsr-opt.h"
#include "dsr-ack.h"
#include "link-cache.h"
#include "debug.h"


struct in_addr dsr_srt_next_hop(struct dsr_srt *srt, int sleft)
{
	int n = srt->laddrs / sizeof(struct in_addr);
	struct in_addr nxt_hop;
	
	if (sleft == 0)
		nxt_hop = srt->dst;
	else 
		nxt_hop = srt->addrs[n-sleft];
	
	return nxt_hop;
}

struct in_addr dsr_srt_prev_hop(struct dsr_srt *srt, int sleft)
{
	struct in_addr prev_hop;
	int n = srt->laddrs / sizeof(u_int32_t);
	
	if (sleft + 1 == n)
		prev_hop = srt->src;
	else 
		prev_hop = srt->addrs[n-sleft+1];

	return prev_hop;
}

static int dsr_srt_find_addr(struct dsr_srt *srt, struct in_addr addr, int index)
{
	int n = srt->laddrs / sizeof(struct in_addr);
	
	if (n == 0 || index >= n)
		return 0;
	
	for (; index < n; index++)
		if (srt->addrs[index].s_addr == addr.s_addr)
			return 1;

	if (addr.s_addr == srt->dst.s_addr)
		return 1;
	
	return 0;
}

struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst,
		       unsigned int length, char *addrs)
{
	struct dsr_srt *sr;

	sr = (struct dsr_srt *)MALLOC(sizeof(struct dsr_srt) + length, GFP_ATOMIC);
	
	if (!sr)
		return NULL;

	sr->src.s_addr = src.s_addr;
	sr->dst.s_addr = dst.s_addr;
	sr->laddrs = length;
/* 	sr->index = index; */
	
	if (length != 0 && addrs)
		memcpy(sr->addrs, addrs, length);
	
	return sr;
}

struct dsr_srt *dsr_srt_new_rev(struct dsr_srt *srt)
{
	struct dsr_srt *srt_rev;
	int i, n;

	if (!srt)
		return NULL;
	
	srt_rev = (struct dsr_srt *)MALLOC(sizeof(struct dsr_srt) + 
					   srt->laddrs, GFP_ATOMIC);
	
	srt_rev->src.s_addr = srt->dst.s_addr;
	srt_rev->dst.s_addr = srt->src.s_addr;
	srt_rev->laddrs = srt->laddrs;

	n = srt->laddrs / sizeof(struct in_addr);

	for (i = 0; i < n; i++)
		srt_rev->addrs[i].s_addr = srt->addrs[n-1-i].s_addr;

	return srt_rev;
}

void dsr_srt_del(struct dsr_srt *srt)
{
	FREE(srt);
}


struct dsr_srt_opt *dsr_srt_opt_add(char *buf, int len, struct dsr_srt *srt)
{
	struct dsr_srt_opt *srt_opt;
	
	if (len < DSR_SRT_OPT_LEN(srt))
		return NULL;

	srt_opt = (struct dsr_srt_opt *)buf;

	srt_opt->type = DSR_OPT_SRT;
	srt_opt->length = srt->laddrs + 2;
	srt_opt->f = 0;
	srt_opt->l = 0;
	srt_opt->res = 0;
	SET_SALVAGE(srt_opt, 0);
	srt_opt->sleft = (srt->laddrs / sizeof(struct in_addr));
	
	memcpy(srt_opt->addrs, srt->addrs, srt->laddrs);
	
	return srt_opt;
}


int NSCLASS dsr_srt_add(struct dsr_pkt *dp)
{
	char *buf;
	int n, len, ttl, tot_len, ip_len;
	int prot = 0;
		
	if (!dp || !dp->srt)
		return -1;

	n = dp->srt->laddrs / sizeof(struct in_addr);

	dp->nxt_hop = dsr_srt_next_hop(dp->srt, n);

	/* Calculate extra space needed */

	len = DSR_OPT_HDR_LEN + DSR_SRT_OPT_LEN(dp->srt);

/* 	DEBUG("dsr_opts_len=%d\n", len); */
	
	DEBUG("SR: %s\n", print_srt(dp->srt));

	buf = dsr_pkt_alloc_opts(dp, len);

	if (!buf) {
/* 		DEBUG("Could allocate memory\n"); */
		return -1;
	}

#ifdef NS2
	if (dp->p) {
		hdr_cmn *cmh = HDR_CMN(dp->p);
		prot = cmh->ptype();
	} else 
		prot = PT_NTYPE;
	
	ip_len = IP_HDR_LEN;
	tot_len = dp->payload_len + ip_len + len;
	ttl = dp->nh.iph->ttl();
#else
	prot = dp->nh.iph->protocol;
	ip_len = (dp->nh.iph->ihl << 2);
	tot_len =  ntohs(dp->nh.iph->tot_len) + len;
	ttl = dp->nh.iph->ttl;
#endif	
	dp->nh.iph = dsr_build_ip(dp, dp->src, dp->dst, ip_len, tot_len, 
				  IPPROTO_DSR, ttl);
	
	if (!dp->nh.iph) 
		return -1;

	dp->dh.opth = dsr_opt_hdr_add(buf, len, prot);

	if (!dp->dh.opth) {
/* 		DEBUG("Could not create DSR opts header!\n"); */
		return -1;
	}

	buf += DSR_OPT_HDR_LEN;
	len -= DSR_OPT_HDR_LEN;

	dp->srt_opt = dsr_srt_opt_add(buf, len, dp->srt);

	if (!dp->srt_opt) {
/* 		DEBUG("Could not create Source Route option header!\n"); */
		return -1;
	}

	buf += DSR_SRT_OPT_LEN(dp->srt);
	len -= DSR_SRT_OPT_LEN(dp->srt);
	
	return 0;
}

int NSCLASS dsr_srt_opt_recv(struct dsr_pkt *dp)
{
	int n;	
	
	if (!dp || !dp->srt_opt)
		return DSR_PKT_ERROR;
	
	/* We should add this source route info to the cache... */
	n = (dp->srt_opt->length - 2) / sizeof(struct in_addr);
	
	dp->srt = dsr_srt_new(dp->src, dp->dst, dp->srt_opt->length, 
			      (char *)dp->srt_opt->addrs);
	
	if (!dp->srt) {
		DEBUG("Create source route failed\n");
		return DSR_PKT_ERROR;
	}
	
	DEBUG("SR: %s\n", print_srt(dp->srt));

	dsr_rtc_add(dp->srt, ConfValToUsecs(RouteCacheTimeout), 0);
	
	if (dp->srt_opt->sleft == 0) {
		dp->prv_hop = dsr_srt_prev_hop(dp->srt, n-1);
			
/* 		DEBUG("prev_hop=%s\n", print_ip(dp->prv_hop)); */
		
		neigh_tbl_add(dp->prv_hop, dp->mac.ethh);
		
		if (dp->flags & PKT_PROMISC_RECV)
			return DSR_PKT_DROP;
		else
			return DSR_PKT_SRT_REMOVE;
	}
	
	if (dp->srt_opt->sleft > n) {
		// Send ICMP parameter error
		return DSR_PKT_SEND_ICMP;
	} else {
		struct in_addr myaddr = my_addr();
		struct in_addr next_hop_intended;
		
		if (dp->srt_opt->sleft > n) {
			DEBUG("segments left=%d larger than n=%d\n", 
			      dp->srt_opt->sleft, n);
			return DSR_PKT_ERROR;
		}
		next_hop_intended = dsr_srt_next_hop(dp->srt, dp->srt_opt->sleft);
		dp->srt_opt->sleft--;
		
		dp->nxt_hop = dsr_srt_next_hop(dp->srt, dp->srt_opt->sleft);
		dp->prv_hop = dsr_srt_prev_hop(dp->srt, dp->srt_opt->sleft);
			
		DEBUG("next_hop=%s prev_hop=%s next_hop_intended=%s\n", 
		      print_ip(dp->nxt_hop), 
		      print_ip(dp->prv_hop), 
		      print_ip(next_hop_intended));
		
		neigh_tbl_add(dp->prv_hop, dp->mac.ethh);

		/* Automatic route shortening - Check if this node is the
		 * intended next hop... */
		if (next_hop_intended.s_addr != myaddr.s_addr && 
		    dsr_srt_find_addr(dp->srt, myaddr, dp->srt_opt->sleft) && 
		    !grat_rrep_tbl_find(dp->src, dp->prv_hop)) {
			struct dsr_srt *srt_to_me;
			
			/* Send Grat RREP */
			DEBUG("Send Gratuitous RREP to %s\n", 
			      print_ip(dp->src));
			
			grat_rrep_tbl_add(dp->src, dp->prv_hop);
			
			srt_to_me = lc_srt_find(dp->src, myaddr);
			
			dsr_rrep_send(srt_to_me);

			FREE(srt_to_me);

			return DSR_PKT_DROP;
		}
	
		/* TODO: check for multicast address in next hop or dst */
		/* TODO: check MTU and compare to pkt size */
	
		return DSR_PKT_FORWARD;
	}
	return DSR_PKT_ERROR;
}
