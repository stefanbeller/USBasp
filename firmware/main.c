/*
 * USBasp - USB in-circuit programmer for Atmel AVR controllers
 *
 * Thomas Fischl <tfischl@gmx.de>
 *
 * License........: GNU GPL v2 (see Readme.txt)
 * Target.........: ATMega8 at 12 MHz
 * Creation Date..: 2005-02-20
 * Last change....: 2009-02-28
 *
 * PC2 SCK speed option.
 * GND  -> slow (8khz SCK),
 * open -> software set speed (default is 375kHz SCK)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "usbasp.h"
#include "usbdrv.h"
#include "isp.h"
#include "clock.h"
#include "tpi.h"
#include "tpi_defs.h"
#include "i2c.h"
#include "microwire.h"

// --- Перемещаем ОПРЕДЕЛЕНИЯ переменных ВВЕРХ ---
static uint8_t replyBuffer[8] = {0};
uint8_t prog_state = PROG_STATE_IDLE;
uint8_t prog_sck = USBASP_ISP_SCK_AUTO;
static uint8_t requested_sck = USBASP_ISP_SCK_AUTO;   // raw-запрос хоста, до резолва

uint32_t prog_address = 0;
uint8_t prog_address_high = 0; // Биты 16-23 адреса (совместимость со старыми avrdude) 
volatile uint8_t prog_address_newmode = 0; // Режим "нового" адреса (3 байта вместо 2)

// Управление многостраничной записью Flash (критично для split-writes) 
uint16_t prog_pagesize = 0;
uint16_t prog_pagecounter = 0;
uint16_t prog_nbytes = 0;  // Остаток байт в ТЕКУЩЕМ USB-чанке

// Универсальные счетчики протокола
static uint8_t spi_cs_hi = 1;       // Флаг управления CS для SPI-транзакций
static uint8_t mw_cs_lo = 1;       // Флаг управления CS (хотя в UsbAsp-flash CS всегда управляется прошивкой)
static uint8_t mw_bits_remaining = 0;
uint8_t user_speed_requested = 0;    // Флаг: юзер принудительно задал SCK?
static uint8_t i2c_stop_aw = 0;  // Контроль потока I2C (чтобы знать, слать STOP после блока или нет)

// Объявления функций
extern uint8_t sck_sw_delay;
extern void tpi_init(void);
extern void tpi_send_byte(uint8_t data);
extern uint8_t tpi_recv_byte(void);
extern void tpi_pr_update(uint16_t addr);
extern uint8_t last_success_speed;
extern uint8_t isp_hiaddr;
extern uint8_t tpi_parity_err;

/* Макрос для быстрой проверки минимального значения */
//#define MIN(a, b) (((a) < (b)) ? (a) : (b))
// Оптимизированная функция вместо макроса MIN
static inline uint8_t min_u8_u16(uint8_t a, uint16_t b) {
    return (b < a) ? (uint8_t)b : a;
}

/* Байт расширенного адреса (A23..A16) из 32-битного адреса.
 * ispUpdateExtended() принимает его уже сдвинутым на 1 (бит A17 -> LSB). */
#define EXT_ADDR_BYTE(addr) (((const uint8_t *)&(addr))[2])

/* Завершить текущий многостраничный/пакетный трансфер. */
static inline void endTransfer(void) {
    prog_state = PROG_STATE_IDLE;
    prog_nbytes = 0;
}

