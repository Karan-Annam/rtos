/* STM32F3 Discovery board support: 72 MHz clock, USART1 console on PC4/PC5,
 * and the 8-LED compass ring on PE8..PE15.
 *
 * Console wiring: PC4 = USART1_TX, PC5 = USART1_RX (AF7), 115200 8N1.
 * On board revisions with ST-LINK/V2-B these pins ARE the ST-LINK virtual
 * COM port -- just open the ST-LINK serial port on the host. On older revs,
 * attach any 3.3V USB-serial adapter to PC4/PC5.
 *
 * Registers are written by hand (no vendor headers) with the reference
 * manual open -- same philosophy as the rest of the project. */

#include <stdint.h>
#include "board.h"

#define REG32(a) (*(volatile uint32_t *)(a))

/* ---- RCC (reset & clock control) ---- */
#define RCC_CR        REG32(0x40021000)
#define   CR_HSEON      (1u << 16)
#define   CR_HSERDY     (1u << 17)
#define   CR_HSEBYP     (1u << 18)
#define   CR_PLLON      (1u << 24)
#define   CR_PLLRDY     (1u << 25)
#define RCC_CFGR      REG32(0x40021004)
#define   CFGR_SW_PLL     0x2u
#define   CFGR_SWS_MASK   (0x3u << 2)
#define   CFGR_SWS_PLL    (0x2u << 2)
#define   CFGR_PPRE1_DIV2 (0x4u << 8)   /* APB1 = 36 MHz (max allowed)   */
#define   CFGR_PLLSRC_HSE (1u << 16)
#define   CFGR_PLLMUL_X9  (0x7u << 18)  /* 8 MHz HSE * 9 = 72 MHz        */
#define   CFGR_PLLMUL_X16 (0xEu << 18)  /* 4 MHz HSI/2 * 16 = 64 MHz     */
#define RCC_AHBENR    REG32(0x40021014)
#define   AHBENR_IOPCEN  (1u << 19)
#define   AHBENR_IOPEEN  (1u << 21)
#define RCC_APB2ENR   REG32(0x40021018)
#define   APB2ENR_USART1EN (1u << 14)

#define FLASH_ACR     REG32(0x40022000)
#define   ACR_PRFTBE    (1u << 4)

/* ---- GPIO ---- */
#define GPIOC_BASE    0x48000800u
#define GPIOE_BASE    0x48001000u
#define GPIO_MODER(b)  REG32((b) + 0x00)
#define GPIO_ODR(b)    REG32((b) + 0x14)
#define GPIO_BSRR(b)   REG32((b) + 0x18)
#define GPIO_AFRL(b)   REG32((b) + 0x20)

/* ---- USART1 (APB2 @ 72 MHz) ---- */
#define USART1_BASE   0x40013800u
#define USART_CR1     REG32(USART1_BASE + 0x00)
#define   CR1_UE        (1u << 0)
#define   CR1_RE        (1u << 2)
#define   CR1_TE        (1u << 3)
#define USART_BRR     REG32(USART1_BASE + 0x0C)
#define USART_ISR     REG32(USART1_BASE + 0x1C)
#define   ISR_RXNE      (1u << 5)
#define   ISR_TC        (1u << 6)
#define   ISR_TXE       (1u << 7)
#define USART_RDR     REG32(USART1_BASE + 0x24)
#define USART_TDR     REG32(USART1_BASE + 0x28)

static uint32_t g_sysclk_hz = 8000000u;   /* until clock_init succeeds */

/* The compass ring, clockwise from north: LD3(PE9) LD5(PE10) LD7(PE11)
 * LD9(PE12) LD10(PE13) LD8(PE14) LD6(PE15) LD4(PE8). */
static const uint8_t led_pin[8] = { 9, 10, 11, 12, 13, 14, 15, 8 };

