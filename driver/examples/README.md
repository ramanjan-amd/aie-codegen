//****************************************************************//
// Build and run the driver examples locally                      //
//****************************************************************//

Build examples (recommended)
------------------------------
From `driver/examples/`:

	make

This **builds the AIE-RT driver first** when `../../src/libaie_codegen.so` is
missing, by running `make -f Makefile.Linux -j8` in `../../src`. That makefile
copies headers into `../../include/` and `../../internal/`, then produces
`../../src/libaie_codegen.so`. The examples are then compiled and linked with
`-laie_codegen` and an rpath to `../../src`.

If you already built the driver and only changed an example source file,
`make` skips the src rebuild unless you remove or touch the library.

Build driver only (optional)
------------------------------
From the repository root:

	cd src
	make -f Makefile.Linux -j8

Manual `cc` (single line — keep the closing quote on `-Wl,-rpath,...`)
----------------------------------------------------------------------
If the shell shows a continuation prompt `>` after you paste a command, the
`-Wl,-rpath,'$ORIGIN/../../src'` argument is missing its **final** single
quote `'`.

`xaie_txn_reserialize_test.c` includes `xaie_helper.h` directly, so add both
`include/` and `include/aie_codegen_inc/` (same for `internal/` if you use it).
Build the driver in `src/` first so `libaie_codegen.so` exists.

One line each:

	cc -Wall -Wextra --std=c11 -I../../include -I../../include/aie_codegen_inc -I../../internal -I../../internal/aie_codegen_inc -L../../src -o xaie_txn_reserialize_test xaie_txn_reserialize_test.c -laie_codegen -Wl,-rpath,'$ORIGIN/../../src'

	cc -Wall -Wextra --std=c11 -I../../include -I../../include/aie_codegen_inc -I../../internal -I../../internal/aie_codegen_inc -L../../src -o xaie_instbuf_validation_test xaie_instbuf_validation_test.c -laie_codegen -Wl,-rpath,'$ORIGIN/../../src'

Alternative without embedding rpath: set `LD_LIBRARY_PATH` when running:

	LD_LIBRARY_PATH=../../src ./xaie_txn_reserialize_test ...

The umbrella header is `aie_codegen.h`. Adjust `-I` paths if your tree places
headers elsewhere.

Run transaction reserialize test
---------------------------------
Provide a transaction binary:

	./xaie_txn_reserialize_test <INPUT_TXN> [OUTPUT_TXN]

Run instruction-buffer validation test
--------------------------------------
No arguments; exercises performance counter and event broadcast / status APIs
under `XAie_StartInstBuf` / `XAie_CompleteInstBuf`:

	./xaie_instbuf_validation_test