static inline uint8_t is_power_of_two16(uint16_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

/* -------------------------------------------------------------------------------- */
/* * Протокол USBasp передает метаданные в полях SETUP-пакета: 
   * data[4:5] -> wIndex: ПОЛНЫЙ ожидаемый размер транзакции (или 0 для совместимости). 
   * data[6:7] -> wLength: реальный размер полезных данных в ЭТОМ конкретном USB-пакете (макс 255/64 байта). 
*/
static void setupTransfer(uint8_t *data, uint8_t new_state) {
    uint8_t *p_addr = (uint8_t*)&prog_address;
    p_addr[0] = data[2];
    p_addr[1] = data[3];
    
    if (prog_address_newmode) {
        p_addr[2] = prog_address_high;
        p_addr[3] = 0;
    } else {
        p_addr[2] = 0;
        p_addr[3] = 0;
    }
    
    uint16_t wlength = ((uint16_t)data[7] << 8) | data[6];
    prog_nbytes = wlength;
    
    // Инициализация страничной записи
    if (new_state == PROG_STATE_WRITEFLASH) {
        uint16_t windex = ((uint16_t)data[5] << 8) | data[4];
        prog_pagesize = windex; // avrdude передает размер страницы в СЛОВАХ в wIndex

        // ЗАЩИТА ОТ КРИВОГО РАЗМЕРА СТРАНИЦЫ.
        // prog_pagecounter считается в БАЙТАХ: одна страница = 2 * words.
        // Любое некорректное значение (0, переполнение, не степень двойки)
        // включает безопасный побайтовый режим записи.
        uint16_t page_bytes = (uint16_t)(windex << 1);
        if (is_power_of_two16(windex) && page_bytes > windex) {
            uint16_t low_addr = ((uint16_t)data[3] << 8) | data[2];
            prog_pagesize = page_bytes;   // байт-размер страницы для ispFlushPage/счётчика
            prog_pagecounter = prog_pagesize - (low_addr & (prog_pagesize - 1));
        } else {
            prog_pagesize = 0;
            prog_pagecounter = 0;
        }
    }
    
    prog_state = new_state;
}

static void setupSPIState(uint8_t mode, uint8_t *data) {
    spi_cs_hi = data[2]; // Сначала читаем флаг!
    CS_LOW();            // Потом дергаем ножку
    prog_nbytes = (data[7] << 8) | data[6];
    prog_state = mode;
}

static uint8_t tpi_waitbusy(void) {
    uint8_t t, csr;
    for (t = 0; t < 50; t++) {              // 50·320мс? нет: 50 итераций, ~16мс с clockWait
        tpi_send_byte(TPI_OP_SIN(NVMCSR));
        csr = tpi_recv_byte();
        if (tpi_parity_err) return 1;
        if (!(csr & NVMCSR_BSY)) return 0;
        clockWait(1);
    }
    return 2;
}

void tpi_read_block_c(uint16_t addr, uint8_t *buf, uint16_t len) {
    tpi_pr_update(addr);
    while (len--) {
        tpi_send_byte(TPI_OP_SLD_INC);
        *buf = tpi_recv_byte();
        if (tpi_parity_err) {               // битый байт не уедет хосту молча
            endTransfer();
            return;
        }
        buf++;
    }
}

void tpi_write_block_c(uint16_t addr, uint8_t *buf, uint16_t len) {
    tpi_pr_update(addr);
    while (len--) {
        tpi_send_byte(TPI_OP_SOUT(NVMCMD));
        tpi_send_byte(NVMCMD_WORD_WRITE);
        tpi_send_byte(TPI_OP_SST_INC);
        tpi_send_byte(*buf++);
        if (tpi_waitbusy() != 0) {
            endTransfer();                  // короткий трансфер → avrdude увидит ошибку
            return;
        }
    }
}

/* -------------------------------------------------------------------------------- */
usbMsgLen_t usbFunctionSetup(uint8_t data[8]) {

 usbMsgLen_t len = 0;
        

	if (data[1] == USBASP_FUNC_CONNECT) {
        	ispSetSCKOption(prog_sck);
        	prog_address_newmode = 0;
        	ledRedOn();
        	ispConnect();
        	replyBuffer[0] = ispEnterProgrammingMode();
        	len = 1;

	} else if (data[1] == USBASP_FUNC_DISCONNECT) {
		ispDisconnect();
		ledGreenOff();

//------------------------------------------------------------------------------------------

	} else if (data[1] == USBASP_FUNC_TRANSMIT) {
		replyBuffer[0] = ispTransmit(data[2]);
		replyBuffer[1] = ispTransmit(data[3]);
		replyBuffer[2] = ispTransmit(data[4]);
		replyBuffer[3] = ispTransmit(data[5]);
		len = 4;

	} else if (data[1] == USBASP_FUNC_ENABLEPROG) {
        	replyBuffer[0] = ispEnterProgrammingMode();
		len = 1;

	} else if (data[1] == USBASP_FUNC_READFLASH) {
            	setupTransfer(data, PROG_STATE_READFLASH);
            	len = USB_NO_MSG;

    	} else if (data[1] == USBASP_FUNC_READEEPROM) {
            	setupTransfer(data, PROG_STATE_READEEPROM);
            	len = USB_NO_MSG;

        } else if (data[1] == USBASP_FUNC_WRITEFLASH) {
            	setupTransfer(data, PROG_STATE_WRITEFLASH);
            	len = USB_NO_MSG;

        } else if (data[1] == USBASP_FUNC_WRITEEEPROM) {
            	setupTransfer(data, PROG_STATE_WRITEEEPROM);
            	len = USB_NO_MSG;

	} else if (data[1] == USBASP_FUNC_SETLONGADDRESS) {
                prog_address_newmode = 1;

                // 1. СОХРАНЯЕМ БЕЗ СДВИГА!
                // Это биты 16..23 чистого 32-битного адреса.
                // Если сдвинуть, сломается математика prog_address.
                prog_address_high = data[4];  
                
                // 2. ОТПРАВЛЯЕМ В ЧИП СО СДВИГОМ!
                // Вытаскиваем бит A17 (бит 17) и кладем его в бит 0,
                // как того требует даташит на AVR (команда 0x4D).
            	ispUpdateExtended(data[4] >> 1);   

                replyBuffer[0] = 0;
                len = 1;
    
    	 } else if (data[1] == USBASP_FUNC_SETISPSCK) {
    		prog_state = PROG_STATE_IDLE;

    		if (data[2] == USBASP_ISP_SCK_AUTO ||
        	   (data[2] >= USBASP_ISP_SCK_0_5 && data[2] <= USBASP_ISP_SCK_3000)) {
        		prog_sck = data[2];
		        requested_sck = prog_sck;
		        user_speed_requested = (data[2] != USBASP_ISP_SCK_AUTO);
		        replyBuffer[0] = 0;                      // успех
		    } else {
		        prog_sck = USBASP_ISP_SCK_AUTO;
		        requested_sck = prog_sck;
		        user_speed_requested = 0;
		        replyBuffer[0] = 1;                      // ошибка → хост покажет "cannot set sck period"
		    }
		len = 1;

	} else if (data[1] == USBASP_FUNC_GETISPSCK) {
    		replyBuffer[0] = 0;
    		replyBuffer[1] = prog_sck;           // текущая установленная скорость
    		replyBuffer[2] = last_success_speed; // предыдущая успешная скорость
    		replyBuffer[3] = sck_sw_delay;
    		replyBuffer[4] = requested_sck;
    		replyBuffer[5] = prog_state;
    		len = 6;


	} else if (data[1] == USBASP_FUNC_GETCAPABILITIES) {
		replyBuffer[0] = USBASP_CAP_0_TPI | USBASP_CAP_0_I2C | USBASP_CAP_0_MW;
		replyBuffer[1] = USBASP_CAP_1_SCK_AUTO | USBASP_CAP_1_HW_SCK;
		replyBuffer[2] = 0;
		replyBuffer[3] = USBASP_CAP_3_FLASH | USBASP_CAP_3_EEPROM |
                 		 USBASP_CAP_3_FUSES | USBASP_CAP_3_LOCKBITS |
                 		 USBASP_CAP_3_EXTENDED_ADDR | USBASP_CAP_3MHZ;          // 0x40 – никаких сдвигов    
    		len = 4;
								
//spi ----------------------------------------------------------------------------------------

	} else if (data[1] == USBASP_FUNC_SPI_CONNECT) {
		ispSetSCKOption(prog_sck);
		ledRedOn();
		ispSPIConnect();
			
	} else if (data[1] == USBASP_FUNC_SPI_READ) {
    		setupSPIState(PROG_STATE_SPI_READ, data);
    		len = USB_NO_MSG;

	} else if (data[1] == USBASP_FUNC_SPI_WRITE) {
    		setupSPIState(PROG_STATE_SPI_WRITE, data);
    		len = USB_NO_MSG;
    	
//i2c 24xx ------------------------------------------------------------------------------------

    	} else if (data[1] == USBASP_FUNC_I2C_INIT) {
        	ledRedOn();
            	i2c_init();
    
    	} else if (data[1] == USBASP_FUNC_I2C_START) {
            	i2c_start();
    
    	} else if (data[1] == USBASP_FUNC_I2C_STOP) {
            	i2c_stop();
    
    	} else if (data[1] == USBASP_FUNC_I2C_WRITE_BYTE) {
            	replyBuffer[0] = i2c_send_byte(data[2]);
            	len = 1;
    
    	} else if (data[1] == USBASP_FUNC_I2C_READ_BYTE) {
            	replyBuffer[0] = i2c_read_byte(data[2]);
            	len = 1;
    
	} else if (data[1] == USBASP_FUNC_I2C_READ) {
            // ПК не шлет I2C_START перед чтением, прошивка делает всё сама
            i2c_start();
            i2c_address(data[2], I2C_READ);
            prog_nbytes = (data[7] << 8) | data[6];
            prog_state = PROG_STATE_I2C_READ;
            len = USB_NO_MSG;

        } else if (data[1] == USBASP_FUNC_I2C_WRITE) {
            i2c_start();
            i2c_address(data[2], I2C_WRITE); // Адрес устройства
            // data[4] = Команда стоп(1) или старт(0)
            i2c_stop_aw = data[4] & 0x01;
            prog_nbytes = (data[7] << 8) | data[6];
            prog_state = PROG_STATE_I2C_WRITE;
            len = USB_NO_MSG;
        
        
//microwire 93xx ---------------------------------------------------------------------------------------------

	} else if (data[1] == USBASP_FUNC_MW_WRITE) {
                CS_HI(); // Включаем CS (RST = 1)
                mw_cs_lo = data[2];
                
                // data[4] (wIndex low) = сколько бит передавать в ЭТОМ чанке
                mw_bits_remaining = data[4]; 
                
                prog_nbytes = (data[7] << 8) | data[6]; // Размер куска данных
                prog_state = PROG_STATE_MW_WRITE;
                len = USB_NO_MSG;

        } else if (data[1] == USBASP_FUNC_MW_READ) {
                mw_cs_lo = data[2];
                mw_bits_remaining = 0; // СБРАСЫВАЕМ ФЛАГ
                prog_nbytes = (data[7] << 8) | data[6];
                prog_state = PROG_STATE_MW_READ;
                len = USB_NO_MSG;

        } else if (data[1] == USBASP_FUNC_MW_BUSY) {
                replyBuffer[0] = mwBusy() ? 1 : 0;
                len = 1;
    
//TPI ------------------------------------------------------------------------------------------

	} else if (data[1] == USBASP_FUNC_TPI_CONNECT) {
		tpi_dly_cnt = data[2] | (data[3] << 8);
		
		/* RST high */
		ISP_OUT |= (1 << ISP_RST);
		ISP_DDR |= (1 << ISP_RST);

		clockWait(3);

		/* RST low */
		ISP_OUT &= ~(1 << ISP_RST);
		ledRedOn();

		clockWait(16);
		tpi_init();
	
	} else if (data[1] == USBASP_FUNC_TPI_DISCONNECT) {

		tpi_send_byte(TPI_OP_SSTCS(TPISR));
		tpi_send_byte(0);

		clockWait(10);

		/* pulse RST */
		ISP_OUT |= (1 << ISP_RST);
		clockWait(5);
		ISP_OUT &= ~(1 << ISP_RST);
		clockWait(5);

		/* set all ISP pins inputs */
		ISP_DDR &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));
		/* switch pullups off */
		ISP_OUT &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));

			
	} else if (data[1] == USBASP_FUNC_TPI_RAWREAD) {
		replyBuffer[0] = tpi_recv_byte();
		len = 1;
	
	} else if (data[1] == USBASP_FUNC_TPI_RAWWRITE) {
		tpi_send_byte(data[2]);
	        replyBuffer[0] = 0;          // <-- обязательно!
    		len = 1;

	} else if (data[1] == USBASP_FUNC_TPI_READBLOCK) {
    		setupTransfer(data, PROG_STATE_TPI_READ);
    		len = USB_NO_MSG;
	
	} else if (data[1] == USBASP_FUNC_TPI_WRITEBLOCK) {
		setupTransfer(data, PROG_STATE_TPI_WRITE);
		len = USB_NO_MSG; /* multiple out */

	}
