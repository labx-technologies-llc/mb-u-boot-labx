// File        : preboot.h
// Author      : Yuriy Dragunov (yuriy.dragunov@labxtechnologies.com)
// Description : Pre-boot procedures for U-Boot.
// Copyright (c) 2012, Lab X Technologies, LLC.  All rights reserved.

#ifndef LABXLIB_PREBOOT_H
#define LABXLIB_PREBOOT_H

#ifdef CONFIG_LABX_PREBOOT
int labx_is_fallback_fpga(void);
int labx_is_golden_fpga(void);

int check_runtime_crcs(void);
int check_golden_crcs(void);
#endif

#endif /* LABXLIB_PREBOOT_H */

