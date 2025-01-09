// SPDX-License-Identifier: GPL-2.0
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include "tty.h"

/* Legacy tty mutex glue */

/*
 * Getting the big tty mutex.
 */

void tty_lock(struct tty_struct *tty)
	ACQUIRE(tty->legacy_mutex)
{
	tty_kref_get(tty);
	mutex_lock(&tty->legacy_mutex);
}
EXPORT_SYMBOL(tty_lock);

int tty_lock_interruptible(struct tty_struct *tty)
	TRY_ACQUIRE(0, tty->legacy_mutex)
{
	int ret;

	tty_kref_get(tty);
	ret = mutex_lock_interruptible(&tty->legacy_mutex);
	if (ret)
		tty_kref_put(tty);
	return ret;
}

void tty_unlock(struct tty_struct *tty)
	RELEASE(tty->legacy_mutex)
{
	mutex_unlock(&tty->legacy_mutex);
	tty_kref_put(tty);
}
EXPORT_SYMBOL(tty_unlock);

void tty_lock_slave(struct tty_struct *tty)
	NO_THREAD_SAFETY_ANALYSIS /* conditional locking */
{
	if (tty && tty != tty->link)
		tty_lock(tty);
}

void tty_unlock_slave(struct tty_struct *tty)
	NO_THREAD_SAFETY_ANALYSIS /* conditional locking */
{
	if (tty && tty != tty->link)
		tty_unlock(tty);
}

void tty_set_lock_subclass(struct tty_struct *tty)
{
	lockdep_set_subclass(&tty->legacy_mutex, TTY_LOCK_SLAVE);
}
