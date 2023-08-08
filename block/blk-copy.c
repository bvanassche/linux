// SPDX-License-Identifier: GPL-2.0
/*
 * Offloaded and onloaded data copying support.
 */
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/blk-copy.h>
#include <linux/blk-mq.h>

/**
 * struct blkdev_copy_onload_ctx - Tracks the state of a single onloaded copy operation.
 * @params: Data copy parameters.
 * @read_work: For scheduling read work.
 * @write_work: For scheduling write work.
 * @buf: Data buffer.
 * @buf_len: Length in bytes of @buf.
 * @offset: Current copying offset. Range: [0, @len[.
 * @chunk: Size in bytes of the chunk of data that is being copied.
 */
struct blkdev_copy_onload_ctx {
	struct blk_copy_params *params;
	struct work_struct read_work;
	struct work_struct write_work;
	void *buf;
	ssize_t buf_len;
	loff_t offset;
	loff_t chunk;
};

/* End all bios in the @ctx->bios list with status @ctx->status. */
static void blkdev_end_bios(struct bio_copy_offload_ctx *ctx)
{
	struct bio *bio, *next;

	bio = ctx->bios;
	ctx->bios = NULL;
	for (; bio; bio = next) {
		next = bio->bi_next;
		bio->bi_status = ctx->status;
		bio_endio(bio);
	}
}

/*
 * Called after LBA translation finished for all bios associated with copy context
 * @ctx.
 */
static void blkdev_translation_complete(struct bio_copy_offload_ctx *ctx)
{
	struct module *owner = NULL;
	struct bio *bio;

	WARN_ON_ONCE(ctx->phase != BLKDEV_TRANSLATE_LBAS);
	ctx->phase = BLKDEV_COPY;

	/* Check whether all bios are associated with the same block driver. */
	for (bio = ctx->bios; bio; bio = bio->bi_next) {
		if (!owner) {
			owner = bio->bi_bdev->bd_disk->fops->owner;
		} else if (owner != bio->bi_bdev->bd_disk->fops->owner) {
			ctx->status = BLK_STS_INVAL;
			break;
		}
	}

	/* Remove the first bio from the bio list and submit it. */
	bio = ctx->bios;
	ctx->bios = bio->bi_next;
	bio->bi_next = NULL;
	if (ctx->biotail == bio)
		ctx->biotail = NULL;
	if (ctx->status == BLK_STS_OK)
		submit_bio(bio);
	else
		bio_endio(bio);
}

/* REQ_OP_COPY_* completion handler. */
static void blkdev_req_op_copy_done(struct bio *bio)
{
	struct bio_copy_offload_ctx *ctx = bio->bi_copy_ctx;
	struct blk_copy_params *params = ctx->params;
	blk_status_t status;

	switch (ctx->phase) {
	case BLKDEV_TRANSLATE_LBAS:
		scoped_guard(spinlock_irqsave, &ctx->lock)
			if (!ctx->status)
				ctx->status = bio->bi_status;
		break;
	case BLKDEV_COPY:
		status = ctx->status;
		ctx->phase = BLKDEV_COPY_DONE;
		blkdev_end_bios(ctx);
		kfree(ctx);
		scoped_guard(spinlock_irqsave, &params->lock) {
			if (!params->status)
				params->status = status;
		}
		if (atomic_dec_and_test(&params->copy_ctx_count))
			params->end_io(params);
		break;
	case BLKDEV_COPY_DONE:
		break;
	}
}

/*
 * Check that all LBA offsets are aligned with both the source and the destination
 * logical block sizes. Compare input and output length. Store the number of bytes
 * to be transferred in *@len.
 */
static int blkdev_copy_check_params(const struct blk_copy_params *params,
				    loff_t *len)
{
	const unsigned int mask =
		max(bdev_logical_block_size(params->in_bdev),
		    bdev_logical_block_size(params->out_bdev)) - 1;
	loff_t in_len = 0, out_len = 0;
	unsigned int i;

	for (i = 0; i < params->in_nseg; i++) {
		if ((params->in_segs[i].pos | params->in_segs[i].len) & mask)
			return -EINVAL;
		in_len += params->in_segs[i].len;
	}

	for (i = 0; i < params->out_nseg; i++) {
		if ((params->out_segs[i].pos | params->out_segs[i].len) & mask)
			return -EINVAL;
		out_len += params->out_segs[i].len;
	}

	if (in_len != out_len)
		return -EINVAL;

	*len = in_len;

	return 0;
}

/*
 * Calculate the number of bytes in the max_copy_src_segments input segments
 * starting from input segment @in_idx.
 */
