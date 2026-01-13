/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */
#ifndef __I915_GEM_WW_H__
#define __I915_GEM_WW_H__

#include <drm/drm_drv.h>

struct i915_gem_ww_ctx {
	struct ww_acquire_ctx ctx;
	struct list_head obj_list;
	struct drm_i915_gem_object *contended;
	bool intr;
};

void i915_gem_ww_ctx_init(struct i915_gem_ww_ctx *ctx, bool intr)
	__acquires(&ctx->ctx);
void i915_gem_ww_ctx_fini(struct i915_gem_ww_ctx *ctx)
	__releases(&ctx->ctx);
int __must_check i915_gem_ww_ctx_backoff(struct i915_gem_ww_ctx *ctx);
void i915_gem_ww_unlock_single(struct drm_i915_gem_object *obj);

/* Internal functions used by the inlines! Don't use. */
static inline void __i915_gem_ww_init(struct i915_gem_ww_ctx *ww, bool intr)
	__context_unsafe(/* balanced by __i915_gem_ww_fini() */)
{
	i915_gem_ww_ctx_init(ww, intr);
}

static inline int __i915_gem_ww_fini(struct i915_gem_ww_ctx *ww, int err)
	__context_unsafe(/* conditional release */)
{
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(ww);
		if (!err)
			err = -EDEADLK;
	}

	if (err != -EDEADLK)
		i915_gem_ww_ctx_fini(ww);

	return err;
}

#define for_i915_gem_ww(_ww, _err, _intr)			  \
	for (__i915_gem_ww_init(_ww, _intr), (_err) = -EDEADLK;	  \
	     (_err) == -EDEADLK;				  \
	     (_err) = __i915_gem_ww_fini(_ww, _err))
#endif
