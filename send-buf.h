#ifndef _SEND_BUF_H
#define _SEND_BUF_H

#include "dsr.h"

#ifndef NO_GLOBALS

#define SEND_BUF_DROP 1
#define SEND_BUF_SEND 2

#endif /* NO_GLOBALS */

#ifndef NO_DECLS

void send_buf_set_max_len(unsigned int max_len);
int send_buf_find(struct in_addr dst);
int send_buf_enqueue_packet(struct dsr_pkt *dp, int (*okfn)(struct dsr_pkt *));
int send_buf_set_verdict(int verdict, struct in_addr dst);
int send_buf_flush(void);
int send_buf_init(void);
void send_buf_cleanup(void);
void  send_buf_timeout(unsigned long data);
#endif /* NO_DECLS */

#endif /* _SEND_BUF_H */
