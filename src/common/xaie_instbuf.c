/******************************************************************************
* Copyright (C) 2022-2026 Advanced Micro Devices, Inc. All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "xaie_helper.h"
#include "xaie_instbuf.h"

/*****************************************************************************/
/**
* @file xaie_instbuf.c
*
* Implementation of the XDP instruction buffer: session lifecycle, dynamic
* storage for @ref AieRegOp entries, and internal record helpers used from
* @c xaie_helper.c.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Auto    04/10/2026  Initial creation
* 1.1   Auto    04/10/2026  Internal _XAie_*InstBuf* helpers; 256 initial and
*			    8K max slots; _XAie_EnsureInstBufCapacity growth path
* </pre>
*
******************************************************************************/

/*
 * The Ops array holds a sequence of AieRegOp records. Capacity is tracked as a
 * slot count; each slot uses sizeof(AieRegOp) bytes.
 *
 * - First allocation: 256 slots.
 * - Hard cap: 8K slots (8 * 1024), i.e. XAIE_INSTBUF_MAX_ENTRY_COUNT.
 */
#define XAIE_INSTBUF_INITIAL_ENTRY_COUNT 256U
#define XAIE_INSTBUF_MAX_ENTRY_COUNT (8U * 1024U)

typedef enum {
	XAIE_INSTBUF_SESSION_IDLE = 0,
	XAIE_INSTBUF_SESSION_RECORDING = 1,
} XAie_InstBufSessionState;

typedef struct {
	AieRegOp *Ops;
	u32 OpsCount;
	u32 OpsCapacity;
	u8 SessionState;
} XAie_InstBuff;

static XAie_InstBuff *_XAie_GetInstBuf(XAie_DevInst *DevInst)
{
	if (DevInst == XAIE_NULL || DevInst->InstBufPriv == NULL) {
		return NULL;
	}
	return (XAie_InstBuff *)DevInst->InstBufPriv;
}

/*****************************************************************************/
/**
*
* @brief Returns non-zero when the device has an active recording session.
*
* @param	DevInst: Device instance pointer.
*
* @return	1 if @c SessionState is recording, else 0.
*
******************************************************************************/
u8 _XAie_IsInstBufRecording(const XAie_DevInst *DevInst)
{
	const XAie_InstBuff *Buff;

	if (DevInst == XAIE_NULL || DevInst->InstBufPriv == NULL) {
		return 0U;
	}
	Buff = (const XAie_InstBuff *)DevInst->InstBufPriv;
	return (Buff->SessionState == XAIE_INSTBUF_SESSION_RECORDING) ? 1U : 0U;
}

static void _XAie_FreeInstBuf(XAie_InstBuff *Buff)
{
	if (Buff == NULL) {
		return;
	}
	free(Buff->Ops);
	free(Buff);
}

static AieRC _XAie_EnsureInstBufCapacity(XAie_InstBuff *Buff, u32 MinSlots)
{
	u32 NewCapacity;
	u32 MaxSlots;
	AieRegOp *AllocatedOps;

	MaxSlots = XAIE_INSTBUF_MAX_ENTRY_COUNT;
	if (MinSlots > MaxSlots) {
		XAIE_ERROR("Instruction buffer capacity exceeds max (%u AieRegOp slots)\n",
				(unsigned)MaxSlots);
		return XAIE_ERR;
	}

	if (Buff->OpsCapacity >= MinSlots) {
		return XAIE_OK;
	}

	NewCapacity = Buff->OpsCapacity ? Buff->OpsCapacity
					     : XAIE_INSTBUF_INITIAL_ENTRY_COUNT;
	while (NewCapacity < MinSlots) {
		u32 Next;

		if (NewCapacity >= MaxSlots) {
			XAIE_ERROR("Instruction buffer capacity overflow (requested "
					"%u slots)\n", MinSlots);
			return XAIE_ERR;
		}
		Next = NewCapacity * 2U;
		if (Next < NewCapacity) {
			XAIE_ERROR("Instruction buffer capacity overflow\n");
			return XAIE_ERR;
		}
		if (Next > MaxSlots) {
			Next = MaxSlots;
		}
		NewCapacity = Next;
	}

	/* realloc copies existing elements when growing the allocation. */
	AllocatedOps = (AieRegOp *)realloc(Buff->Ops,
			(size_t)NewCapacity * sizeof(AieRegOp));
	if (AllocatedOps == NULL) {
		XAIE_ERROR("Instruction buffer realloc failed (capacity %u)\n",
				NewCapacity);
		return XAIE_ERR;
	}
	Buff->Ops = AllocatedOps;
	Buff->OpsCapacity = NewCapacity;
	XAIE_DBG("Instruction buffer capacity now %u slots\n", NewCapacity);
	return XAIE_OK;
}

