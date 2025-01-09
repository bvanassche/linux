/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SEQMUTEX_H
#define _BCACHEFS_SEQMUTEX_H

#include <linux/mutex.h>

struct seqmutex {
	struct mutex	lock;
	u32		seq;
};

#define seqmutex_init(_lock)	mutex_init(&(_lock)->lock)

static inline bool seqmutex_trylock(struct seqmutex *lock)
	TRY_ACQUIRE(1, lock->lock)
{
	return mutex_trylock(&lock->lock);
}

static inline void seqmutex_lock(struct seqmutex *lock)
	ACQUIRE(lock->lock)
{
	mutex_lock(&lock->lock);
	lock->seq++;
}

static inline u32 seqmutex_unlock(struct seqmutex *lock)
	RELEASE(lock->lock)
{
	u32 seq = lock->seq;
	mutex_unlock(&lock->lock);
	return seq;
}

static inline bool seqmutex_relock(struct seqmutex *lock, u32 seq)
	NO_THREAD_SAFETY_ANALYSIS
{
	if (lock->seq != seq || !mutex_trylock(&lock->lock))
		return false;

	if (lock->seq != seq) {
		mutex_unlock(&lock->lock);
		return false;
	}

	return true;
}

#endif /* _BCACHEFS_SEQMUTEX_H */
