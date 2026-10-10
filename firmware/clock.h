/*
 * clock.h - part of USBasp
 *
 * Autor..........: Thomas Fischl <tfischl@gmx.de>
 * Description....: Provides functions for timing/waiting
 * Licence........: GNU GPL v2 (see Readme.txt)
 * Creation Date..: 2005-02-23
 * Last change....: 2025-11-10
 */

#ifndef __clock_h_included__
#define	__clock_h_included__

#ifndef F_CPU /* should be defined in Makefile */
#define F_CPU           12000000UL   /* 12MHz */
//#define F_CPU           16000000UL   /* 16MHz */
#endif

#include <avr/io.h>

// Автоматический расчет тиков для 320 мкс при предделителе 64
// Для 16 МГц: (16000000 / 64) * 0.000320 = 80
// Для 12 МГц: (12000000 / 64) * 0.000320 = 60
#define CLOCK_T_320us	((F_CPU / 64) * 320 / 1000000)

// Универсальное имя для регистра управления таймером
#if defined(__AVR_ATmega8__) || defined(__AVR_ATmega8A__) || defined(__AVR_ATmega48__) || defined(__AVR_ATmega88__) || defined(__AVR_ATmega168__)
  #define CLOCK_TCCR TCCR0
#else
  // Для ATmega328p и других новых чипов
  #define CLOCK_TCCR TCCR0B
#endif

#define TIMERVALUE      TCNT0

/* set prescaler to 64: CS01=1, CS00=1 */
#define clockInit()  (CLOCK_TCCR = (1 << CS01) | (1 << CS00))

/* wait time * 320 us */
void clockWait(uint8_t time);

#endif /* __clock_h_included__ */