#include "config.h"

#if INPUT_MODE == INPUT_MODE_UART

#include "link_uart.h"
#include "main.h"
#include <string.h>

/* USART1 runs from PCLK = 48 MHz (RCC_CFGR3.USART1SW at its reset value). */
#define LINK_UART_BRR 417U       /* 48 MHz / 115200 = 416.7 -> 115108 baud, -0.08 % */
#define LINK_UART_RTO_BITS 202U  /* 1.75 ms at 115200 = 201.6 bit times */
#define LINK_UART_IRQ_PRIO 2U    /* same as the pulse build's EXTI */

#define PA9_POS 9U
#define PA10_POS 10U

static uint8_t s_buf[LINK_RX_BUF];
static volatile size_t s_len;
static volatile bool s_ready; /* s_buf holds a complete frame not yet taken */
static volatile bool s_drop;  /* current frame is bad (overflow or line error); discard at RTO */

void link_uart_init(void)
{
    NVIC_DisableIRQ(USART1_IRQn);

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    USART1->CR1 = 0U; /* UE = 0: the rest may only be written while disabled */
    s_len = 0U;
    s_ready = false;
    s_drop = false;

    USART1->BRR = LINK_UART_BRR;
    USART1->CR2 = USART_CR2_RTOEN; /* 1 stop bit, receiver timeout on */
    /* ONEBIT: sample each bit once (mid-bit) instead of three times. HSI48 is
     * untrimmed here (no CRS sync source), so clock error is the main risk to
     * the link; the single-sample method raises the receiver's clock
     * tolerance from about 3.3 % to about 3.9 %. */
    USART1->CR3 = USART_CR3_ONEBIT;
    USART1->RTOR = LINK_UART_RTO_BITS;
    USART1->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF |
                  USART_ICR_IDLECF | USART_ICR_TCCF | USART_ICR_RTOCF;
    /* 8N1, oversampling 16 */
    USART1->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE | USART_CR1_RTOIE | USART_CR1_UE;

    /* Pins last, so TX is already idling high when it reaches the connector.
     * PA9 TX push-pull, no pull; PA10 RX with pull-up so an unplugged cable
     * reads idle. SYSCFG_CFGR1.PA11_PA12_RMP is left at its reset value (clear):
     * setting it would put PA11/PA12 on pins 17/18 instead. */
    GPIOA->OTYPER &= ~((1U << PA9_POS) | (1U << PA10_POS));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3U << (PA9_POS * 2U)) | (3U << (PA10_POS * 2U)))) |
                   (1U << (PA10_POS * 2U));
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFU << ((PA9_POS - 8U) * 4U)) | (0xFU << ((PA10_POS - 8U) * 4U)))) |
                    (1U << ((PA9_POS - 8U) * 4U)) | (1U << ((PA10_POS - 8U) * 4U));
    GPIOA->MODER = (GPIOA->MODER & ~((3U << (PA9_POS * 2U)) | (3U << (PA10_POS * 2U)))) |
                   (2U << (PA9_POS * 2U)) | (2U << (PA10_POS * 2U));

    NVIC_SetPriority(USART1_IRQn, LINK_UART_IRQ_PRIO);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);
}

void link_uart_irq(void)
{
    uint32_t isr = USART1->ISR;
    uint32_t errors = isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE);

    if ((isr & USART_ISR_RXNE) || errors)
    {
        if (s_ready)
        {
            /* The master has started a new request, so the frame still
             * waiting to be taken is stale (missed while the loop was busy,
             * e.g. during the boot LED sequence). Drop it. */
            s_ready = false;
            s_len = 0U;
            s_drop = false;
        }
    }

    if (errors)
    {
        /* A lost or corrupt byte: the frame cannot pass its CRC, so drop it. */
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        s_drop = true;
    }

    if (isr & USART_ISR_RXNE)
    {
        uint8_t b = (uint8_t)USART1->RDR; /* reading RDR clears RXNE */
        if (!s_drop)
        {
            if (s_len < LINK_RX_BUF)
            {
                s_buf[s_len++] = b;
            }
            else
            {
                s_drop = true; /* too long for any request we answer */
            }
        }
    }

    if (isr & USART_ISR_RTOF)
    {
        USART1->ICR = USART_ICR_RTOCF;
        if (s_drop)
        {
            s_len = 0U;
            s_drop = false;
        }
        else if (s_len > 0U)
        {
            s_ready = true;
        }
    }
}

bool link_uart_take_frame(uint8_t *buf, size_t *len)
{
    if (!s_ready)
    {
        return false;
    }

    bool got = false;
    NVIC_DisableIRQ(USART1_IRQn); /* at most LINK_RX_BUF bytes copied: well under one byte time */
    if (s_ready)
    {
        size_t n = s_len;
        memcpy(buf, s_buf, n);
        *len = n;
        s_len = 0U;
        s_ready = false;
        got = true;
    }
    NVIC_EnableIRQ(USART1_IRQn);
    return got;
}

void link_uart_flush(void)
{
    NVIC_DisableIRQ(USART1_IRQn);
    /* Mid-frame: drop the rest of it up to its RTO, never a truncated tail. */
    s_drop = s_drop || (s_len > 0U && !s_ready);
    s_len = 0U;
    s_ready = false;
    NVIC_EnableIRQ(USART1_IRQn);
}

void link_uart_send(const uint8_t *buf, size_t len)
{
    /* Receiver off while we talk, so an echoing adapter or a loopback cannot
     * hand our own reply back as a request. Back on after TC: the line is
     * idle-high then, so re-enabling cannot see a false start bit. */
    USART1->CR1 &= ~USART_CR1_RE;
    for (size_t i = 0U; i < len; i++)
    {
        while ((USART1->ISR & USART_ISR_TXE) == 0U)
        {
        }
        USART1->TDR = buf[i];
    }
    while ((USART1->ISR & USART_ISR_TC) == 0U)
    {
    }
    USART1->CR1 |= USART_CR1_RE;
}

#endif /* INPUT_MODE == INPUT_MODE_UART */
