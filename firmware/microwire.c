/*
 * Tiny Microwire master bit-bang
 */

#include <avr/io.h>
#include "isp.h"
#include "microwire.h"

#define ORG_OFFSET 6

#ifndef F_CPU
#define F_CPU 12000000UL
#endif

// Атрибут noinline ЗАПРЕЩАЕТ компилятору копировать этот цикл повсюду.
// Теперь это единая подпрограмма, вызываемая через rcall.
__attribute__((noinline))
static void mw_delay(void) {
    __builtin_avr_delay_cycles((F_CPU / 1000000UL) * 10UL);
}

/* ---------- local helpers ---------- */
// Убрано: mw_cs_hi и mw_cs_lo. Используем макросы CS_HI() и CS_LOW() из isp.h

static inline void mw_clk_hi(void)   { ISP_OUT |=  (1 << ISP_SCK); }
static inline void mw_clk_lo(void)   { ISP_OUT &= ~(1 << ISP_SCK); }
static inline void mw_si_hi(void)    { ISP_OUT |=  (1 << ISP_MOSI); }
static inline void mw_si_lo(void)    { ISP_OUT &= ~(1 << ISP_MOSI); }
static inline uint8_t mw_so_read(void){ return (ISP_IN & (1 << ISP_MISO)) ? 1 : 0; }

/* ---------- public API ---------- */

void mwBegin(void)
{
    /* CS=RST must be output */
    ISP_DDR |= (1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI);
    ISP_DDR &= ~(1 << ISP_MISO);
    mw_clk_lo();
    mw_si_lo();
    CS_LOW();  // <--- ИСПОЛЬЗУЕМ МАКРОС ИЗ isp.h
} 

void mwStart(void)
{
    CS_LOW();   // <--- ИСПОЛЬЗУЕМ МАКРОС
    mw_clk_lo();
    mw_delay();

    CS_HI();    // <--- ИСПОЛЬЗУЕМ МАКРОС
    mw_delay();
}

void mwEnd(void)
{
    CS_LOW();   // <--- ИСПОЛЬЗУЕМ МАКРОС
    mw_clk_lo();
}
void mwSendData(uint16_t data, uint8_t bits)
{
    for (uint16_t mask = 1U << (bits - 1); mask; mask >>= 1) {
        mw_clk_lo();
        if (data & mask) 
            mw_si_hi();
        else 
            mw_si_lo();
        mw_delay();
        mw_clk_hi();
        mw_delay();
        mw_clk_lo();
    }
}
uint8_t mwReadByte(void)
{
    uint8_t val = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        mw_clk_lo();
        mw_delay();
        mw_clk_hi();
        val = (val << 1) | mw_so_read();
        mw_delay();
        mw_clk_lo();
    }
    return val;
}

uint8_t mwBusy(void)
{
    mwBegin();
    CS_HI();
    mw_delay();
    uint8_t busy = mw_so_read() == 0;
    mwEnd();
    return busy;
}