static loff_t blk_max_src_len(const struct blk_copy_params *params,
			      unsigned int in_idx)
{
	uint16_t max_src_segments =
		params->in_bdev->bd_queue->limits.max_copy_src_segments;
	unsigned int max_i = min(params->in_nseg, in_idx + max_src_segments);
	loff_t len = 0;

	for (uint32_t i = in_idx; i < max_i; i++)
		len += params->in_segs[i].len;

	return len;
}

/*
 * Calculate the number of bytes in the max_copy_dst_segments output segments
 * starting from output segment @out_idx.
 */
static loff_t blk_max_dst_len(const struct blk_copy_params *params,
			      unsigned int out_idx)
{
	uint16_t max_dst_segments =
		params->out_bdev->bd_queue->limits.max_copy_dst_segments;
	unsigned int max_i = min(params->out_nseg, out_idx + max_dst_segments);
	loff_t len = 0;

	for (uint32_t i = out_idx; i < max_i; i++)
		len += params->out_segs[i].len;

	return len;
}

struct blkdev_copy_sync_ctx {
	struct completion compl;
	blk_status_t status;
};

static void blkdev_end_copy_sync(const struct blk_copy_params *params)
{
	struct blkdev_copy_sync_ctx *ctx = params->private;

	complete(&ctx->compl);
}

static int blkdev_copy_sync(struct blk_copy_params *params)
{
	struct blkdev_copy_sync_ctx ctx = {
		.compl = COMPLETION_INITIALIZER_ONSTACK(ctx.compl),
	};
	int ret;

	WARN_ON_ONCE(params->end_io || params->private);
	params->end_io = blkdev_end_copy_sync;
	params->private = &ctx;

	ret = blkdev_copy_offload(params);
	if (ret && ret != -EIOCBQUEUED)
		return ret;

	wait_for_completion(&ctx.compl);
	return blk_status_to_errno(ctx.status);
}

/**
 * blkdev_copy_chunk() - submit a single copy offload operation
 * @params: Copy offload input parameters.
 * @in_idx: Index of the input segment from where to start copying.
 * @out_idx: Index of the output segment to where to start copying.
 * @in_offset: Offset in bytes from the start of input segment @in_idx.
 * @out_offset: Offset in bytes from the start of output segment @out_idx.
 * @chunk: Maximum number of bytes to copy.
 *
 * Returns: the number of bytes covered by the submitted copy operation or a
 *	negative error number.
 */
static loff_t blkdev_copy_chunk(struct blk_copy_params *params, u32 *in_idx,
				u32 *out_idx, loff_t *in_offset,
				loff_t *out_offset, loff_t chunk)
{
	struct bio_copy_offload_ctx *ctx;
	u32 bio_count;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return -ENOMEM;

	spin_lock_init(&ctx->lock);
	ctx->params = params;
	ctx->phase = BLKDEV_TRANSLATE_LBAS;
	ctx->translation_complete = blkdev_translation_complete;
	/*
	 * Initialized to one to prevent that ctx->translation_complete() is
	 * called before bio submission has finished.
	 */
	ctx->bio_count = 1;

	WARN_ON_ONCE(chunk <= 0);
	chunk = min(chunk, blk_max_src_len(params, *in_idx) - *in_offset);
	WARN_ON_ONCE(chunk <= 0);
	chunk = min(chunk, blk_max_dst_len(params, *out_idx) - *out_offset);
	WARN_ON_ONCE(chunk <= 0);
	ctx->len = chunk;
	for (loff_t bytes, remaining_in = chunk; remaining_in > 0;
	     remaining_in -= bytes) {
		struct bio *src_bio;

		src_bio = bio_alloc(params->in_bdev, 0, REQ_OP_COPY_SRC,
				    GFP_NOIO);
		if (!src_bio) {
			if (remaining_in == chunk)
				goto free_ctx;
			else
				goto enomem;
		}
		atomic_inc(&params->copy_ctx_count);
		scoped_guard(spinlock_irqsave, &ctx->lock)
			ctx->bio_count++;
		bytes = min(remaining_in, params->in_segs[*in_idx].len -
			    *in_offset);
		src_bio->bi_iter.bi_size = bytes;
		src_bio->bi_iter.bi_sector = (params->in_segs[*in_idx].pos +
					      *in_offset) >> SECTOR_SHIFT;
		src_bio->bi_copy_ctx = ctx;
		src_bio->bi_end_io = blkdev_req_op_copy_done;
		*in_offset += bytes;
		if (*in_offset >= params->in_segs[*in_idx].len) {
			*in_offset -= params->in_segs[*in_idx].len;
			(*in_idx)++;
		}
		submit_bio(src_bio);
	}
	for (loff_t bytes, remaining_out = chunk; remaining_out;
	     remaining_out -= bytes) {
		struct bio *dst_bio;

		dst_bio = bio_alloc(params->out_bdev, 0, REQ_OP_COPY_DST,
				    GFP_NOIO);
		if (!dst_bio)
			goto enomem;
		scoped_guard(spinlock_irqsave, &ctx->lock)
			ctx->bio_count++;
		bytes = min(remaining_out, params->out_segs[*out_idx].len -
			    *out_offset);
		dst_bio->bi_iter.bi_size = bytes;
		dst_bio->bi_iter.bi_sector = (params->out_segs[*out_idx].pos +
					      *out_offset) >> SECTOR_SHIFT;
		dst_bio->bi_copy_ctx = ctx;
		dst_bio->bi_end_io = blkdev_req_op_copy_done;
		*out_offset += bytes;
		if (*out_offset >= params->out_segs[*out_idx].len) {
			*out_offset -= params->out_segs[*out_idx].len;
			(*out_idx)++;
		}
		submit_bio(dst_bio);
	}

dec_bio_count:
	scoped_guard(spinlock_irqsave, &ctx->lock)
		bio_count = --ctx->bio_count;
	if (bio_count == 0)
		ctx->translation_complete(ctx);
	return chunk;

enomem:
	scoped_guard(spinlock_irqsave, &ctx->lock)
		if (!ctx->status)
			ctx->status = BLK_STS_RESOURCE;
	chunk = -ENOMEM;
	goto dec_bio_count;

free_ctx:
	kfree(ctx);
	return -ENOMEM;
}

