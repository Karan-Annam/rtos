/* ARMv7-M core peripheral registers -- the handful the kernel needs.
 * Deliberately not CMSIS: writing these by hand is half the point. */

#ifndef ARMV7M_H
#define ARMV7M_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- System Control Block ---- */
#define SCB_ICSR        REG32(0xE000ED04)
#define   ICSR_PENDSVSET  (1u << 28)
#define SCB_SHPR2       REG32(0xE000ED1C)  /* [31:24] = SVCall priority   */
#define SCB_SHPR3       REG32(0xE000ED20)  /* [23:16] PendSV, [31:24] SysTick */
#define SCB_CFSR        REG32(0xE000ED28)  /* configurable fault status   */
#define SCB_HFSR        REG32(0xE000ED2C)  /* hard fault status           */
#define SCB_MMFAR       REG32(0xE000ED34)  /* MemManage fault address     */
#define SCB_BFAR        REG32(0xE000ED38)  /* bus fault address           */
#define SCB_SHCSR       REG32(0xE000ED24)  /* enable usage/bus/mem faults */
#define   SHCSR_USGFAULTENA (1u << 18)
#define   SHCSR_BUSFAULTENA (1u << 17)
#define   SHCSR_MEMFAULTENA (1u << 16)

/* ---- SysTick ---- */
#define SYST_CSR        REG32(0xE000E010)
#define   SYST_CSR_ENABLE    (1u << 0)
#define   SYST_CSR_TICKINT   (1u << 1)
#define   SYST_CSR_CLKSOURCE (1u << 2)     /* 1 = processor clock         */
#define SYST_RVR        REG32(0xE000E014)
#define SYST_CVR        REG32(0xE000E018)

/* ---- NVIC (external interrupts 0..31 are all we use) ---- */
#define NVIC_ISER0      REG32(0xE000E100)  /* set-enable                  */
#define NVIC_ICER0      REG32(0xE000E180)  /* clear-enable                */
#define NVIC_ISPR0      REG32(0xE000E200)  /* set-pending (SW trigger)    */
#define NVIC_IPR(n)     (*(volatile uint8_t *)(0xE000E400 + (n)))

static inline void nvic_enable_irq(int irq)  { NVIC_ISER0 = 1u << irq; }
static inline void nvic_disable_irq(int irq) { NVIC_ICER0 = 1u << irq; }
static inline void nvic_pend_irq(int irq)    { NVIC_ISPR0 = 1u << irq; }
static inline void nvic_set_prio(int irq, uint8_t prio) { NVIC_IPR(irq) = prio; }

/* ---- DWT cycle counter (stats; not emulated by QEMU, guarded at runtime) */
#define DWT_CTRL        REG32(0xE0001000)
#define DWT_CYCCNT      REG32(0xE0001004)
#define DEMCR           REG32(0xE000EDFC)
#define   DEMCR_TRCENA    (1u << 24)

static inline void arch_wfi(void) { __asm__ volatile ("wfi"); }
static inline void arch_isb(void) { __asm__ volatile ("isb"); }
static inline void arch_dsb(void) { __asm__ volatile ("dsb"); }

#endif /* ARMV7M_H */
