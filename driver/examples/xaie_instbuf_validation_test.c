/******************************************************************************
* Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
* @file xaie_instbuf_validation_test.c
* @{
*
* Validates the XDP instruction buffer by recording register traffic while
* invoking performance counter and event broadcast / status APIs. When
* @ref XAie_StartInstBuf is active, @c XAie_Write32 /
* @c XAie_MaskWrite32 / @c XAie_Read32 record operations without backend MMIO.
* Buffer sizing matches @c xaie_instbuf.c (initial 256 slots, max 8K slots).
*
* Covers public instbuf API usage:
* - @ref XAie_StartInstBuf (first alloc, second session, after export)
* - @ref XAie_ClearInstBuf (no buffer, invalid dev, while recording without
*   ending capture; after export when idle)
* - @ref XAie_ExportInstBuf (no buffer, invalid dev, NULL outputs, empty capture,
*   full capture, second capture)
* - @ref XAie_FreeExportedInstBuf (NULL and non-NULL)
* - @ref XAie_Finish (releases @c _XAie_FinishInstBuf state)
*
* Usage: xaie_instbuf_validation_test
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Auto    04/10/2026  Initial creation
* 1.1   Auto    04/10/2026  More events APIs (generate, combo, stream, group,
*                           edge, PC, broadcast unblock)
* 1.2   Auto    04/10/2026  Align with instbuf limits; extra API checks
* 1.3   Auto    04/16/2026  Export / ClearInstBuf checks; export vs complete
* 1.4   Auto    04/16/2026  Transfer-only export; drop Complete/Reset tests
* 1.5   Auto    04/16/2026  Full API surface coverage (negative + sequences)
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include <stdio.h>
#include <stdlib.h>

#include <aie_codegen.h>

/***************************** Macro Definitions *****************************/
/* Match xaie_txn_reserialize_test.c (AIE2 IPU-style partition geometry). */
#define XAIE_BASE_ADDR		0x00000000000ULL
#define XAIE_NUM_ROWS		6
#define XAIE_NUM_COLS		5
#define XAIE_COL_SHIFT		25
#define XAIE_ROW_SHIFT		20
#define XAIE_SHIM_ROW		0
#define XAIE_RES_TILE_ROW_START	1
#define XAIE_RES_TILE_NUM_ROWS	1
#define XAIE_AIE_TILE_ROW_START	2
#define XAIE_AIE_TILE_NUM_ROWS	4

/* Must match src/common/xaie_instbuf.c (XAIE_INSTBUF_*_ENTRY_COUNT). */
#define INSTBUF_MAX_SLOTS	(8U * 1024U)

/*****************************************************************************/
/**
*
* Counts recorded opcodes and checks that Write32, MaskWrite32, and Read32
* were all observed (from the combined perfcnt and events calls).
*
******************************************************************************/
static int ValidateInstBufMix(const AieRegOp *Ops, u32 Count)
{
	u32 nWrite = 0U, nMask = 0U, nRead = 0U;
	u32 i;

	printf("Captured ops (idx : kind : reg_off : mask : value)\n");
	for(i = 0U; i < Count; i++) {
		const char *kind;

		switch(Ops[i].opcode) {
		case AIE_REG_OP_WRITE32:
			kind = "W32";
			nWrite++;
			break;
		case AIE_REG_OP_MASKWRITE32:
			kind = "MW32";
			nMask++;
			break;
		case AIE_REG_OP_READ32:
			kind = "R32";
			nRead++;
			break;
		default:
			printf("ERROR: Unknown opcode %u at index %u\n",
					(unsigned)Ops[i].opcode, (unsigned)i);
			return -1;
		}

		if(Ops[i].opcode == AIE_REG_OP_READ32) {
			printf("  %3u : %-4s : 0x%012llx :    n/a    :    n/a\n",
					(unsigned)i, kind,
					(unsigned long long)Ops[i].reg_off);
		} else {
			printf("  %3u : %-4s : 0x%012llx : 0x%08x : 0x%08x\n",
					(unsigned)i, kind,
					(unsigned long long)Ops[i].reg_off,
					(unsigned)Ops[i].mask, (unsigned)Ops[i].value);
		}
	}

	if(nWrite == 0U || nMask == 0U || nRead == 0U) {
		printf("ERROR: Expected at least one Write32, MaskWrite32, and Read32; "
				"got write=%u mask=%u read=%u (total %u ops)\n",
				(unsigned)nWrite, (unsigned)nMask, (unsigned)nRead,
				(unsigned)Count);
		return -1;
	}

	return 0;
}