/**
 * blkdev_copy_offload() - copy data and offload copying if possible.
 * @params: Source and destination block device, data ranges and completion
 *	callback.
 *
 * If @params->end_io != NULL, data is copied asynchronously. If @params->end_io
 * == NULL, this function only returns after data copying finished.
 *
 * Return: 0 upon success; -EIOCBQUEUED if the completion callback function will
 *	be called or has already been called; -EOPNOTSUPP if copy offloading is
 *	not supported by the block device or if the source or destination
 *	address ranges span more than one dm device.
 */
int blkdev_copy_offload(struct blk_copy_params *params)
{
	loff_t in_offset = 0, out_offset = 0;
	u32 in_idx = 0, out_idx = 0;
	loff_t len, chunk, max_chunk;
	int ret;

	might_sleep();

	if (!params->end_io)
		return blkdev_copy_sync(params);

	spin_lock_init(&params->lock);

	if (!bdev_max_copy_sectors(params->in_bdev) ||
	    !bdev_max_copy_sectors(params->out_bdev))
		return -EOPNOTSUPP;

	ret = blkdev_copy_check_params(params, &len);
	if (ret)
		return ret;

	params->len = len;

	max_chunk = (u64)min(bdev_max_copy_sectors(params->in_bdev),
			     bdev_max_copy_sectors(params->out_bdev))
		    << SECTOR_SHIFT;

	atomic_set(&params->copy_ctx_count, 1);

	for (loff_t offset = 0; offset < len; offset += chunk) {
		chunk = min(len - offset, max_chunk);
		chunk = blkdev_copy_chunk(params, &in_idx, &out_idx, &in_offset,
					  &out_offset, chunk);
	}

	if (atomic_dec_and_test(&params->copy_ctx_count))
		params->end_io(params);

	return -EIOCBQUEUED;
}
EXPORT_SYMBOL_GPL(blkdev_copy_offload);

static void *blkdev_copy_alloc_buf(size_t req_size, size_t *alloc_size)
{
	unsigned int min_size = PAGE_SIZE;
	char *buf;

	while (req_size >= min_size) {
		buf = kmalloc(req_size, GFP_NOIO | __GFP_NOWARN);
		if (buf) {
			*alloc_size = req_size;
			return buf;
		}
		req_size >>= 1;
	}

	return NULL;
}

static struct bio *bio_map_buf(void *buf, unsigned int len)
{
	struct page *page;
	struct bio *bio;
	static const uint16_t nr_vecs = 1;

	bio = bio_kmalloc(nr_vecs, GFP_NOIO);
	if (!bio)
		return NULL;
	bio_init_inline(bio, /*bdev=*/NULL, /*max_vecs=*/nr_vecs, /*opf=*/0);

	page = virt_to_page(buf);
	if (bio_add_page(bio, page, len, offset_in_page(buf)) < len) {
		/* we don't support partial mappings */
		bio_uninit(bio);
		kfree(bio);
		WARN_ON_ONCE(true);
		return NULL;
	}

	return bio;
}

static void blkdev_write_done(struct bio *bio)
{
	struct blkdev_copy_onload_ctx *ctx = bio->bi_copy_ctx;
	struct blk_copy_params *params = ctx->params;
	blk_status_t sts = bio->bi_status;

	kfree(bio);

	if (sts) {
		params->status = sts;
		params->end_io(params);
		return;
	}

	ctx->offset += ctx->chunk;

	schedule_work(&ctx->read_work);
}

