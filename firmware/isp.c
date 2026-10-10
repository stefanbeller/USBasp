/*
 * isp.c - part of USBasp
 *
 * ISP (In-System Programming) interface: SPI master either bit-banged in
 * software or driven by the ATmega hardware SPI unit. Also implements the
 * automatic SCK speed search used when entering programming mode.
 *
 * Licence: GNU GPL v2 (see Readme.txt)
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stddef.h>

#include "isp.h"
#include "clock.h"
#include "usbasp.h"
#include "i2c.h"

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

/* Safe stub: before a speed has been configured, transactions return 0xFF
 * (idle-SPI behaviour) instead of hanging the firmware. */
static uint8_t dummy_transmit(uint8_t b) {
    (void)b;
    return 0xFF;
}

uint8_t (*ispTransmit)(uint8_t) = dummy_transmit;

uint8_t last_success_speed = USBASP_ISP_SCK_3000; /* cached for AUTO mode */
uint8_t sck_sw_delay = 0;                         /* half-period ticks, SW SPI */
uint8_t isp_hiaddr = 0xFF;                        /* last extended-address byte sent */

/* ------------------------------------------------------------------ */
/* Speed tables                                                        */
/* ------------------------------------------------------------------ */

#define SPI_BASE ((1 << SPE) | (1 << MSTR))   /* enable + master mode */

/* Hardware SPI divider settings, indexed by (USBASP_ISP_SCK_3000 - option),
 * valid for a 12 MHz crystal. SPCR holds SPR1/SPR0, SPSR holds SPI2X. */
static const uint8_t hw_spcr_table[] PROGMEM = {
    0,                              /* 3.000 MHz   f/4   SPI2X=0 */
    (1 << SPR0),                    /* 1.500 MHz   f/8   SPI2X=1 */
    (1 << SPR0),                    /* 750 kHz     f/16  SPI2X=0 */
    (1 << SPR1),                    /* 375 kHz     f/32  SPI2X=1 */
    (1 << SPR1),                    /* 187.5 kHz   f/64  SPI2X=0 */
    (1 << SPR1) | (1 << SPR0)       /* 93.75 kHz   f/128 SPI2X=0 */
};

static const uint8_t hw_spsr_table[] PROGMEM = {
    0,                              /* 3.000 MHz   */
    (1 << SPI2X),                   /* 1.500 MHz   */
    0,                              /* 750 kHz     */
    (1 << SPI2X),                   /* 375 kHz     */
    0,                              /* 187.5 kHz   */
    0                               /* 93.75 kHz   */
};

/* Software SPI half-period delay (timer ticks ~320 us per count step),
 * indexed by (USBASP_ISP_SCK_32 - option). */
static const uint8_t sw_delay_table[] PROGMEM = { 3, 6, 12, 24, 48, 96, 192 };

/* Speed search order for automatic SCK detection: fastest to slowest. */
static const uint8_t isp_retry_speeds[] PROGMEM = {
    USBASP_ISP_SCK_3000, USBASP_ISP_SCK_1500, USBASP_ISP_SCK_750,
    USBASP_ISP_SCK_375,  USBASP_ISP_SCK_187_5, USBASP_ISP_SCK_93_75,
    USBASP_ISP_SCK_32,   USBASP_ISP_SCK_16,    USBASP_ISP_SCK_8,
    USBASP_ISP_SCK_4,    USBASP_ISP_SCK_2,     USBASP_ISP_SCK_1,
    USBASP_ISP_SCK_0_5
};

#define ISP_SPEED_CNT (sizeof(isp_retry_speeds) / sizeof(isp_retry_speeds[0]))
#define GET_SPEED(idx) pgm_read_byte(&isp_retry_speeds[(idx)])

/* Switch off the hardware SPI unit and clear its pending interrupt flag. */
static inline void spiHWdisable(void) {
    SPCR &= ~((1 << SPE) | (1 << MSTR));
    (void)SPSR;  /* reading SPSR ... */
    (void)SPDR;  /* ... then SPDR resets the SPIF flag */
}

