/* Startup + vector table for the STM32F303VC (F3 Discovery).
 * Same job as the QEMU startup: vectors, .data copy, .bss zero, board_init,
 * main. The F303 has 82 external interrupt lines; we run everything by
 * polling, so they all default-loop. */

#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

int main(void);
void board_init(void);

void Reset_Handler(void);
void Default_Handler(void);

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

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
static const vector_t g_vectors[16 + 82] = {
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    /* 82 external IRQs, all unused (we poll) */
    [16 ... 97] = Default_Handler,
};

void Reset_Handler(void)
{
    /* Point VTOR at our table explicitly (boot aliasing makes 0 work too,
     * but explicit is kinder to debuggers and future bootloaders). */
    *(volatile uint32_t *)0xE000ED08 = (uint32_t)g_vectors;

    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; )
        *dst++ = 0;

    board_init();
    main();
    for (;;)
        ;
}

void Default_Handler(void)
{
    for (;;)
        ;
}
