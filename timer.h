#ifndef _TIMER_H
#define _TIMER_H

#ifdef KERNEL26
#include <linux/jiffies.h>
#endif

typedef unsigned long usecs_t;

#ifdef NS2
#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>

class DSRUU;

typedef void (DSRUU::*fct_t)(unsigned long data);

class DSRUUTimer : public TimerHandler {
 public:
	DSRUUTimer(DSRUU *a) : TimerHandler() { a_ = a; name_ = "NoName";}
	DSRUUTimer(DSRUU *a, char *name) : TimerHandler() 
		{ a_ = a; name_ = name; }
	fct_t function;
	double expires;
	unsigned long data;
	void init(double expires_,  fct_t fct_, unsigned long data_ ) 
		{expires = expires_; data = data_; function = fct_;}
	char *get_name() { return name_; }
 protected:
	virtual void expire (Event *e);
	DSRUU *a_;
	char *name_;
};

static inline void gettime(struct timeval *tv)
{
	double now, usecs;
	
	/* Timeval is required, timezone is ignored */
	if (!tv)
		return;
	
	now = Scheduler::instance().clock();
	
	tv->tv_sec = (long)now; /* Removes decimal part */
	usecs = (now - tv->tv_sec) * 1000000;
	tv->tv_usec = (long)usecs;
}

#else

#include <linux/timer.h>

typedef struct timer_list DSRUUTimer;

static inline void set_timer(DSRUUTimer *t, struct timeval *expires)
{
	unsigned long exp_jiffies;
#ifdef KERNEL26
	exp_jiffies = jiffies + timeval_to_jiffies(expires) ;
#else
	exp_jiffies = jiffies + ((usecs * HZ) / 1000000l);
#endif
	if (timer_pending(t))
		mod_timer(t, exp_jiffies); 
	else {
		t->expires = exp_jiffies;
		add_timer(t);
	}
}

static inline void gettime(struct timeval *tv)
{
	unsigned long now = jiffies;

	if (!tv)
		return;
#ifdef KERNEL26
	jiffies_to_timeval(now, tv) ;
#else
	tv->tv_sec = now / HZ;

	if (HZ < 1000000)
		tv->tv_usec = now * (1000000l / HZ);
	else
		tv->tv_usec = (now * HZ) / 1000000l;
#endif	
}
#endif /* NS2 */

/* These functions may overflow (although unlikely)... Should probably be
 * improtved in the future */
static inline long timeval_diff(struct timeval *tv1, struct timeval *tv2)
{
	if (!tv1 || !tv2)
		return 0;
	else 
		return (long)((tv1->tv_sec - tv2->tv_sec) * 1000000 + 
			      tv1->tv_usec - tv2->tv_usec);
}

static inline int timeval_add_usecs(struct timeval *tv, usecs_t usecs)
{
    long add;		/* Protect against overflows */

    if (!tv)
	return -1;

    add = tv->tv_usec + usecs;
    tv->tv_sec += add / 1000000;
    tv->tv_usec = add % 1000000;

    return 0;
}
#endif /* _TIMER_H */