/******************************************************************************
* Copyright (C) 2022-2026 Advanced Micro Devices, Inc. All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
* @file xaie_instbuf.h
*
* Instruction buffer for XDP tooling: captures register operations (Write32,
* MaskWrite32, Read32) issued through the XAie_Write32 / XAie_MaskWrite32 /
* XAie_Read32 helpers when a recording session is active. Captured ops are
* stored as an array of @c AieRegOp (see below); the implementation allocates
* 256 slots initially and grows up to 8K (8 * 1024) slots maximum.
*
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Auto    04/10/2026  Initial creation
*
******************************************************************************/
#ifndef XAIE_INSTBUF_H
#define XAIE_INSTBUF_H

#include "xaiegbl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opcode of a recorded register operation for XDP analysis.
 */
typedef enum {
	AIE_REG_OP_WRITE32 = 1,
	AIE_REG_OP_MASKWRITE32 = 2,
	AIE_REG_OP_READ32 = 3,
} AieRegOpcode;

/**
 * @brief One recorded register operation.
 *
 * For @ref AIE_REG_OP_READ32, mask and value are unused; the read is logged
 * by address only.
 */
typedef struct {
	AieRegOpcode opcode;
	u64 reg_off;
	u32 mask;
	u32 value;
} AieRegOp;

XAIE_AIG_EXPORT AieRC XAie_StartInstBuf(XAie_DevInst *DevInst);
XAIE_AIG_EXPORT AieRC XAie_ClearInstBuf(XAie_DevInst *DevInst);
XAIE_AIG_EXPORT AieRC XAie_ExportInstBuf(XAie_DevInst *DevInst,
		AieRegOp **OutOps, u32 *OutOpsCount);
XAIE_AIG_EXPORT void XAie_FreeExportedInstBuf(AieRegOp *Ops);

void _XAie_FinishInstBuf(XAie_DevInst *DevInst);
u8 _XAie_IsInstBufRecording(const XAie_DevInst *DevInst);

AieRC _XAie_Write32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 Value);
AieRC _XAie_MaskWrite32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 Mask,
		u32 Value);
AieRC _XAie_Read32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 *Data);

#ifdef __cplusplus
}
#endif

#endif /* XAIE_INSTBUF_H */
