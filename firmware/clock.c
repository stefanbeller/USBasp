/*
 * clock.c - part of USBasp
 *
 * Autor..........: Thomas Fischl <tfischl@gmx.de>
 * Description....: Provides functions for timing/waiting
 * Licence........: GNU GPL v2 (see Readme.txt)
 * Creation Date..: 2005-02-23
 * Last change....: 2005-04-20
 */

#include "clock.h"

/* wait time * 320 us */
void clockWait(uint8_t time) {
    // Безопасная реализация: не страдает от переполнения 8 бит
    while (time--) {
        uint8_t start = TIMERVALUE;
        while ((uint8_t)(TIMERVALUE - start) < CLOCK_T_320us);
    }
}