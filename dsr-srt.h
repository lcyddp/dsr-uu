#ifndef _DSR_SRT_H
#define _DSR_SRT_H

#include "dsr.h"
#include "debug.h"

#ifdef NS2
#include "endian.h"
#endif

#ifndef NO_GLOBALS

/* Source route options header */
struct dsr_srt_opt {
	u_int8_t type;
	u_int8_t length;
#if defined(__LITTLE_ENDIAN_BITFIELD)	
	union {
		struct {
			u_int16_t sleft:6;
			u_int16_t salv:4;
			u_int16_t res:4;
			u_int16_t l:1;
			u_int16_t f:1;
		};
		u_int16_t fields; /* This is a trick to easily convert the
				   * byte/bit order on little endian systems. */
	};
#elif defined (__BIG_ENDIAN_BITFIELD)
	u_int16_t f:1;
	u_int16_t l:1;
	u_int16_t res:4;
	u_int16_t salv:4;
	u_int16_t sleft:6;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	u_int32_t addrs[0];
};

/* Flags: */
#define SRT_FIRST_HOP_EXT 0x1
#define SRT_LAST_HOP_EXT  0x2


#define DSR_SRT_HDR_LEN sizeof(struct dsr_srt_opt)
#define DSR_SRT_OPT_LEN(srt) (DSR_SRT_HDR_LEN + srt->laddrs)

/* Flags */
#define SRT_BIDIR 0x1

/* Internal representation of a source route */
struct dsr_srt {
	struct in_addr src;
	struct in_addr dst;
	unsigned short flags;
	unsigned short index;
	unsigned int laddrs;	/* length in bytes if addrs */
	struct in_addr addrs[0];	/* Intermediate nodes */
};

static inline char *print_srt(struct dsr_srt *srt)
{
#define BUFLEN 256
	static char buf[BUFLEN];
	unsigned int i, len;

	if (!srt)
		return NULL;

	len = sprintf(buf, "%s<->", print_ip(srt->src));

	for (i = 0; i < (srt->laddrs / sizeof(u_int32_t)) &&
	     (len + 16) < BUFLEN; i++)
		len += sprintf(buf + len, "%s<->", print_ip(srt->addrs[i]));

	if ((len + 16) < BUFLEN)
		len = sprintf(buf + len, "%s", print_ip(srt->dst));
	return buf;
}
struct in_addr dsr_srt_next_hop(struct dsr_srt *srt, int sleft);
struct in_addr dsr_srt_prev_hop(struct dsr_srt *srt, int sleft);
struct dsr_srt_opt *dsr_srt_opt_add(char *buf, int len, int flags, int salvage, struct dsr_srt *srt);
struct dsr_srt *dsr_srt_new(struct in_addr src, struct in_addr dst,
			    unsigned int length, char *addrs);
struct dsr_srt *dsr_srt_new_rev(struct dsr_srt *srt);
void dsr_srt_del(struct dsr_srt *srt);
struct dsr_srt *dsr_srt_concatenate(struct dsr_srt *srt1, struct dsr_srt *srt2);int dsr_srt_check_duplicate(struct dsr_srt *srt);

#endif				/* NO_GLOBALS */

#ifndef NO_DECLS

int dsr_srt_add(struct dsr_pkt *dp);
int dsr_srt_opt_recv(struct dsr_pkt *dp, struct dsr_srt_opt *srt_opt);

#endif				/* NO_DECLS */

#endif				/* _DSR_SRT_H */
