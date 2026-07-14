# Tooling notes (Windows)

What this project was built and tested with.

## Toolchain

- **GNU Arm Embedded Toolchain 10.3** (`arm-none-eabi-gcc`) — installed at
  `C:\Program Files (x86)\GNU Arm Embedded Toolchain\10 2021.10\bin`, on PATH
- **GNU make** (chocolatey) + Git Bash for the shell scripts
- **QEMU 10.x** with `qemu-system-arm` — installed via MSYS2:

  ```sh
  /c/msys64/usr/bin/pacman -S mingw-w64-ucrt-x86_64-qemu
  ```

  (lands in `C:\msys64\ucrt64\bin`, which must be on PATH)

## Building & running

```sh
make                        # TARGET=qemu: kernel + all tests + demos
make TARGET=f3disco         # board build (demos only; tests need semihosting)
make run-test_04_sem        # build + run one test on QEMU
./run_all.sh                # the whole suite, N/N summary
make disasm-test_01_create_rr   # objdump | less
```

Build artifacts go to `build/<target>/`; a `.map` file is emitted next to
every ELF.

## QEMU specifics

- Machine: `mps2-an386` (ARM MPS2 board, AN386 FPGA image = Cortex-M4).
  25 MHz nominal clock; timing in QEMU is approximate but monotonic, which
  is why tests assert on *tick counts*, not wall time.
- `-semihosting`: test output and pass/fail exit codes travel over
  semihosting (`bkpt 0xAB`); `platform_exit(0)` makes QEMU exit 0.
- Interactive demos: input arrives via the emulated CMSDK UART with
  `-nographic`. Quit with `Ctrl-A` then `X`.
- Every run goes through `scripts/run_qemu.sh`, which wraps a `timeout` so
  a hung kernel fails in 30 s instead of blocking the suite.

## Flashing the F3 Discovery

`scripts/flash_f3.sh <elf>` tries, in order:

1. `STM32_Programmer_CLI` (STM32CubeProgrammer)
2. `openocd` (`choco install openocd`)

Serial console: PC4/PC5 @ 115200 8N1 — the ST-LINK VCP on newer board revs,
else any 3.3 V USB-serial adapter.

## Debugging odds & ends

- `qemu-system-arm ... -s -S` waits for gdb on :1234;
  `arm-none-eabi-gdb build/qemu/test.elf -ex 'target remote :1234'`
- The hard-fault handler already prints stacked pc/lr and the fault status
  registers — usually enough to skip gdb entirely.
- uint32_t is `unsigned long` on this toolchain: print with `%lu/%lx` or
  -Werror=format will complain.