static sector_t blkdev_offset_to_out_pos(const struct blk_copy_params *params,
					 loff_t offset)
{
	for (int i = 0; i < params->out_nseg; i++) {
		loff_t rem = params->out_segs[i].len - offset;

		if (rem > 0)
			return params->out_segs[i].pos + offset;
		offset -= params->out_segs[i].len;
	}
	return 0;
}

static void blkdev_write_work(struct work_struct *work)
{
	struct blkdev_copy_onload_ctx *ctx =
		container_of(work, typeof(*ctx), read_work);
	struct blk_copy_params *params = ctx->params;
	struct bio *bio;
	loff_t out_pos;

	out_pos = blkdev_offset_to_out_pos(params, ctx->offset);

	bio = bio_map_buf(ctx->buf, ctx->buf_len);
	if (!bio) {
		params->status = BLK_STS_AGAIN;
		params->end_io(params);
		return;
	}
	bio->bi_opf = REQ_OP_WRITE;
	bio_set_dev(bio, params->out_bdev);
	bio->bi_iter.bi_sector = out_pos >> SECTOR_SHIFT;
	bio->bi_iter.bi_size = ctx->chunk;
	bio->bi_end_io = blkdev_write_done;
	bio->bi_copy_ctx = ctx;
	submit_bio(bio);
}

static void blkdev_read_done(struct bio *bio)
{
	struct blkdev_copy_onload_ctx *ctx = bio->bi_copy_ctx;
	struct blk_copy_params *params = ctx->params;
	blk_status_t sts = bio->bi_status;

	kfree(bio);

	if (sts) {
		params->status = sts;
		params->end_io(params);
		return;
	}

	schedule_work(&ctx->write_work);
}

static sector_t blkdev_offset_to_in_pos(const struct blk_copy_params *params,
					loff_t offset, loff_t *chunk)
{
	for (int i = 0; i < params->in_nseg; i++) {
		loff_t rem = params->in_segs[i].len - offset;

		if (rem > 0) {
			if (*chunk > rem)
				*chunk = rem;
			return params->in_segs[i].pos + offset;
		}
		offset -= params->in_segs[i].len;
	}
	*chunk = 0;
	return 0;
}

static void blkdev_read_work(struct work_struct *work)
{
	struct blkdev_copy_onload_ctx *ctx =
		container_of(work, typeof(*ctx), read_work);
	struct blk_copy_params *params = ctx->params;
	loff_t offset = ctx->offset;
	sector_t in_pos;
	struct bio *bio;

	ctx->chunk = min(ctx->buf_len, params->len - offset);
	if (ctx->chunk)
		in_pos = blkdev_offset_to_in_pos(params, offset, &ctx->chunk);
	if (ctx->chunk == 0) {
		params->end_io(params);
		return;
	}

	bio = bio_map_buf(ctx->buf, ctx->buf_len);
	if (!bio) {
		params->status = BLK_STS_AGAIN;
		params->end_io(params);
		return;
	}
	bio->bi_opf = REQ_OP_READ;
	bio_set_dev(bio, params->in_bdev);
	bio->bi_iter.bi_sector = in_pos >> SECTOR_SHIFT;
	bio->bi_iter.bi_size = ctx->chunk;
	bio->bi_end_io = blkdev_read_done;
	bio->bi_copy_ctx = ctx;
	submit_bio(bio);
}

/**
 * blkdev_copy_onload - asynchronously copy data between two block devices using
 *	read and write operations.
 * @params: Input and output block devices, input and output ranges and
 *	completion callback pointer.
 * Return: 0 upon success; -EIOCBQUEUED if the completion callback function will
 *	be called or has already been called.
 */
int blkdev_copy_onload(struct blk_copy_params *params)
{
	loff_t max_hw_bytes =
		min(queue_max_hw_sectors(params->in_bdev->bd_queue),
		    queue_max_hw_sectors(params->out_bdev->bd_queue)) <<
		SECTOR_SHIFT;
	struct blkdev_copy_onload_ctx *ctx;
	loff_t len;
	int ret;

	ret = blkdev_copy_check_params(params, &len);
	if (ret)
		return ret;

	params->len = len;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return -ENOMEM;

	INIT_WORK(&ctx->read_work, blkdev_read_work);
	INIT_WORK(&ctx->write_work, blkdev_write_work);
	ctx->params = params;

	ctx->buf = blkdev_copy_alloc_buf(min(max_hw_bytes, len), &ctx->buf_len);
	if (!ctx->buf)
		goto err;

	blkdev_read_work(&ctx->read_work);

	return -EIOCBQUEUED;

err:
	kfree(ctx);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(blkdev_copy_onload);
