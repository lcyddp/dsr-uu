#ifndef _DSR_NS_AGENT
#define _DSR_NS_AGENT

#ifndef NS2
#error "To compile the ns-2 version of DSR-UU, NS2 must be defined!"
#endif /* NS2 */

class DSRUU;

#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>
#include <packet.h>
#include <dsr-priqueue.h>
#include <mac.h>
#include <mac-802_11.h>
#include <mobilenode.h>
#include <linux/if_ether.h>
#include "tbl.h"
#include "endian.h"
#include "dsr.h"
#include "timer.h"

#define NSCLASS DSRUU::

#define NO_DECLS
#include "dsr-opt.h"
#include "send-buf.h"
#include "dsr-rreq.h"
#include "dsr-pkt.h"
#include "dsr-rrep.h"
#include "dsr-rerr.h"
#include "dsr-ack.h"
#include "dsr-srt.h"
#include "neigh.h"
#include "link-cache.h"
#include "debug.h"
#undef NO_DECLS

typedef dsr_opt_hdr hdr_dsr;
#define HDR_DSRUU(p) ((hdr_dsr *)hdr_dsr::access(p))

#define TimeNow Scheduler::instance().clock()

#define init_timer(timer)
#define timer_pending(timer) ((timer)->status() == TIMER_PENDING)
#define del_timer_sync(timer) del_timer(timer)
#define MALLOC(s, p)        malloc(s)
#define FREE(p)             free(p)
#define XMIT(pkt) xmit(pkt)
#define DELIVER(pkt) deliver(pkt)
#define __init
#define __exit
#define PARAM(name) DSRUU::get_param(name)
#define ntohl(x) x
#define htonl(x) x
#define htons(x) x
#define ntohs(x) x

#define IPDEFTTL 64


struct hdr_test {
	int data;
	/* Packet header access functions */
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_test* access(const Packet* p) {
		return (hdr_test*) p->access(offset_);
	}
	int size() { return data; }

};

class DSRUU : public Agent {
 public:
	friend class DSRUUTimer;
	
	DSRUU();
	~DSRUU();

	DSRUUTimer ack_timer;

	int command(int argc, const char*const* argv);
	void recv(Packet*, Handler* callback = 0);
	Packet *ns_packet_create(struct dsr_pkt *dp);
	void xmit(struct dsr_pkt *dp);
	void deliver(struct dsr_pkt *dp);

/* 	void tap(const Packet *p); */
	// tap out all data packets received at this host and promiscously snoop
	// them for interesting tidbits
	struct hdr_ip *	dsr_build_ip(struct dsr_pkt *dp, struct in_addr src, struct in_addr dst, int ip_len, int tot_len, int protocol, int ttl);
	void add_timer (DSRUUTimer *t) { t->resched(t->expires); }
	void mod_timer (DSRUUTimer *t, Time expires_) 
		{ t->expires = expires_; t->resched(t->expires); }
	void del_timer (DSRUUTimer *t) { t->cancel(); }
	static const int get_param(int index) { return params[index]; }
	static const int set_param(int index, int val) 
		{ params[index] = val; return val; }
#define NO_GLOBALS
#undef NO_DECLS

#undef _DSR_OPT_H
#include "dsr-opt.h"

#undef _DSR_IO_H
#include "dsr-io.h"

#undef _DSR_RREQ_H
#include "dsr-rreq.h"

#undef _DSR_RREP_H
#include "dsr-rrep.h"

#undef _DSR_RERR_H
#include "dsr-rerr.h"

#undef _DSR_ACK_H
#include "dsr-ack.h"

#undef _DSR_SRT_H
#include "dsr-srt.h"

#undef _SEND_BUF_H
#include "send-buf.h"

#undef _NEIGH_H
#include "neigh.h"

#undef _MAINT_BUF_H
#include "maint-buf.h"

#undef _LINK_CACHE_H
#include "link-cache.h"

#undef _DEBUG_H
#include "debug.h"

#undef NO_GLOBALS
	
	struct in_addr my_addr() { return myaddr; }
	int arpset(struct in_addr addr, unsigned int mac_addr);
	inline void ethtoint(char * eth, int *num)
		{
			memcpy((char*)num, eth, ETH_ALEN);
			return;
		}
	inline void inttoeth(int *num, char* eth)
		{
			memcpy(eth,(char*)num, ETH_ALEN);
			return;
		}
 private:
	static int params[PARAMS_MAX];
	struct in_addr myaddr;
	unsigned long macaddr;
	Trace *trace_;
	Mac *mac_;
	LL *ll_;
	CMUPriQueue *ifq_;
	MobileNode *node_;

	struct tbl rreq_tbl;
	struct tbl grat_rrep_tbl;
	struct tbl send_buf;
	struct tbl neigh_tbl;
	struct tbl maint_buf;
	
	unsigned int rreq_seqno;
	
	DSRUUTimer grat_rrep_tbl_timer;
	DSRUUTimer send_buf_timer;
	DSRUUTimer neigh_tbl_timer;
	DSRUUTimer lc_timer;

	/* The link cache */
	struct lc_graph LC;
};

#endif /* _DSR_NS_AGENT_H */
