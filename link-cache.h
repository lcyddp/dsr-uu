#ifndef _LINK_CACHE_H
#define _LINK_CACHE_H

#include "tbl.h"
#include "timer.h"

#define LC_TIMER

#ifndef NO_GLOBALS

struct lc_graph {
	struct tbl nodes;
	struct tbl links;
	struct lc_node *src;
#ifdef __KERNEL__
	struct timer_list timer;
	rwlock_t lock;
#endif
};

#define dsr_rtc_find(s,d) lc_srt_find(s,d)
#define dsr_rtc_add(srt,t,f) lc_srt_add(srt,t,f)

#endif /* NO_GLOBALS */

#ifndef NO_DECLS

int lc_link_del(struct in_addr src, struct in_addr dst);
int lc_link_add(struct in_addr src, struct in_addr dst, 
		unsigned long timeout, int status, int cost);
void lc_garbage_collect_set(void);
void lc_garbage_collect(unsigned long data);
struct dsr_srt *lc_srt_find(struct in_addr src, struct in_addr dst);
int lc_srt_add(struct dsr_srt *srt, unsigned long timeout, unsigned short flags);
void lc_flush(void);
void __dijkstra(struct in_addr src);
int lc_init(void);
void lc_cleanup(void);

#endif /* NO_DECLS */

#endif /* _LINK_CACHE */
