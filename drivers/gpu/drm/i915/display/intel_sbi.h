/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _INTEL_SBI_H_
#define _INTEL_SBI_H_

#include <linux/types.h>

#include "intel_display_core.h"

struct intel_display;

enum intel_sbi_destination {
	SBI_ICLK,
	SBI_MPHY,
};

void intel_sbi_init(struct intel_display *display);
void intel_sbi_fini(struct intel_display *display);
void intel_sbi_lock(struct intel_display *display)
	__acquires(&display->sbi.lock);
void intel_sbi_unlock(struct intel_display *display)
	__releases(&display->sbi.lock);
u32 intel_sbi_read(struct intel_display *display, u16 reg,
		   enum intel_sbi_destination destination);
void intel_sbi_write(struct intel_display *display, u16 reg, u32 value,
		     enum intel_sbi_destination destination);

#endif /* _INTEL_SBI_H_ */
