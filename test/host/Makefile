# Host test harness for ../../src/ide.c -- compiles the REAL driver source with
# the host C compiler against a mock Gayle/ATA model and checks its SCSI->ATA
# translation, probe gating, byte order handling and error paths.
#
#   make check     # build + run both variants; exit 0 == all assertions pass
#   make clean
#
# The driver is K&R C (-std=gnu89).  It is compiled in a kernel-only include
# world (-Istubs shadows sys/types.h, sys/errno.h, sys/inline.h, rico.h, sd.h)
# with HOST_TEST defined and mock_regs.h force-included so its register seam
# lands in the mock.  The harness files are ordinary host C.
#
# Two variants of the driver are built and run: the 68030 object as shipped,
# and the 68040/68060 one (-DIDE_CACHE=1 -DIDE_NBPP=4096), whose data-cache
# line operations, ordering barrier and TTR reads land in the mock as well.
#
# The root-class module form (-DIDE_MODULE) is built and run the same two
# ways (ide_modtest, ide_modtest_040): its load, deferred self-test, halt and
# unload entries against the same mock, with the kernel calls it adds mocked
# in ide_modtest.c.

CC      ?= cc
CSTD    := -std=gnu89 -fno-builtin
CWARN   := -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-non-prototype \
           -Wno-unused-function
CFLAGS  := $(CSTD) $(CWARN) -O0 -g
BUILD   := build
SRC     := ../../src
DRV     := $(SRC)/ide.c

CACHE     := -DIDE_CACHE=1 -DIDE_NBPP=4096
STUBS     := mock_regs.h stubs/rico.h stubs/sd.h \
             stubs/sys/types.h stubs/sys/errno.h stubs/sys/inline.h stubs/sys/param.h
HOST_OBJS := $(BUILD)/mock_gayle.o $(BUILD)/kstubs.o
BIN       := $(BUILD)/ide_test
BIN040    := $(BUILD)/ide_test_040
MODBIN    := $(BUILD)/ide_modtest
MODBIN040 := $(BUILD)/ide_modtest_040

.PHONY: all check clean
all: $(BIN) $(BIN040) $(MODBIN) $(MODBIN040)

$(BUILD)/ide.o: $(DRV) $(STUBS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DHOST_TEST -Istubs -I$(SRC) -include mock_regs.h -c $(DRV) -o $@

$(BUILD)/ide_040.o: $(DRV) $(STUBS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CACHE) -DHOST_TEST -Istubs -I$(SRC) -include mock_regs.h -c $(DRV) -o $@

$(BUILD)/ide_mod.o: $(DRV) $(STUBS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DHOST_TEST -DIDE_MODULE -Istubs -I$(SRC) -include mock_regs.h -c $(DRV) -o $@

$(BUILD)/ide_mod_040.o: $(DRV) $(STUBS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CACHE) -DHOST_TEST -DIDE_MODULE -Istubs -I$(SRC) -include mock_regs.h -c $(DRV) -o $@

$(BUILD)/ide_modtest_040.o: ide_modtest.c mock_gayle.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CACHE) -c $< -o $@

$(BUILD)/ide_test_040.o: ide_test.c mock_gayle.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CACHE) -c $< -o $@

$(BUILD)/%.o: %.c mock_gayle.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(BUILD)/ide.o $(BUILD)/ide_test.o $(HOST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN040): $(BUILD)/ide_040.o $(BUILD)/ide_test_040.o $(HOST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(MODBIN): $(BUILD)/ide_mod.o $(BUILD)/ide_modtest.o $(HOST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(MODBIN040): $(BUILD)/ide_mod_040.o $(BUILD)/ide_modtest_040.o $(HOST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

check: $(BIN) $(BIN040) $(MODBIN) $(MODBIN040)
	./$(BIN)
	./$(BIN040)
	./$(MODBIN)
	./$(MODBIN040)

clean:
	rm -rf $(BUILD)