static AieRC _XAie_AppendInstBuf(XAie_DevInst *DevInst, const AieRegOp *RegOps)
{
	XAie_InstBuff *Buff = _XAie_GetInstBuf(DevInst);

	if (Buff == NULL) {
		XAIE_ERROR("Instruction buffer append: invalid state pointer\n");
		return XAIE_ERR;
	}
	if (Buff->SessionState != XAIE_INSTBUF_SESSION_RECORDING) {
		return XAIE_OK;
	}
	if (_XAie_EnsureInstBufCapacity(Buff, Buff->OpsCount + 1U) != XAIE_OK) {
		XAIE_ERROR("Instruction buffer append failed (op count %u)\n",
				Buff->OpsCount);
		return XAIE_ERR;
	}
	Buff->Ops[Buff->OpsCount] = *RegOps;
	Buff->OpsCount++;
	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* @brief Records a Write32 operation into the instruction buffer.
*
* @param	DevInst: Device instance pointer.
* @param	RegOff: Register offset.
* @param	Value: Value written.
*
* @return	XAIE_OK on success, XAIE_ERR if append fails.
*
******************************************************************************/
AieRC _XAie_Write32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 Value)
{
	AieRegOp RegOps;

	RegOps.opcode = AIE_REG_OP_WRITE32;
	RegOps.reg_off = RegOff;
	RegOps.mask = 0U;
	RegOps.value = Value;
	return _XAie_AppendInstBuf(DevInst, &RegOps);
}

/*****************************************************************************/
/**
*
* @brief Records a MaskWrite32 operation into the instruction buffer.
*
* @param	DevInst: Device instance pointer.
* @param	RegOff: Register offset.
* @param	Mask: Bit mask.
* @param	Value: Value written.
*
* @return	XAIE_OK on success, XAIE_ERR if append fails.
*
******************************************************************************/
AieRC _XAie_MaskWrite32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 Mask,
		u32 Value)
{
	AieRegOp RegOps;

	RegOps.opcode = AIE_REG_OP_MASKWRITE32;
	RegOps.reg_off = RegOff;
	RegOps.mask = Mask;
	RegOps.value = Value;
	return _XAie_AppendInstBuf(DevInst, &RegOps);
}

/*****************************************************************************/
/**
*
* @brief Records a Read32 operation (address only). Optionally zeroes @p Data
* for capture-only returns.
*
* @param	DevInst: Device instance pointer.
* @param	RegOff: Register offset.
* @param	Data: Optional; set to 0 when non-NULL after append.
*
* @return	XAIE_OK on success, XAIE_ERR if append fails.
*
******************************************************************************/
AieRC _XAie_Read32InstBuf(XAie_DevInst *DevInst, u64 RegOff, u32 *Data)
{
	AieRegOp RegOps;
	AieRC Status;

	RegOps.opcode = AIE_REG_OP_READ32;
	RegOps.reg_off = RegOff;
	RegOps.mask = 0U;
	RegOps.value = 0U;
	Status = _XAie_AppendInstBuf(DevInst, &RegOps);
	if (Data != NULL) {
		*Data = 0U;
	}
	return Status;
}

