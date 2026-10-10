#include <avr/io.h>
#include "clock.h"
#include "i2c.h"
#include "isp.h"

// Удалены stdio.h и stdlib.h — они не нужны и жрут флеш!

#define I2C_SDA_PIN           ISP_MISO
#define I2C_SCL_PIN           ISP_MOSI

#define I2C_SDA_PORT_READ     ISP_IN
#define I2C_SCL_PORT_READ     ISP_IN
#define I2C_SDA_PORT_DIR      ISP_DDR
#define I2C_SCL_PORT_DIR      ISP_DDR
#define I2C_SDA_PORT          ISP_OUT
#define I2C_SCL_PORT          ISP_OUT

// Макросы для работы с битами (компактнее, чем функции)
#define SET(reg, bit) ((reg) |= (1U << (bit)))
#define CLR(reg, bit) ((reg) &= ~(1U << (bit)))
#define GETBIT(byte, bit) (((byte) >> (bit)) & 1)

// Эмуляция Open-Drain: притягиваем к земле выходом, отпускаем переводом в Hi-Z (вход)
#define I2C_SDA_LOW()  do { SET(I2C_SDA_PORT_DIR, I2C_SDA_PIN); CLR(I2C_SDA_PORT, I2C_SDA_PIN); } while (0)
#define I2C_SDA_HIGH() do { CLR(I2C_SDA_PORT_DIR, I2C_SDA_PIN); SET(I2C_SDA_PORT, I2C_SDA_PIN); } while (0)
#define I2C_SCL_LOW()  do { SET(I2C_SCL_PORT_DIR, I2C_SCL_PIN); CLR(I2C_SCL_PORT, I2C_SCL_PIN); } while (0)
#define I2C_SCL_HIGH() do { CLR(I2C_SCL_PORT_DIR, I2C_SCL_PIN); SET(I2C_SCL_PORT, I2C_SCL_PIN); } while (0)

#define I2C_SDA_VALUE GETBIT(I2C_SDA_PORT_READ, I2C_SDA_PIN)

// Прямые макросы задержек. Компилятор видит константу и генерит точные NOP циклы
// Не оборачиваем _delay_us в функции!
// Универсальный макрос микрозадержки без util/delay.h
// Вычисляется на этапе компиляции. (F_CPU / 1000000) дает количество тактов на 1 мкс.
#define I2C_DELAY_US(us) __builtin_avr_delay_cycles((F_CPU / 1000000UL) * (us))

// Для 100 кГц (Standard Mode)
#define I2C_DELAY_NORM   I2C_DELAY_US(4)

// Для 400 кГц (Fast Mode)
#define I2C_DELAY_FAST   I2C_DELAY_US(2)

/*
// Компактные макросы задержек на чистом ассемблере (для 12 МГц)
// Цикл dec/brne занимает 3 такта. 1 такт = 83.3 наносекунды.

// 4 мкс = 48 тактов. 16 итераций * 3 такта = 48 тактов.
#define I2C_DELAY_NORM __asm__ __volatile__ ( "ldi r24, 16 \n 1: dec r24 \n brne 1b" ::: "r24" )

// 2 мкс = 24 такта. 8 итераций * 3 такта = 24 такта.
#define I2C_DELAY_FAST __asm__ __volatile__ ( "ldi r24, 8 \n 1: dec r24 \n brne 1b" ::: "r24" )
*/

void i2c_init() {
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
}

void i2c_start() {
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
    I2C_DELAY_NORM;
    I2C_SDA_LOW();
    I2C_DELAY_NORM;
    I2C_SCL_LOW();
    I2C_DELAY_NORM;
}

void i2c_start_rep() {
    I2C_SCL_LOW();
    I2C_DELAY_FAST;
    I2C_SDA_HIGH();
    I2C_DELAY_FAST;
    I2C_SCL_HIGH();
    I2C_DELAY_FAST;
    I2C_SDA_LOW();
    I2C_DELAY_FAST;
    I2C_SCL_LOW();
    I2C_DELAY_FAST;
}

void i2c_stop() {
    I2C_SCL_LOW();
    I2C_SDA_LOW();
    I2C_DELAY_FAST;
    I2C_SCL_HIGH();
    I2C_DELAY_FAST;
    I2C_SDA_HIGH();
    I2C_DELAY_NORM;
}

unsigned char i2c_send_byte(unsigned char byte) {
    for (unsigned char i = 0; i < 8; i++) {
        I2C_SCL_LOW();
        I2C_DELAY_FAST;
        
        // Ветвление заменено на более понятное компилятору
        if (byte & 0x80) {
            I2C_SDA_HIGH();
        } else {
            I2C_SDA_LOW();
        }
        
        I2C_DELAY_FAST;
        I2C_SCL_HIGH();
        I2C_DELAY_NORM;
        byte <<= 1;
    }
    
    // Чтение ACK
    I2C_SCL_LOW();
    I2C_SDA_HIGH(); // Отпускаем SDA, чтобы ведомый мог ответить
    I2C_DELAY_FAST;
    I2C_SCL_HIGH();
    I2C_DELAY_NORM;
    
    unsigned char ack = !I2C_SDA_VALUE; // 0 - NACK, 1 - ACK
    
    I2C_SCL_LOW();
    return ack;
}

uint8_t i2c_read_byte(uint8_t ack) {
    uint8_t result = 0;
    I2C_SDA_HIGH(); // Отпускаем SDA
    
    for (uint8_t i = 0; i < 8; i++) {
        I2C_SCL_LOW();
        I2C_DELAY_NORM;
        I2C_SCL_HIGH();
        I2C_DELAY_NORM;
        
        result <<= 1;
        // Чтение бита напрямую в результат (компактнее)
        if (I2C_SDA_VALUE) {
            result |= 1;
        }
    }
    
    // Отправка ACK/NACK
    I2C_SCL_LOW();
    if (ack == I2C_ACK) {
        I2C_SDA_LOW(); // ACK = притянуть к земле
    } else {
        I2C_SDA_HIGH(); // NACK = отпустить
    }
    I2C_DELAY_FAST;
    I2C_SCL_HIGH();
    I2C_DELAY_FAST;
    I2C_SCL_LOW();
    I2C_SDA_HIGH(); // Обязательно отпускаем SDA после ACK
    
    return result;
}

unsigned char i2c_address(unsigned char address, unsigned char rw) {
    address <<= 1;
    if (rw == I2C_READ) {
        address |= 0x01;
    }
    return i2c_send_byte(address);
}