/* ------------------------------------------------------------------ */
/* Speed configuration                                                 */
/* ------------------------------------------------------------------ */

void ispSetSCKOption(uint8_t option) {
    /* resolve AUTO: reuse the last speed that worked, else start fast */
    if (option == USBASP_ISP_SCK_AUTO) {
        option = (last_success_speed != USBASP_ISP_SCK_AUTO &&
                  last_success_speed != 0) ? last_success_speed : USBASP_ISP_SCK_3000;
    }

    /* range check: only options 1..13 are valid */
    if (option < USBASP_ISP_SCK_0_5 || option > USBASP_ISP_SCK_3000) {
        option = USBASP_ISP_SCK_3000;
    }

    prog_sck = option;

    if (option >= USBASP_ISP_SCK_93_75) {
        /* hardware SPI (options 8..13) */
        ispTransmit = ispTransmit_hw;
        sck_sw_delay = 0;

        uint8_t idx = USBASP_ISP_SCK_3000 - option;

        SPCR = SPI_BASE | pgm_read_byte(&hw_spcr_table[idx]);

        /* read-modify-write: clear SPI2X while leaving SPIF/WCOL untouched */
        SPSR = (SPSR & ~(1 << SPI2X)) | pgm_read_byte(&hw_spsr_table[idx]);

        /* clear a possibly pending SPIF flag */
        (void)SPSR;
        (void)SPDR;
    } else {
        /* software SPI (options 1..7) */
        ispTransmit = ispTransmit_sw;
        SPCR = 0;   /* make sure the HW unit releases SCK/MOSI/MISO */

        uint8_t idx = USBASP_ISP_SCK_32 - option;   /* always 0..6 here */
        sck_sw_delay = pgm_read_byte(&sw_delay_table[idx]);
    }
}

/* Wait one SCK half period (software SPI bit timing). */
void ispDelay(void) {
    uint8_t starttime = TIMERVALUE;
    while ((uint8_t)(TIMERVALUE - starttime) < sck_sw_delay) {
    }
}

/* ------------------------------------------------------------------ */
/* Connection handling                                                 */
/* ------------------------------------------------------------------ */

static inline void ispInitPins(void) {
    ISP_DDR |= (1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI);
    ISP_DDR &= ~(1 << ISP_MISO);
    isp_hiaddr = 0xFF;
}

void ispResetState(void) {
    prog_address_newmode = 0;
    prog_address_high    = 0;
    prog_pagesize        = 0;
    isp_hiaddr           = 0xFF;  /* force the first load-ext-address (0x4D) */
}

void ispConnect(void) {
    ispInitPins();
    ispResetState();

    /* classic AVR power-on reset sequence: toggle RST with >=100 ms waits */
    ISP_OUT &= ~(1 << ISP_RST);
    ISP_OUT &= ~(1 << ISP_SCK);
    clockWait(1);
    ISP_OUT |= (1 << ISP_RST);
    clockWait(1);
    ISP_OUT &= ~(1 << ISP_RST);
}

void ispSPIConnect(void) {
    ispInitPins();
    CS_HI();
}

void ispDisconnect(void) {
    ispResetState();

    ISP_OUT |= (1 << ISP_RST);
    clockWait(1);

    /* release all ISP pins: inputs, pullups off */
    ISP_DDR &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));
    ISP_OUT &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));

    SPCR &= ~((1 << SPE) | (1 << MSTR));  /* disable HW SPI ... */
    (void)SPSR;                           /* ... and clear its interrupt flags */
    (void)SPDR;

    /* make sure the shared I2C bus lines are released as well */
    i2c_stop();

    prog_sck = USBASP_ISP_SCK_AUTO;
    prog_state = PROG_STATE_IDLE;
    prog_pagecounter = 0;
    prog_nbytes = 0;
}

/* ------------------------------------------------------------------ */
/* Byte transmission                                                   */
/* ------------------------------------------------------------------ */

