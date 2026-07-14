#!/usr/bin/env bash
# Flash an ELF onto the STM32F3 Discovery through its on-board ST-LINK.
# Usage: flash_f3.sh build/f3disco/demo_ring.elf
# Tries STM32CubeProgrammer first, then OpenOCD.

set -e
ELF="$1"
[ -f "$ELF" ] || { echo "usage: $0 <elf>"; exit 1; }

if command -v STM32_Programmer_CLI >/dev/null 2>&1; then
    BIN="${ELF%.elf}.bin"
    arm-none-eabi-objcopy -O binary "$ELF" "$BIN"
    STM32_Programmer_CLI -c port=SWD -w "$BIN" 0x08000000 -v -rst
elif command -v openocd >/dev/null 2>&1; then
    openocd -f board/stm32f3discovery.cfg \
            -c "program $ELF verify reset exit"
else
    echo "No flasher found. Install one of:"
    echo "  - STM32CubeProgrammer (STM32_Programmer_CLI on PATH)"
    echo "  - OpenOCD (choco install openocd)"
    exit 1
fi
