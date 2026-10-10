/*
 * isp.h - part of USBasp
 *
 * Autor..........: Thomas Fischl <tfischl@gmx.de>
 * Description....: Provides functions for communication/programming
 *                  over ISP interface
 * Licence........: GNU GPL v2 (see Readme.txt)
 * Creation Date..: 2005-02-23
 * Last change....: 2009-02-28
 */

#ifndef ISP_H
#define ISP_H

#ifndef __isp_h_included__
#define	__isp_h_included__

#ifndef uchar
#define	uchar	unsigned char
#endif

#define	ISP_OUT   PORTB
#define ISP_IN    PINB
#define ISP_DDR   DDRB
#define ISP_RST   PB2
#define ISP_MOSI  PB3
#define ISP_MISO  PB4
#define ISP_SCK   PB5

// Для поддержки всех AVR нужно правильно определить:
#define FLASH_MAX_BYTES   (512UL*1024)    // Максимум для USBasp
#define MAX_EEPROM_SIZE   (64UL * 1024)   // 64KB - максимум 16-битного адреса
#define EXTADDR_BLOCK     (0x20000UL)     // 0x20000UL=128K граница и для ATmega2560 (256KB = 0x40000).
#define EXTADDR_BLOCKS    (FLASH_MAX_BYTES / EXTADDR_BLOCK)

#define CS_LOW()	ISP_OUT &= ~(1 << ISP_RST); /* RST low */
#define CS_HI()		ISP_OUT |= (1 << ISP_RST); /* RST high */

extern uint8_t (*ispTransmit)(uint8_t);

//extern uint16_t prog_pagesize;
extern uint8_t  prog_state;
extern uint16_t prog_pagecounter;
extern uint16_t prog_nbytes;
extern uint8_t  prog_sck;
extern uint8_t  user_speed_requested;
extern volatile uint8_t prog_address_newmode;
extern uint8_t  prog_address_high;
extern uint16_t prog_pagesize;

/* Prepare connection to target device */
void ispConnect(void);

void ispSPIConnect(void);

/* Close connection to target device */
void ispDisconnect(void);

void ispDelay(void);

/* read an write a byte from isp using software (slow) */
uint8_t ispTransmit_sw(uint8_t send_byte);

/* read an write a byte from isp using hardware (fast) */
uint8_t ispTransmit_hw(uint8_t send_byte);

/* enter programming mode */
uint8_t ispEnterProgrammingMode(void);

/* write byte to flash at given address */
uint8_t ispWriteFlash(uint32_t address, uint8_t data, uint8_t pollmode);

uint8_t ispFlushPage(uint32_t address);

/* read byte from flash at given address */
uint8_t ispReadFlash(uint32_t address);

/* read byte from eeprom at given address */
uint8_t ispReadEEPROM(uint16_t address);

/* write byte to eeprom at given address */
uint8_t ispWriteEEPROM(uint16_t address, uint8_t data);

/* set SCK speed. call before ispConnect! */
void ispSetSCKOption(uint8_t option);

void ispUpdateExtended(uint8_t ext_addr);

#endif /* __isp_h_included__ */

#endif