/*****************************************************************************/
/**
*
* This API starts or restarts an instruction-buffer capture session.
*
* @param	DevInst: Device instance pointer.
*
* @return	XAIE_OK on success, XAIE_INVALID_ARGS, or XAIE_ERR.
*
******************************************************************************/
AieRC XAie_StartInstBuf(XAie_DevInst *DevInst)
{
	XAie_InstBuff *Buff;

	if (DevInst == XAIE_NULL ||
			DevInst->IsReady != XAIE_COMPONENT_IS_READY) {
		XAIE_ERROR("Instruction buffer start: invalid device instance\n");
		return XAIE_INVALID_ARGS;
	}

	Buff = _XAie_GetInstBuf(DevInst);
	if (Buff == NULL) {
		Buff = (XAie_InstBuff *)calloc(1, sizeof(*Buff));
		if (Buff == NULL) {
			XAIE_ERROR("Instruction buffer start: calloc failed\n");
			return XAIE_ERR;
		}
		DevInst->InstBufPriv = (void *)Buff;
		XAIE_DBG("Instruction buffer state allocated\n");
	}

	if (Buff->Ops == NULL) {
		if (_XAie_EnsureInstBufCapacity(Buff,
				XAIE_INSTBUF_INITIAL_ENTRY_COUNT) != XAIE_OK) {
			XAIE_ERROR("Instruction buffer start: initial allocation "
					"failed\n");
			return XAIE_ERR;
		}
	}

	/*
	 * New recording session: clear the full allocated array so callers cannot
	 * rely on stale slots after a prior Complete or partial fill.
	 */
	if (Buff->Ops != NULL && Buff->OpsCapacity > 0U) {
		memset(Buff->Ops, 0,
				(size_t)Buff->OpsCapacity * sizeof(AieRegOp));
	}
	Buff->OpsCount = 0U;
	Buff->SessionState = XAIE_INSTBUF_SESSION_RECORDING;
	XAIE_DBG("Instruction buffer recording started\n");
	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API discards recorded operations without transferring the allocation to
* the caller. Session state is unchanged: if a recording session is active,
* capture continues and subsequent XAie traffic is still recorded.
*
* @param	DevInst: Device instance pointer.
*
* @return	XAIE_OK on success, XAIE_INVALID_ARGS if no buffer exists.
*
******************************************************************************/
AieRC XAie_ClearInstBuf(XAie_DevInst *DevInst)
{
	XAie_InstBuff *Buff;

	if (DevInst == XAIE_NULL ||
			DevInst->IsReady != XAIE_COMPONENT_IS_READY) {
		XAIE_ERROR("Instruction buffer clear: invalid device instance\n");
		return XAIE_INVALID_ARGS;
	}
	Buff = _XAie_GetInstBuf(DevInst);
	if (Buff == NULL) {
		XAIE_ERROR("Instruction buffer clear: no active buffer\n");
		return XAIE_INVALID_ARGS;
	}

	if (Buff->Ops != NULL && Buff->OpsCapacity > 0U) {
		memset(Buff->Ops, 0,
				(size_t)Buff->OpsCapacity * sizeof(AieRegOp));
	}
	Buff->OpsCount = 0U;
	XAIE_DBG("Instruction buffer cleared (ops discarded)\n");
	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API transfers ownership of the driver-allocated @c Ops array to the
* caller. There is no copy: the pointer is the same allocation used while
* recording (realloc-grown as needed). The instruction-buffer session becomes
* idle and the device holds no @c Ops until the next @ref XAie_StartInstBuf.
*
* @param	DevInst: Device instance pointer.
* @param	OutOps: Receives the buffer pointer, or NULL if no allocation exists.
* @param	OutOpsCount: Number of valid @ref AieRegOp entries at the start of
*		the buffer (the allocation may be larger).
*
* @return	XAIE_OK on success, XAIE_INVALID_ARGS on failure.
*
* @note		When @a OutOpsCount is 0, @a OutOps may still be non-NULL if the
*		buffer was allocated but no operations were recorded; the caller
*		must still pass @a OutOps to @ref XAie_FreeExportedInstBuf when
*		non-NULL.
*
******************************************************************************/
AieRC XAie_ExportInstBuf(XAie_DevInst *DevInst, AieRegOp **OutOps,
		u32 *OutOpsCount)
{
	XAie_InstBuff *Buff;
	AieRegOp *Transferred;
	u32 Count;

	if (DevInst == XAIE_NULL ||
			DevInst->IsReady != XAIE_COMPONENT_IS_READY) {
		XAIE_ERROR("Instruction buffer export: invalid device instance\n");
		return XAIE_INVALID_ARGS;
	}
	if (OutOps == NULL || OutOpsCount == NULL) {
		XAIE_ERROR("Instruction buffer export: invalid output pointer(s)\n");
		return XAIE_INVALID_ARGS;
	}

	Buff = _XAie_GetInstBuf(DevInst);
	if (Buff == NULL) {
		XAIE_ERROR("Instruction buffer export: no active buffer\n");
		return XAIE_INVALID_ARGS;
	}

	Count = Buff->OpsCount;
	Transferred = Buff->Ops;
	Buff->Ops = NULL;
	Buff->OpsCapacity = 0U;
	Buff->OpsCount = 0U;
	Buff->SessionState = XAIE_INSTBUF_SESSION_IDLE;

	*OutOps = Transferred;
	*OutOpsCount = Count;
	XAIE_DBG("Instruction buffer export: transferred %u operations\n", Count);
	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API frees the buffer returned by @ref XAie_ExportInstBuf.
*
* @param	Ops: Pointer from @ref XAie_ExportInstBuf, or NULL.
*
******************************************************************************/
void XAie_FreeExportedInstBuf(AieRegOp *Ops)
{
	free(Ops);
}

/*****************************************************************************/
/**
*
* Frees instruction-buffer state attached to the device instance.
* Invoked from @ref XAie_Finish only; not a public API.
*
* @param	DevInst: Device instance pointer.
*
* @return	None.
*
******************************************************************************/
void _XAie_FinishInstBuf(XAie_DevInst *DevInst)
{
	XAie_InstBuff *Buff;

	if (DevInst == XAIE_NULL) {
		return;
	}
	Buff = _XAie_GetInstBuf(DevInst);
	if (Buff == NULL) {
		return;
	}
	XAIE_DBG("Instruction buffer finish: releasing state\n");
	_XAie_FreeInstBuf(Buff);
	DevInst->InstBufPriv = NULL;
}