//------------------------------------------------------------------------------------------

	if (len != 0 && len != USB_NO_MSG) {
        usbMsgPtr = replyBuffer;
    	}
    return len;
}

uint8_t usbFunctionRead(uint8_t *data, uint8_t len)
{
    // ЗАЩИТА ОТ НУЛЕВОЙ ДЛИНЫ ПАКЕТА
    if (len == 0) return 0;

    /* Быстрая проверка: если не «читаем» – сразу выход */
    if ((prog_state != PROG_STATE_READFLASH)  &&
        (prog_state != PROG_STATE_READEEPROM) &&
        (prog_state != PROG_STATE_TPI_READ)   &&
        (prog_state != PROG_STATE_SPI_READ)   &&
        (prog_state != PROG_STATE_MW_READ)    &&
        (prog_state != PROG_STATE_I2C_READ)) {
        goto exit_unsupported;
    }

    /* Оптимизация: определяем реальную длину один раз */
    len = min_u8_u16(len, prog_nbytes);
    ledGreenOn();

    switch (prog_state) {

	/* ---------- TPI – быстро отдельно --- */
        case PROG_STATE_TPI_READ:
            tpi_read_block_c(prog_address, data, len); // Используем C-функцию
            prog_address += len;
            break;

        /* ---------- SPI (Чтение) ----------- */
        case PROG_STATE_SPI_READ: {
            uint8_t count = len;
            uint8_t *dst = data;
            do {
                *dst++ = ispTransmit(0);
            } while (--count);
            prog_nbytes -= len;
            if (prog_nbytes == 0 && spi_cs_hi) CS_HI();
            break;
        }

        /* ---------- I2C Read ---------- */
        case PROG_STATE_I2C_READ: {
            uint8_t count = len;
            uint8_t *dst = data;

            do {
                prog_nbytes--; // Сначала уменьшаем глобальный счетчик оставшихся байт

                // Если после декремента prog_nbytes == 0, значит мы читаем САМЫЙ ПОСЛЕДНИЙ байт транзакции
                uint8_t ack = (prog_nbytes == 0) ? I2C_NACK : I2C_ACK;
                *dst++ = i2c_read_byte(ack);
            } while (--count);

            // prog_nbytes уже обновлен внутри цикла! Ничего вычитать не нужно.
            if (prog_nbytes == 0) i2c_stop();
            break;
        }

        /* ---------- MW Read ---------- */
        case PROG_STATE_MW_READ: {
            uint8_t count = len;
            uint8_t *dst = data;

            do {
                *dst++ = mwReadByte();
            } while (--count);
            prog_nbytes -= len;
            if (prog_nbytes == 0 && mw_cs_lo) mwEnd();
            break;
        }

        /* ---------- Чтение READFLASH ---------- */
        case PROG_STATE_READFLASH: {
            uint32_t addr = prog_address;
            uint8_t *dst = data;
            uint8_t count = len;

            // Обязательно обновляем расширенный адрес в начале чанка
            ispUpdateExtended(EXT_ADDR_BYTE(addr) >> 1);

            do {
                *dst++ = ispReadFlash(addr);
                addr++;

                // Переход через границу 64 КБ
                if ((uint16_t)addr == 0) {
                    ispUpdateExtended(EXT_ADDR_BYTE(addr) >> 1);
                }
            } while (--count);

            prog_address = addr;
            prog_nbytes -= len;
            break;
        }

        /* ---------- Чтение EEPROM ---------- */
        case PROG_STATE_READEEPROM: {
            // EEPROM не нуждается в extended адресе (максимум 64К)
            // Используем быстрый 16-битный адрес в цикле
            uint16_t addr = (uint16_t)prog_address;
            uint8_t *dst = data;
            uint8_t count = len; // len уже посчитан как MIN(len, prog_nbytes) в начале функции!

            do {
                *dst++ = ispReadEEPROM(addr++);
            } while (--count);

            prog_address += len; // Обновляем глобальный 32-битный адрес
            prog_nbytes -= len;  // Вычитаем ровно столько, сколько прочитали
            break;
        }

        default:
            goto exit_unsupported;
    }

    /* Единый пост-обработчик: TPI сам аварийно завершил трансфер при
     * ошибке четности (prog_nbytes обнулен) - передаём хосту короткий пакет. */
    if (prog_nbytes == 0) {
        prog_state = PROG_STATE_IDLE;
    }

  exit_unsupported:
    ledGreenOff();
    return 0xFF;

    /* Единый выход успешного чтения */
    ledGreenOff();
    ledRedOn();
    return len;
}

