/* Interrupt-masking critical sections for ARMv7-M.
 *
 * The whole kernel's concurrency story rests on these two inlines: every
 * mutation of the ready bitmap, ready queues, wait lists and timer lists
 * happens between irq_lock()/irq_unlock(), so an interrupt (and therefore a
 * preemption) can never observe or corrupt a half-updated structure. That is
 * the "no lost wakeups" rule from the design notes.
 *
 * We save/restore PRIMASK rather than blindly cpsie, so critical sections
 * nest correctly and are safe to use inside ISRs.
 */

#ifndef ARMV7M_CRITICAL_H
#define ARMV7M_CRITICAL_H

#include <stdint.h>

static inline uint32_t irq_lock(void)
{
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask \n cpsid i"
                      : "=r"(primask) :: "memory");
    return primask;
}

static inline void irq_unlock(uint32_t primask)
{
    __asm__ volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

/* IPSR holds the active exception number: 0 = thread mode. Lets the kernel
 * reject blocking calls made from interrupt context. */
static inline uint32_t arch_in_isr(void)
{
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
}

#endif /* ARMV7M_CRITICAL_H */
