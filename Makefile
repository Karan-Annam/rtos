# ==========================================================================
#  RTOS build
#
#  make TARGET=qemu     (default)  -> build/qemu/<name>.elf, runs on QEMU
#  make TARGET=f3disco             -> build/f3disco/<name>.elf, flash to board
#
#  make            - build kernel + all tests + all apps for TARGET
#  make tests      - build all tests
#  make run-<name> - build and run one test/app on QEMU (e.g. run-test_00_hello)
#  make clean
# ==========================================================================

TARGET ?= qemu

CROSS    := arm-none-eabi-
CC       := $(CROSS)gcc
OBJDUMP  := $(CROSS)objdump
SIZE     := $(CROSS)size

# Soft-float everywhere for now so QEMU tests and the board run the *same*
# kernel code paths (FPU lazy stacking is a stretch milestone).
CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft

ifeq ($(TARGET),qemu)
  BOARD_DIR := board/qemu_mps2
  LDSCRIPT  := $(BOARD_DIR)/mps2_an386.ld
else ifeq ($(TARGET),f3disco)
  BOARD_DIR := board/f3disco
  LDSCRIPT  := $(BOARD_DIR)/stm32f303vc.ld
else
  $(error unknown TARGET '$(TARGET)' (use qemu or f3disco))
endif

BUILD := build/$(TARGET)

CFLAGS := $(CPUFLAGS) -O2 -g3 -Wall -Wextra -Werror \
          -ffunction-sections -fdata-sections -fno-common \
          -Ikernel/inc -Iarch/armv7m -I$(BOARD_DIR) \
          -MMD -MP
ASFLAGS := $(CPUFLAGS) -g3 -MMD -MP
LDFLAGS  = $(CPUFLAGS) -T $(LDSCRIPT) -nostartfiles --specs=nano.specs \
           -Wl,--gc-sections -Wl,-Map=$(@:.elf=.map)

# --- kernel + arch + board sources (shared by every image) ---------------
KERNEL_SRCS := $(wildcard kernel/src/*.c) \
               $(wildcard arch/armv7m/*.c) \
               $(wildcard $(BOARD_DIR)/*.c) \
               $(wildcard shell/*.c)
KERNEL_ASMS := $(wildcard arch/armv7m/*.S)

KERNEL_OBJS := $(KERNEL_SRCS:%.c=$(BUILD)/obj/%.o) \
               $(KERNEL_ASMS:%.S=$(BUILD)/obj/%.o)

# --- one image per test / app ---------------------------------------------
TEST_NAMES := $(notdir $(basename $(wildcard tests/*.c)))
APP_NAMES  := $(notdir $(basename $(wildcard apps/*.c)))

TEST_ELFS := $(TEST_NAMES:%=$(BUILD)/%.elf)
APP_ELFS  := $(APP_NAMES:%=$(BUILD)/%.elf)

# Tests rely on semihosting to report pass/fail, so they're QEMU-only;
# the board target builds the demo apps.
ifeq ($(TARGET),qemu)
all: tests apps
else
all: apps
endif
tests: $(TEST_ELFS)
apps: $(APP_ELFS)

$(BUILD)/%.elf: $(BUILD)/obj/tests/%.o $(KERNEL_OBJS) $(LDSCRIPT)
	$(CC) $(filter %.o,$^) $(LDFLAGS) -o $@
	@$(SIZE) $@

$(BUILD)/%.elf: $(BUILD)/obj/apps/%.o $(KERNEL_OBJS) $(LDSCRIPT)
	$(CC) $(filter %.o,$^) $(LDFLAGS) -o $@
	@$(SIZE) $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

# --- convenience -----------------------------------------------------------
run-%: $(BUILD)/%.elf
	scripts/run_qemu.sh $<

disasm-%: $(BUILD)/%.elf
	$(OBJDUMP) -d $< | less

clean:
	rm -rf build

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

.PHONY: all tests apps clean run-% disasm-%
