# Link against ../../src/libaie_codegen.so; headers come from ../../include and
# ../../internal (populated by the same src build as the .so).
CC ?= cc
CFLAGS = -Wall -Wextra --std=c11
INCLUDES = -I../../include -I../../include/aie_codegen_inc \
	-I../../internal -I../../internal/aie_codegen_inc
LDFLAGS = -L../../src
LIBS = -laie_codegen
RPATH = -Wl,-rpath,'$$ORIGIN/../../src'

AIE_LIB = ../../src/libaie_codegen.so
CODEGEN_HDR = ../../include/aie_codegen.h

BINS = xaie_txn_reserialize_test xaie_instbuf_validation_test

.PHONY: all txn instbuf clean

all: txn instbuf

txn: xaie_txn_reserialize_test

instbuf: xaie_instbuf_validation_test

# Default src target builds include/, internal/, and libaie_codegen.so.
$(AIE_LIB) $(CODEGEN_HDR):
	$(MAKE) -C ../../src -f Makefile.Linux -j8

$(BINS): %: %.c $(AIE_LIB) $(CODEGEN_HDR)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $< $(LIBS) $(RPATH)

clean:
	rm -f $(BINS)
	$(MAKE) -C ../../src -f Makefile.Linux clean
