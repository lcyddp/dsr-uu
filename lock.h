/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
#ifndef __LOCK_H__
#define __LOCK_H__

#ifdef __KERNEL__
#include <linux/spinlock.h>

#define spin_lock_destroy(x)
#define rwlock_destroy(x)

#else
#include <pthread.h>

typedef pthread_mutex_t spinlock_t;

#define SPIN_LOCK_UNLOCKED PTHREAD_MUTEX_INITIALIZER

#define DEFINE_SPINLOCK(x) spinlock_t x = PTHREAD_MUTEX_INITIALIZER

#define spin_lock_init(x) pthread_mutex_init(x, NULL)
#define spin_lock_init_recursive(x) {                              \
	pthread_mutexattr_t attr;                                  \
	pthread_mutexattr_init(&attr);                             \
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); \
	pthread_mutex_init(&mutex, &attr);                         \
	pthread_mutexattr_destroy(&attr); }
/*
#define spin_lock_destroy(x) pthread_mutex_destroy(x)
#define spin_lock(x) pthread_mutex_lock(x)
#define spin_trylock(x) pthread_mutex_trylock(x)
#define spin_unlock(x) pthread_mutex_unlock(x)

#define spin_lock_bh(x) pthread_mutex_lock(x)
#define spin_trylock_bh(x) pthread_mutex_trylock(x)
#define spin_unlock_bh(x) pthread_mutex_unlock(x)

#define spin_lock_irqsave(x, flags) pthread_mutex_lock(x)
#define spin_unlock_irqrestore(x, flags) pthread_mutex_unlock(x)
*/
#define spin_lock_destroy(x) 
#define spin_lock(x) 
#define spin_trylock(x) 
#define spin_unlock(x) 

#define spin_lock_bh(x)
#define spin_trylock_bh(x)
#define spin_unlock_bh(x) 

#define spin_lock_irqsave(x, flags) 
#define spin_unlock_irqrestore(x, flags) 

typedef pthread_mutex_t rwlock_t;

#define __RW_LOCK_UNLOCKED(x) PTHREAD_MUTEX_INITIALIZER
#define DEFINE_RWLOCK(x) rwlock_t x = PTHREAD_MUTEX_INITIALIZER

/*
#define rwlock_init(x) pthread_mutex_init(x, NULL)
#define rwlock_destroy(x) pthread_mutex_destroy(x)
#define write_lock(x) pthread_mutex_lock(x)
#define read_lock(x) pthread_mutex_lock(x)
#define write_lock_bh(x) pthread_mutex_lock(x)
#define read_lock_bh(x) pthread_mutex_lock(x)
#define write_trylock(x) pthread_mutex_trylock(x)
#define read_trylock(x) pthread_mutex_trylock(x)
#define write_trylock_bh(x) pthread_mutex_trylock(x)
#define read_trylock_bh(x) pthread_mutex_trylock(x)
#define write_unlock(x) pthread_mutex_unlock(x)
#define read_unlock(x) pthread_mutex_unlock(x)
#define write_unlock_bh(x) pthread_mutex_unlock(x)
#define read_unlock_bh(x) pthread_mutex_unlock(x)
*/
#define rwlock_init(x)
#define rwlock_destroy(x)
#define write_lock(x) 
#define read_lock(x) 
#define write_lock_bh(x)
#define read_lock_bh(x)
#define write_trylock(x)
#define read_trylock(x) 
#define write_trylock_bh(x) 
#define read_trylock_bh(x)
#define write_unlock(x) 
#define read_unlock(x) 
#define write_unlock_bh(x) 
#define read_unlock_bh(x)

#define local_bh_disable()
#define local_bh_enable()

#endif /* __KERNEL__ */

#endif /* __LOCK_H__ */