uint8_t usbFunctionWrite(uint8_t *data, uint8_t len)
{ 
    uint8_t retVal = 0; 

        // ЗАЩИТА ОТ НУЛЕВОЙ ДЛИНЫ ПАКЕТА
        if (len == 0) return 0;

	/* быстрая проверка режима */
	if (prog_state == PROG_STATE_IDLE) return 0xFF;
    
		if ((prog_state != PROG_STATE_WRITEFLASH)  &&
	           (prog_state != PROG_STATE_WRITEEEPROM) &&
	           (prog_state != PROG_STATE_TPI_WRITE)   &&
	           (prog_state != PROG_STATE_SPI_WRITE)   &&
	           (prog_state != PROG_STATE_MW_WRITE)    &&
	           (prog_state != PROG_STATE_I2C_WRITE)) return 0xFF;

	    	// ОГРАНИЧИВАЕМ LEN ОДИН РАЗ В НАЧАЛЕ!
	    	len = min_u8_u16(len, prog_nbytes);
	    	ledGreenOn();

    	/* ---------- TPI ---------- */
	if (prog_state == PROG_STATE_TPI_WRITE) {
		tpi_write_block_c(prog_address, data, len);
		prog_address += len;
	        prog_nbytes  -= len;
	       
	        if (prog_nbytes == 0) {
	            prog_state = PROG_STATE_IDLE;
	            retVal = 1; // Транзакция завершена
            	  } else {
                	retVal = 0; // Ждем следующий чанк
            	}
	        goto exit;
	}

	/* ---------- SPI (Запись) ---------- */
	if (prog_state == PROG_STATE_SPI_WRITE) {
	  	uint8_t *src = data;
	        uint8_t count = len; // Используем len напрямую
        
	        do {
	            ispTransmit(*src++);
	        } while (--count);
        
	        prog_nbytes -= len;
	        if (prog_nbytes == 0) {
	            if (spi_cs_hi) CS_HI();
	            prog_state = PROG_STATE_IDLE;
	            retVal = 1; // Транзакция завершена
            	  } else {
                    retVal = 0; // Ждем следующий чанк
            	}
	        goto exit;
	}

	/* ---------- I2C Write ---------- */
    	if (prog_state == PROG_STATE_I2C_WRITE) {
           uint8_t *src = data; 
           uint8_t count = len;
        
           do {
            if (i2c_send_byte(*src++) == 0) {
	        prog_state = PROG_STATE_IDLE;
	        prog_nbytes = 0;
	        retVal = 1;           // прервать трансфер
	        goto exit;
	     }
           } while (--count);
             prog_nbytes -= len;

           if (prog_nbytes == 0) { 
               if(i2c_stop_aw) i2c_stop(); 
               prog_state = PROG_STATE_IDLE; 
               retVal = 1; 
             }
           goto exit;
    	}

	/* ---------- MW Write ---------- */
    	if (prog_state == PROG_STATE_MW_WRITE) {
            uint8_t *src = data;
            uint8_t count = len; 
                        
            do {
                uint8_t bits = (mw_bits_remaining >= 8) ? 8 : mw_bits_remaining;
                if (bits == 0) break;
                
                mwSendData(*src++, bits); // Отправляем N бит из байта
                mw_bits_remaining -= bits;
            } while (--count);
    
            prog_nbytes -= len;
    
            if (prog_nbytes == 0) {
                if (mw_cs_lo) mwEnd(); // Выключаем CS, если попросили
                prog_state = PROG_STATE_IDLE;
                retVal = 1; // Транзакция завершена
              
              } else {
              
                retVal = 0; // Ждем следующий чанк
            }
            goto exit;
        }

	/* ---------- Flash – с extended addressing ---------- */
	if (prog_state == PROG_STATE_WRITEFLASH) {
            uint32_t addr = prog_address;
            uint8_t *src = data;
            uint8_t count = len;
            uint8_t paged = (prog_pagesize != 0);

            // Обновляем расширенный адрес (бит 17)
            ispUpdateExtended(EXT_ADDR_BYTE(addr) >> 1);

            do {
                if (!paged) {
                    // Старый чип (побайтовая запись с ожиданием)
                    ispWriteFlash(addr, *src++, 1);
                } else {
                    // Современный чип (грузим в буфер)
                    ispWriteFlash(addr, *src++, 0);
                    
                    // Дошли до границы страницы? Записываем!
                    if (--prog_pagecounter == 0) {
                        // ИСПРАВЛЕНО: Никаких uint32_t масок в main.c!
                        // Передаем адрес как есть, а ispFlushPage сам его обрежет.
                        ispFlushPage(addr); 
                        prog_pagecounter = prog_pagesize;
                    }
                }
        
                addr++;
        
                // Переход через границу 64 КБ
                if ((uint16_t)addr == 0) {
                    ispUpdateExtended(EXT_ADDR_BYTE(addr) >> 1);
                }

            } while (--count);
    
            // Сохраняем прогресс
            prog_address = addr; 
            prog_nbytes -= len;

            if (prog_nbytes == 0) {
                prog_state = PROG_STATE_IDLE;

                // Запись хвоста (если файл не кратен странице)
                if (prog_pagesize != 0 && prog_pagecounter != prog_pagesize) {
                    // ИСПРАВЛЕНО (Ошибка №5): Обновляем extended адрес перед flush хвоста!
                    ispUpdateExtended(EXT_ADDR_BYTE(prog_address) >> 1);
                    // ИСПРАВЛЕНО: отнимаем 1, чтобы попасть в последнюю записанную страницу!
                    ispFlushPage(prog_address - 1);
                }
                 retVal = 1; // Транзакция завершена
            } else {
                retVal = 0; // Ждем следующий чанк
            }
            goto exit;
        }
        
        /* ---------- EEPROM ---------- */
	if (prog_state == PROG_STATE_WRITEEEPROM) {
	    uint8_t *src = data;
	    uint8_t count = len; // V-USB передает максимум 255 байт, uint8_t достаточно!
        
	        do {
	            if (ispWriteEEPROM((uint16_t)prog_address, *src++) != 0) {
	                prog_state = PROG_STATE_IDLE;
	                retVal = 1;
	                goto exit;
	            }
	            prog_address++;
	        } while (--count);
        
	        prog_nbytes -= len;
	        if (prog_nbytes == 0) {
	            prog_state = PROG_STATE_IDLE;
	            retVal = 1;
	        } else {
	            retVal = 0;
	        }
	        goto exit;
	    }

	    retVal = 0xFF;
 
   exit:
      ledGreenOff();
      ledRedOn();
      return retVal;
}

