/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_IO_H
#define _FS_CEPH_IO_H

#include <linux/compiler_attributes.h>

int __must_check ceph_start_io_read(struct inode *inode)
	__cond_acquires_shared(0, &inode->i_rwsem);
void ceph_end_io_read(struct inode *inode)
	__releases_shared(&inode->i_rwsem);
int __must_check ceph_start_io_write(struct inode *inode)
	__cond_acquires(0, &inode->i_rwsem);
void ceph_end_io_write(struct inode *inode)
	__releases(&inode->i_rwsem);
int __must_check ceph_start_io_direct(struct inode *inode)
	__cond_acquires_shared(0, &inode->i_rwsem);
void ceph_end_io_direct(struct inode *inode)
	__releases_shared(&inode->i_rwsem);

#endif /* FS_CEPH_IO_H */