uint8_t ispTransmit_sw(uint8_t send_byte) {
    uint8_t rec_byte = 0;

    ISP_OUT &= ~(1 << ISP_SCK);
    for (uint8_t bit = 8; bit; --bit) {
        if (send_byte & 0x80)
            ISP_OUT |=  (1 << ISP_MOSI);
        else
            ISP_OUT &= ~(1 << ISP_MOSI);
        send_byte <<= 1;

        ispDelay();
        ISP_OUT |= (1 << ISP_SCK);          /* rising edge: target samples MOSI */
        ispDelay();
        rec_byte = (rec_byte << 1) | ((ISP_IN >> ISP_MISO) & 1);
        ISP_OUT &= ~(1 << ISP_SCK);         /* falling edge: target updates MISO */
        ispDelay();
    }
    return rec_byte;
}

uint8_t ispTransmit_hw(uint8_t send_byte) {
    SPDR = send_byte;
    while (!(SPSR & (1 << SPIF))) {
    }
    return SPDR;
}

/* ------------------------------------------------------------------ */
/* Entering programming mode                                           */
/* ------------------------------------------------------------------ */

/* One full attempt at the given speed: chip erase pulse + sign-on.
 * Returns 0 on success. After the 0x53 echo we additionally read
 * signature byte 0 (always 0x1E on AVR); a wrong value means the line
 * is too noisy at this speed, so the attempt is treated as failure. */
static uint8_t tryEnterProgMode(uint8_t speed_code) {
    ispSetSCKOption(speed_code);

    /* RST pulse length: short for fast speeds, long for slow ones;
     * then wait ~32 ms (datasheet requires >= 20 ms) */
    uint8_t pulse = (speed_code <= USBASP_ISP_SCK_32) ? 16 : 2;

    for (uint8_t tries = 5; tries > 0; tries--) {
        ISP_OUT |= (1 << ISP_RST);
        clockWait(pulse);
        ISP_OUT &= ~(1 << ISP_RST);
        clockWait(100);

        isp_hiaddr = 0xFF;

        /* sign-on: 0xAC 0x53 xx xx -> echo expected in third byte */
        ispTransmit(0xAC);
        ispTransmit(0x53);
        uint8_t check = ispTransmit(0x00);
        ispTransmit(0x00);

        if (check == 0x53) {
            /* verify link quality via signature byte 0 */
            ispTransmit(0x30);
            ispTransmit(0x00);
            ispTransmit(0x00);
            uint8_t sig = ispTransmit(0x00);

            if (sig == 0x1E)
                return 0;   /* stable link */
            /* else: current speed too high for this cable - keep trying */
        }

        clockWait(3);
    }
    return 1;   /* all tries at this speed failed */
}