int main(void) {

    // 1. САМОЕ ПЕРВОЕ ДЕЛО: запускаем таймер! 
    // Это позволит нам использовать clockWait() вместо тяжелой _delay_ms()
    clockInit();

    /* no pullups on USB and ISP pins */
    PORTD = 0;
    PORTB = 0;
    
    /* Output SE0 for USB reset */
    PORTB &= ~((1 << PB1) | (1 << PB0)); // D+ и D- = 0
    DDRB |= (1 << PB1) | (1 << PB0); 	 // выходы, low	
    
    // Ждем 63 мс (200 * 320us = 64 мс)
    clockWait(200);           
    DDRB = 0;                // возвращаем во входы

    /* Инициализация порта C: светодиоды и подтяжки для входов */
    DDRC = (1 << PC0) | (1 << PC1);  
    DDRC &= ~(1 << PC2);
    PORTC |= (1 << PC2) | (1 << PC3) | (1 << PC4) | (1 << PC5);
   
    /* ----------- индикация ----------- */

    ledRedOn();
    // Ждем 127 мс (2 раза по 200 * 320us = 128 мс)
    clockWait(200);
    clockWait(200);
    
    ledRedOff();
    ledGreenOn();  
    clockWait(200);
    clockWait(200);
    
    ledGreenOff();
    ledRedOn();
   
    /* ----------- USB ----------- */
    usbInit();

    sei();

    for (;;) {
        usbPoll();
    }
    return 0;
}