/*****************************************************************************/
/**
*
* Exercise perf counter and events APIs so the instruction buffer captures a
* mixed stream of register operations.
*
******************************************************************************/
static AieRC RunInstBufCaptureWorkload(XAie_DevInst *DevInst, XAie_LocType Loc,
		u32 *PcVal, u32 *EvReg, u8 *EvBit,
		XAie_Events *EvStart, XAie_Events *EvStop, XAie_Events *EvRst)
{
	AieRC RC;

	RC = XAie_PerfCounterSet(DevInst, Loc, XAIE_CORE_MOD, 0U, 0x12345678U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterControlSet(DevInst, Loc, XAIE_CORE_MOD, 0U,
			XAIE_EVENT_TRUE_CORE, XAIE_EVENT_TRUE_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterResetControlSet(DevInst, Loc, XAIE_CORE_MOD, 0U,
			XAIE_EVENT_TIMER_SYNC_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterSet(DevInst, Loc, XAIE_CORE_MOD, 1U, 0x55aa55aaU);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterControlSet(DevInst, Loc, XAIE_CORE_MOD, 1U,
			XAIE_EVENT_NONE_CORE, XAIE_EVENT_NONE_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterEventValueSet(DevInst, Loc, XAIE_CORE_MOD, 1U,
			0x42U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterGet(DevInst, Loc, XAIE_CORE_MOD, 0U, PcVal);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_PerfCounterGetControlConfig(DevInst, Loc, XAIE_CORE_MOD, 0U,
			EvStart, EvStop, EvRst);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventBroadcast(DevInst, Loc, XAIE_CORE_MOD, 0U,
			XAIE_EVENT_NONE_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventBroadcast(DevInst, Loc, XAIE_CORE_MOD, 1U,
			XAIE_EVENT_TRUE_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventBroadcast(DevInst, Loc, XAIE_CORE_MOD, 2U,
			XAIE_EVENT_TIMER_SYNC_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventBroadcastReset(DevInst, Loc, XAIE_CORE_MOD, 3U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventRegStatus(DevInst, Loc, XAIE_CORE_MOD, 0U, EvReg);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventReadStatus(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_TRUE_CORE, EvBit);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventGenerate(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_TIMER_SYNC_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventComboConfig(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_COMBO0, XAIE_EVENT_COMBO_E1_AND_E2,
			XAIE_EVENT_TRUE_CORE, XAIE_EVENT_NONE_CORE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventComboReset(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_COMBO1);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventSelectStrmPort(DevInst, Loc, 0U,
			XAIE_STRMSW_SLAVE, CORE, 0U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventSelectStrmPortReset(DevInst, Loc, 1U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventGroupControl(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_GROUP_0_CORE, 0U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventEdgeControl(DevInst, Loc, XAIE_CORE_MOD, 0U,
			XAIE_EVENT_TRUE_CORE, (u8)XAIE_EDGE_EVENT_RISING);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventPCEnable(DevInst, Loc, 0U, 0x100U);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = XAie_EventBroadcastUnblockDir(DevInst, Loc, XAIE_CORE_MOD,
			XAIE_EVENT_SWITCH_A, 0U,
			(u8)XAIE_EVENT_BROADCAST_SOUTH);
	if(RC != XAIE_OK) {
		return RC;
	}

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* Main: partition init, instbuf API matrix, workload capture, export, finish.
*
******************************************************************************/
int main(void)
{
	AieRC RC;
	XAie_SetupConfig(ConfigPtr, XAIE_DEV_GEN_AIE2IPU, XAIE_BASE_ADDR,
			XAIE_COL_SHIFT, XAIE_ROW_SHIFT,
			XAIE_NUM_COLS, XAIE_NUM_ROWS, XAIE_SHIM_ROW,
			XAIE_RES_TILE_ROW_START, XAIE_RES_TILE_NUM_ROWS,
			XAIE_AIE_TILE_ROW_START, XAIE_AIE_TILE_NUM_ROWS);
	XAie_InstDeclare(DevInst, &ConfigPtr);

	XAie_LocType Loc = XAie_TileLoc(0U, XAIE_AIE_TILE_ROW_START);
	AieRegOp *ExportedOps = NULL;
	u32 ExportedCount = 0U;
	u32 OpCount = 0U;
	u32 PcVal = 0U;
	u32 EvReg = 0U;
	u8 EvBit = 0U;
	XAie_Events EvStart = XAIE_EVENT_NONE_CORE;
	XAie_Events EvStop = XAIE_EVENT_NONE_CORE;
	XAie_Events EvRst = XAIE_EVENT_NONE_CORE;

	printf("\n***********************************************************\n");
	printf("*  Instruction buffer validation (perfcnt / events)      *\n");
	printf("***********************************************************\n\n");

	printf("Initializing AIE device instance...\n");
	RC = XAie_SetupPartitionConfig(&DevInst, XAIE_BASE_ADDR, 1, 1);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_SetupPartitionConfig failed (RC=%d)\n", RC);
		return EXIT_FAILURE;
	}

	RC = XAie_CfgInitialize(&DevInst, &ConfigPtr);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_CfgInitialize failed (RC=%d)\n", RC);
		return EXIT_FAILURE;
	}

	RC = XAie_PmRequestTiles(&DevInst, NULL, 0);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_PmRequestTiles failed (RC=%d)\n", RC);
		XAie_Finish(&DevInst);
		return EXIT_FAILURE;
	}

	RC = XAie_PartitionInitialize(&DevInst, NULL);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_PartitionInitialize failed (RC=%d)\n", RC);
		XAie_Finish(&DevInst);
		return EXIT_FAILURE;
	}

	/*----------------------------------------------------------------------
	 * Negative: invalid device / missing InstBufPriv
	 *--------------------------------------------------------------------*/
	printf("\n--- Negative: invalid args / no instbuf container ---\n");
	{
		AieRegOp *BadExp = NULL;
		u32 BadCnt = 0U;

		XAie_FreeExportedInstBuf(NULL);

		if(XAie_StartInstBuf(XAIE_NULL) != XAIE_INVALID_ARGS) {
			printf("ERROR: XAie_StartInstBuf(NULL) expected "
					"XAIE_INVALID_ARGS\n");
			goto fail_finish;
		}
		if(XAie_ClearInstBuf(XAIE_NULL) != XAIE_INVALID_ARGS) {
			printf("ERROR: XAie_ClearInstBuf(NULL) expected "
					"XAIE_INVALID_ARGS\n");
			goto fail_finish;
		}
		if(XAie_ExportInstBuf(XAIE_NULL, &BadExp, &BadCnt) !=
				XAIE_INVALID_ARGS) {
			printf("ERROR: XAie_ExportInstBuf(NULL, ...) expected "
					"XAIE_INVALID_ARGS\n");
			goto fail_finish;
		}
		if(XAie_ClearInstBuf(&DevInst) != XAIE_INVALID_ARGS) {
			printf("ERROR: XAie_ClearInstBuf without container\n");
			goto fail_finish;
		}
		if(XAie_ExportInstBuf(&DevInst, &BadExp, &BadCnt) !=
				XAIE_INVALID_ARGS) {
			printf("ERROR: XAie_ExportInstBuf without container\n");
			goto fail_finish;
		}
	}

	/*----------------------------------------------------------------------
	 * StartInstBuf: allocate container + Ops; second start = new session
	 *--------------------------------------------------------------------*/
	printf("\n--- StartInstBuf: first and second session ---\n");
	RC = XAie_StartInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_StartInstBuf failed (RC=%d)\n", RC);
		goto fail_finish;
	}
	RC = XAie_StartInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: second XAie_StartInstBuf failed (RC=%d)\n", RC);
		goto fail_finish;
	}

	/*----------------------------------------------------------------------
	 * ExportInstBuf: empty capture (count 0); FreeExportedInstBuf
	 *--------------------------------------------------------------------*/
	printf("\n--- ExportInstBuf: empty (transfer allocation, count 0) ---\n");
	ExportedOps = NULL;
	ExportedCount = 0U;
	RC = XAie_ExportInstBuf(&DevInst, &ExportedOps, &ExportedCount);
	if(RC != XAIE_OK) {
		printf("ERROR: empty export RC=%d\n", RC);
		goto fail_finish;
	}
	if(ExportedCount != 0U) {
		printf("ERROR: empty export expected OutOpsCount 0\n");
		XAie_FreeExportedInstBuf(ExportedOps);
		goto fail_finish;
	}
	XAie_FreeExportedInstBuf(ExportedOps);
	ExportedOps = NULL;

	/*----------------------------------------------------------------------
	 * ExportInstBuf: invalid output pointers
	 *--------------------------------------------------------------------*/
	printf("\n--- ExportInstBuf: NULL OutOps / NULL OutOpsCount ---\n");
	RC = XAie_StartInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_StartInstBuf after empty export (RC=%d)\n", RC);
		goto fail_finish;
	}
	if(XAie_ExportInstBuf(&DevInst, NULL, &ExportedCount) !=
			XAIE_INVALID_ARGS) {
		printf("ERROR: export with NULL OutOps\n");
		goto fail_finish;
	}
	if(XAie_ExportInstBuf(&DevInst, &ExportedOps, NULL) !=
			XAIE_INVALID_ARGS) {
		printf("ERROR: export with NULL OutOpsCount\n");
		goto fail_finish;
	}

	/*----------------------------------------------------------------------
	 * ClearInstBuf while recording: discard ops, session stays recording
	 *--------------------------------------------------------------------*/
	printf("\n--- ClearInstBuf: clears buffer; recording continues ---\n");
	RC = XAie_ClearInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_ClearInstBuf while recording (RC=%d)\n", RC);
		goto fail_finish;
	}

	/*----------------------------------------------------------------------
	 * Full workload, export, validate, free
	 *--------------------------------------------------------------------*/
	printf("\n--- Full workload -> ExportInstBuf -> ValidateInstBufMix ---\n");
	RC = RunInstBufCaptureWorkload(&DevInst, Loc, &PcVal, &EvReg, &EvBit,
			&EvStart, &EvStop, &EvRst);
	if(RC != XAIE_OK) {
		printf("ERROR: RunInstBufCaptureWorkload failed (RC=%d)\n", RC);
		goto fail_finish;
	}
	(void)PcVal;
	(void)EvReg;
	(void)EvBit;
	(void)EvStart;
	(void)EvStop;
	(void)EvRst;

	RC = XAie_ExportInstBuf(&DevInst, &ExportedOps, &ExportedCount);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_ExportInstBuf after workload (RC=%d)\n", RC);
		goto fail_finish;
	}
	OpCount = ExportedCount;
	if(OpCount > INSTBUF_MAX_SLOTS) {
		printf("ERROR: op count %u > max\n", (unsigned)OpCount);
		RC = XAIE_ERR;
		XAie_FreeExportedInstBuf(ExportedOps);
		goto fail_finish;
	}
	if(OpCount > 0U && ExportedOps == NULL) {
		printf("ERROR: non-zero count with NULL OutOps\n");
		RC = XAIE_ERR;
		goto fail_finish;
	}

	printf("Total captured: %u register operation(s).\n\n",
			(unsigned)OpCount);
	if(ValidateInstBufMix(ExportedOps, OpCount) != 0) {
		RC = XAIE_ERR;
		XAie_FreeExportedInstBuf(ExportedOps);
		goto fail_finish;
	}
	printf("Validation passed (Write32 / MaskWrite32 / Read32 all present).\n");

	XAie_FreeExportedInstBuf(ExportedOps);
	ExportedOps = NULL;

	/*----------------------------------------------------------------------
	 * After export: session idle, Ops gone — Clear then empty export
	 *--------------------------------------------------------------------*/
	printf("\n--- ClearInstBuf after export (idle); ExportInstBuf empty ---\n");
	RC = XAie_ClearInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_ClearInstBuf after export (RC=%d)\n", RC);
		goto fail_finish;
	}
	RC = XAie_ExportInstBuf(&DevInst, &ExportedOps, &ExportedCount);
	if(RC != XAIE_OK) {
		printf("ERROR: export after idle clear (RC=%d)\n", RC);
		goto fail_finish;
	}
	if(ExportedCount != 0U || ExportedOps != NULL) {
		printf("ERROR: expected empty export after idle clear\n");
		RC = XAIE_ERR;
		XAie_FreeExportedInstBuf(ExportedOps);
		goto fail_finish;
	}
	XAie_FreeExportedInstBuf(ExportedOps);
	ExportedOps = NULL;

	/*----------------------------------------------------------------------
	 * Second capture: Start after export, small workload, export, free
	 *--------------------------------------------------------------------*/
	printf("\n--- Second capture: StartInstBuf -> partial APIs -> export ---\n");
	RC = XAie_StartInstBuf(&DevInst);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_StartInstBuf for second capture (RC=%d)\n", RC);
		goto fail_finish;
	}
	RC = XAie_EventBroadcast(&DevInst, Loc, XAIE_CORE_MOD, 0U,
			XAIE_EVENT_NONE_CORE);
	if(RC != XAIE_OK) {
		printf("ERROR: XAie_EventBroadcast (second capture) RC=%d\n", RC);
		goto fail_finish;
	}
	RC = XAie_ExportInstBuf(&DevInst, &ExportedOps, &ExportedCount);
	if(RC != XAIE_OK) {
		printf("ERROR: second XAie_ExportInstBuf RC=%d\n", RC);
		goto fail_finish;
	}
	if(ExportedCount == 0U) {
		printf("ERROR: second capture expected non-zero op count\n");
		RC = XAIE_ERR;
		XAie_FreeExportedInstBuf(ExportedOps);
		goto fail_finish;
	}
	if(ExportedOps == NULL) {
		printf("ERROR: second capture NULL OutOps with non-zero count\n");
		RC = XAIE_ERR;
		goto fail_finish;
	}
	printf("Second capture: %u op(s).\n", (unsigned)ExportedCount);
	XAie_FreeExportedInstBuf(ExportedOps);
	ExportedOps = NULL;

	/*----------------------------------------------------------------------
	 * XAie_Finish: releases instbuf container (_XAie_FinishInstBuf)
	 *--------------------------------------------------------------------*/
	printf("\n--- XAie_Finish (releases instbuf state) ---\n");
	XAie_Finish(&DevInst);

	printf("\n***********************************************************\n");
	printf("*  ALL TESTS PASSED                                         *\n");
	printf("***********************************************************\n\n");

	return EXIT_SUCCESS;

fail_finish:
	XAie_FreeExportedInstBuf(ExportedOps);
	XAie_Finish(&DevInst);
	printf("\nTEST FAILED (RC=%d)\n", RC);
	return EXIT_FAILURE;
}

/** @} */