static void clock_init(void)
{
    /* The Discovery's OSC_IN is fed 8 MHz by the ST-LINK MCO: HSE in
     * bypass mode (no crystal fitted). */
    RCC_CR |= CR_HSEBYP | CR_HSEON;

    uint32_t spin = 100000;
    while (!(RCC_CR & CR_HSERDY) && --spin)
        ;

    /* 72 MHz needs 2 flash wait states; enable the prefetch buffer too. */
    FLASH_ACR = (FLASH_ACR & ~0x7u) | 2u | ACR_PRFTBE;

    if (spin) {   /* HSE alive: 8 MHz x9 = 72 MHz */
        RCC_CFGR = CFGR_PPRE1_DIV2 | CFGR_PLLSRC_HSE | CFGR_PLLMUL_X9;
        g_sysclk_hz = 72000000u;
    } else {      /* fallback: HSI/2 = 4 MHz, x16 = 64 MHz */
        RCC_CFGR = CFGR_PPRE1_DIV2 | CFGR_PLLMUL_X16;
        g_sysclk_hz = 64000000u;
    }

    RCC_CR |= CR_PLLON;
    while (!(RCC_CR & CR_PLLRDY))
        ;
    RCC_CFGR |= CFGR_SW_PLL;
    while ((RCC_CFGR & CFGR_SWS_MASK) != CFGR_SWS_PLL)
        ;
}

static void gpio_init(void)
{
    RCC_AHBENR |= AHBENR_IOPCEN | AHBENR_IOPEEN;

    /* PC4/PC5 -> alternate function 7 (USART1) */
    GPIO_MODER(GPIOC_BASE) = (GPIO_MODER(GPIOC_BASE)
                              & ~((3u << (4 * 2)) | (3u << (5 * 2))))
                             | (2u << (4 * 2)) | (2u << (5 * 2));
    GPIO_AFRL(GPIOC_BASE) = (GPIO_AFRL(GPIOC_BASE)
                             & ~((0xFu << (4 * 4)) | (0xFu << (5 * 4))))
                            | (7u << (4 * 4)) | (7u << (5 * 4));

    /* PE8..PE15 -> outputs (the LED ring) */
    uint32_t moder = GPIO_MODER(GPIOE_BASE);
    for (int pin = 8; pin <= 15; pin++) {
        moder &= ~(3u << (pin * 2));
        moder |= 1u << (pin * 2);
    }
    GPIO_MODER(GPIOE_BASE) = moder;
}

static void uart_init(void)
{
    RCC_APB2ENR |= APB2ENR_USART1EN;
    USART_BRR = g_sysclk_hz / 115200u;   /* USART1 runs on APB2 = SYSCLK */
    USART_CR1 = CR1_UE | CR1_TE | CR1_RE;
}

void board_init(void)
{
    clock_init();
    gpio_init();
    uart_init();
}

uint32_t board_sysclk_hz(void)
{
    return g_sysclk_hz;
}

/* ------------------------------------------------------------- console */

void console_putc(char c)
{
    while (!(USART_ISR & ISR_TXE))
        ;
    USART_TDR = (uint8_t)c;
}

void console_write(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            console_putc('\r');
        console_putc(*s);
    }
}

int console_getc_nonblock(void)
{
    if (USART_ISR & ISR_RXNE)
        return (int)(USART_RDR & 0xFF);
    return -1;
}

int console_getc(void)
{
    int c;
    while ((c = console_getc_nonblock()) < 0)
        ;
    return c;
}

/* ---------------------------------------------------------------- LEDs */

int board_led_count(void)
{
    return 8;
}

void board_led_set(int idx, int on)
{
    if (idx < 0 || idx > 7)
        return;
    uint32_t bit = 1u << led_pin[idx];
    GPIO_BSRR(GPIOE_BASE) = on ? bit : bit << 16;
}

/* ------------------------------------------------------------ the end */

void platform_exit(int code)
{
    __asm__ volatile ("cpsid i");
    while (!(USART_ISR & ISR_TC))
        ;
    /* No simulator to leave: show the verdict on the ring and park.
     * All 8 solid = failure; alternating 4 = clean exit. */
    for (int i = 0; i < 8; i++)
        board_led_set(i, code ? 1 : (i & 1));
    for (;;)
        ;
}
