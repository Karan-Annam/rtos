/* Startup + vector table for QEMU mps2-an386 (Cortex-M4).
 *
 * On Cortex-M there is no "bootloader magic": the hardware reads the first
 * two words of memory at address 0 -- initial MSP and the reset vector --
 * and starts executing. Everything else (copying .data, zeroing .bss) is
 * our job, done here in Reset_Handler before main() runs.
 */

#include <stdint.h>

/* Symbols defined by the linker script */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

int main(void);
void board_init(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Kernel exception handlers (implemented in arch/armv7m). Weak-aliased to
 * Default_Handler so M0 links before the kernel exists. */
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void)        WEAK_DEFAULT;
void HardFault_Handler(void)  WEAK_DEFAULT;
void MemManage_Handler(void)  WEAK_DEFAULT;
void BusFault_Handler(void)   WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void)        WEAK_DEFAULT;
void DebugMon_Handler(void)   WEAK_DEFAULT;
void PendSV_Handler(void)     WEAK_DEFAULT;
void SysTick_Handler(void)    WEAK_DEFAULT;

/* A few external IRQ lines (NVIC interrupts 0..7). Tests can strongly
 * override one of these and trigger it via NVIC software-set-pending. */
void IRQ0_Handler(void) WEAK_DEFAULT;
void IRQ1_Handler(void) WEAK_DEFAULT;
void IRQ2_Handler(void) WEAK_DEFAULT;
void IRQ3_Handler(void) WEAK_DEFAULT;
void IRQ4_Handler(void) WEAK_DEFAULT;
void IRQ5_Handler(void) WEAK_DEFAULT;
void IRQ6_Handler(void) WEAK_DEFAULT;
void IRQ7_Handler(void) WEAK_DEFAULT;

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
static const vector_t g_vectors[] = {
    (vector_t)&_estack,     /*  0: initial MSP                */
    Reset_Handler,          /*  1: reset                      */
    NMI_Handler,            /*  2: NMI                        */
    HardFault_Handler,      /*  3: hard fault                 */
    MemManage_Handler,      /*  4: MPU fault                  */
    BusFault_Handler,       /*  5: bus fault                  */
    UsageFault_Handler,     /*  6: usage fault                */
    0, 0, 0, 0,             /*  7-10: reserved                */
    SVC_Handler,            /* 11: SVCall                     */
    DebugMon_Handler,       /* 12: debug monitor              */
    0,                      /* 13: reserved                   */
    PendSV_Handler,         /* 14: PendSV (context switch)    */
    SysTick_Handler,        /* 15: SysTick (kernel tick)      */
    /* External interrupts (NVIC 0..7), rest default          */
    IRQ0_Handler, IRQ1_Handler, IRQ2_Handler, IRQ3_Handler,
    IRQ4_Handler, IRQ5_Handler, IRQ6_Handler, IRQ7_Handler,
};

void Reset_Handler(void)
{
    /* Copy .data from its load address to RAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    /* Zero .bss */
    for (dst = &_sbss; dst < &_ebss; )
        *dst++ = 0;

    board_init();
    main();

    /* main() returning on bare metal has nowhere to go */
    for (;;)
        ;
}

void Default_Handler(void)
{
    for (;;)
        ;
}
