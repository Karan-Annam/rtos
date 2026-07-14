#!/usr/bin/env bash
# Run one ELF on QEMU's mps2-an386 (Cortex-M4) with semihosting.
# Usage: run_qemu.sh <elf> [timeout_seconds]
# Exit code comes from the guest's semihosting SYS_EXIT (0 = pass),
# or 124 if the kernel hung and the timeout fired.

set -u
ELF="$1"
TMO="${2:-20}"

exec timeout --foreground "$TMO" \
    qemu-system-arm \
        -machine mps2-an386 \
        -cpu cortex-m4 \
        -nographic \
        -semihosting \
        -kernel "$ELF"
