/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_BLK_COPY_H
#define __LINUX_BLK_COPY_H

#include <linux/blk_types.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/workqueue_types.h>

struct blk_copy_params;
struct request;

enum blkdev_copy_phase {
	BLKDEV_TRANSLATE_LBAS,
	BLKDEV_COPY,
	BLKDEV_COPY_DONE,
};

/*
 * struct bio_copy_offload_ctx - context information for blkdev_copy_offload()
 * @params: Input parameters passed to blkdev_copy_offload().
 * @len: Number of bytes associated with this copy context.
 * @phase: Copy offload phase: either translating LBAs or copying data.
 * @lock: Protects @bios, @biotail and @bio_count.
 * @bios: List with REQ_OP_COPY_* bios for which LBA translation completed.
 * @biotail: Last element in the @bios list.
 * @bio_count: Number bios for which LBA translation has not yet completed.
 * @status: bio completion status.
 * @translation_complete: Called after LBA translation has completed.
 *	LBA translation has completed once bio_count drops to zero.
 */
struct bio_copy_offload_ctx {
	struct blk_copy_params *params;
	loff_t len;
	enum blkdev_copy_phase phase;
	spinlock_t lock;
	struct bio *bios __guarded_by(&lock);
	struct bio *biotail __guarded_by(&lock);
	u32 bio_count __guarded_by(&lock);
	blk_status_t status __guarded_by(&lock);
	void (*translation_complete)(struct bio_copy_offload_ctx *ctx);
};

#endif /* __LINUX_BLK_COPY_H */