uint8_t ispEnterProgrammingMode(void) {
    uint8_t start_idx = 0;
    uint8_t initial_speed = USBASP_ISP_SCK_AUTO;

    /* 1. priority speed: explicit user request, else the cached one */
    if (user_speed_requested) {
        user_speed_requested = 0;
        initial_speed = prog_sck;
    } else if (last_success_speed != USBASP_ISP_SCK_AUTO) {
        initial_speed = last_success_speed;
    }

    /* 2. try the priority speed first */
    if (initial_speed != USBASP_ISP_SCK_AUTO) {
        if (tryEnterProgMode(initial_speed) == 0) {
            prog_sck = initial_speed;
            last_success_speed = initial_speed;
            return 0;
        }

        /* failed: find where to resume the descending scan */
        for (uint8_t i = 0; i < ISP_SPEED_CNT; i++) {
            uint8_t speed = GET_SPEED(i);
            if (speed == initial_speed) {
                start_idx = i + 1;      /* continue one step slower */
                break;
            } else if (speed < initial_speed) {
                start_idx = i;          /* requested speed not in table:
                                         * start at first slower entry */
                break;
            }
        }

        /* invalidate the cache after a failure */
        if (initial_speed == last_success_speed)
            last_success_speed = USBASP_ISP_SCK_AUTO;
    }

    /* 3. walk the whole table down to the slowest speed */
    for (uint8_t i = start_idx; i < ISP_SPEED_CNT; i++) {
        if (tryEnterProgMode(GET_SPEED(i)) == 0) {
            last_success_speed = GET_SPEED(i);
            return 0;
        }
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Memory access                                                       */
/* ------------------------------------------------------------------ */

/* Load extended address byte (bit A17) for chips with >64K flash.
 * Only active in "new addressing" mode and only when the page changed. */
void ispUpdateExtended(uint8_t ext_addr) {
    if (!prog_address_newmode)
        return;

    if (ext_addr != isp_hiaddr) {
        isp_hiaddr = ext_addr;
        ispTransmit(0x4D);        /* load extended address command */
        ispTransmit(0x00);
        ispTransmit(ext_addr);
        ispTransmit(0x00);
    }
}

uint8_t ispReadFlash(uint32_t address) {
    uint8_t *p = (uint8_t *)&address;

    ispTransmit(0x20 | ((p[0] & 1) << 3));              /* even/odd byte select */
    ispTransmit((p[1] >> 1) | ((p[2] & 1) << 7));       /* A15..A1 + A16 */
    ispTransmit((p[0] >> 1) | ((p[1] & 1) << 7));       /* A7..A0 */
    return ispTransmit(0);
}

uint8_t ispWriteFlash(uint32_t address, uint8_t data, uint8_t pollmode) {
    uint8_t *p = (uint8_t *)&address;

    ispTransmit(0x40 | ((p[0] & 1) << 3));
    ispTransmit((p[1] >> 1) | ((p[2] & 1) << 7));
    ispTransmit((p[0] >> 1) | ((p[1] & 1) << 7));
    ispTransmit(data);

    if (!pollmode)
        return 0;   /* paged mode: data just goes into the page buffer */

    /* polled mode (legacy chips): verify with up to ~6.4 ms of polling */
    if (data != 0xFF) {
        for (uint8_t t = 0; t < 20; t++) {
            if (ispReadFlash(address) == data)
                return 0;
            clockWait(1);
        }
    }
    clockWait(15);  /* blind 4.8 ms wait for erasing 0xFF bytes */
    return 0;
}

/* Write out the page buffer to flash (page-mode write, 0x4C).
 * The address is rounded down to the page boundary first.
 * NOTE: prog_pagesize is kept in BYTES (setupTransfer converts avrdude's
 * word count once), so the mask can be applied directly. */
uint8_t ispFlushPage(uint32_t address) {
    uint8_t *p = (uint8_t *)&address;

    if (prog_pagesize > 0) {
        uint16_t mask = ~(uint16_t)(prog_pagesize - 1);
        p[0] &= (uint8_t)mask;
        p[1] &= (uint8_t)(mask >> 8);
    }

    ispTransmit(0x4C);
    ispTransmit((p[1] >> 1) | ((p[2] & 1) << 7));   /* includes bit A17 */
    ispTransmit((p[0] >> 1) | ((p[1] & 1) << 7));
    ispTransmit(0);

    clockWait(15);      /* page write takes max 4.5 ms */
    return 0;
}

uint8_t ispReadEEPROM(uint16_t address) {
    ispTransmit(0xA0);
    ispTransmit((uint8_t)(address >> 8));
    ispTransmit((uint8_t)address);
    return ispTransmit(0);
}

uint8_t ispWriteEEPROM(uint16_t address, uint8_t data) {
    ispTransmit(0xC0);
    ispTransmit((uint8_t)(address >> 8));
    ispTransmit((uint8_t)address);
    ispTransmit(data);

    /* poll until self-timed write completes (~8 ms budget) */
    for (uint8_t t = 25; t; --t) {
        clockWait(1);
        if (ispReadEEPROM(address) == data)
            return 0;
    }
    return 1;   /* timeout */
}
