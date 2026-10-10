/**
 * TPI driver for USBasp (ATmega8 / ATmega328P)
 * LSB-first, XOR parity, open-drain via DDR toggle
 */

#include <avr/io.h>
#include "tpi_defs.h"

/* ---------- I/O mapping ---------- */
#define TPI_CLK_PORT  PORTB
#define TPI_CLK_DDR   DDRB
#define TPI_CLK_PIN   5

#define TPI_DATA_PORT PORTB
#define TPI_DATA_DDR  DDRB
#define TPI_DATA_PIN  3
#define TPI_DATA_IN   PINB

/* RAM variables */
.comm tpi_dly_cnt, 2
.global tpi_dly_cnt

.comm tpi_parity_err, 1
.global tpi_parity_err

/* =========================================================
 *              PUBLIC  API
 * =======================================================*/
.global tpi_init
.global tpi_send_byte
.global tpi_recv_byte
.global tpi_pr_update


/* ---------------------------------------------------------
 *  tpi_init — configure pins + 32 idle clocks
 * -------------------------------------------------------*/
tpi_init:
    cbi _SFR_IO_ADDR(TPI_CLK_PORT), TPI_CLK_PIN
    sbi _SFR_IO_ADDR(TPI_CLK_DDR),  TPI_CLK_PIN

    sbi _SFR_IO_ADDR(TPI_DATA_PORT), TPI_DATA_PIN
    cbi _SFR_IO_ADDR(TPI_DATA_DDR),  TPI_DATA_PIN

    ldi r21, 32
1:  rcall tpi_bit_h
    dec r21
    brne 1b
    ret


/* ---------------------------------------------------------
 *  tpi_send_byte — LSB-first, parity, 2 stop bits
 *  in:  r24 = data
 * -------------------------------------------------------*/
tpi_send_byte:
    rcall tpi_bit_l          ; start bit (T=0)
    ldi r18, 8               ; bit counter
    clr r19                  ; parity accumulator

1:
    bst r24, 0               ; T = data[0] (LSB first)
    brtc 2f
    inc r19                  ; If T=1, increment parity counter
2:
    rcall tpi_bit_tx         ; send T
    lsr r24                  ; shift data (r24 БЕЗОПАСЕН!)
    dec r18
    brne 1b

    bst r19, 0               ; T = parity bit (LSB of r19)
    rcall tpi_bit_tx         ; send parity bit
    rcall tpi_bit_h          ; stop 1
    rcall tpi_bit_h          ; stop 2
    ret


/* ---------------------------------------------------------
 *  tpi_recv_byte — LSB-first, parity check
 *  out: r24 = data (0 if timeout/error)
 *  sets tpi_parity_err = 1 on parity error or timeout
 * -------------------------------------------------------*/
tpi_recv_byte:
    clr r19
    sts tpi_parity_err, r19

    ldi r18, 192             ; timeout
wait_start:
    rcall tpi_bit_h
    brtc rx_data
    dec r18
    brne wait_start

    ; === TIMEOUT ===
    ldi r24, 0x00            ; Возвращаем 0 (чтобы не завис while в C)
    ldi r18, 1
    sts tpi_parity_err, r18  ; Ставим флаг ошибки
    
    ; Break sequence при таймауте
    ldi r18, 26
3:  rcall tpi_bit_l
    dec r18
    brne 3b
    rcall tpi_bit_h
    rjmp recv_exit

rx_data:
    ldi r18, 8
    clr r19                  ; calculated parity
    clr r24                  ; received data

recv_loop:
    rcall tpi_bit_h          ; sample bit → T (LSB first)
    lsr r24
    bld r24, 7               ; data[7] = T
    brtc 2f
    inc r19                  ; If T=1, increment parity counter
2:
    dec r18
    brne recv_loop

    rcall tpi_bit_h          ; Читаем бит четности в T
    bld r18, 0               ; r18[0] = принятый бит четности
    eor r19, r18             ; r19[0] = вычисленный XOR принятый
    sbrc r19, 0
    rjmp parity_error

    rcall tpi_bit_h          ; stop 1
    rcall tpi_bit_h          ; stop 2
    rjmp recv_exit

parity_error:
    clr r24                  ; return 0 on parity error
    ldi r18, 1
    sts tpi_parity_err, r18
    
    ; Добавляем break для сброса TPI state machine таргета
    ldi r18, 26
4:  rcall tpi_bit_l          ; break: 26 low bits
    dec r18
    brne 4b
    rcall tpi_bit_h          ; release bus

recv_exit:
    ret


/* ---------------------------------------------------------
 *  LOW-LEVEL BIT I/O
 * -------------------------------------------------------*/
tpi_bit_h:                   ; send '1' (release bus)
    set                      ; T = 1
    rjmp tpi_bit_tx

tpi_bit_l:                   ; send '0' (drive low)
    clt                      ; T = 0
    rjmp tpi_bit_tx

tpi_bit_tx:                  ; exchange 1 bit (open-drain via DDR)
    brts 1f
    ; Drive low: PORT=0, DDR=1
    cbi _SFR_IO_ADDR(TPI_DATA_PORT), TPI_DATA_PIN
    sbi _SFR_IO_ADDR(TPI_DATA_DDR),  TPI_DATA_PIN
    rjmp 2f
1:  ; Release: PORT=1 (pull-up), DDR=0 (input)
    sbi _SFR_IO_ADDR(TPI_DATA_PORT), TPI_DATA_PIN
    cbi _SFR_IO_ADDR(TPI_DATA_DDR),  TPI_DATA_PIN
2:
    rcall tpi_delay          ; t_low

    sbi _SFR_IO_ADDR(TPI_CLK_PORT), TPI_CLK_PIN ; CLK high

    rcall tpi_delay          ; t_high (slave drives response)

    in  r25, _SFR_IO_ADDR(TPI_DATA_IN)
    bst r25, TPI_DATA_PIN    ; T = sampled bit

    cbi _SFR_IO_ADDR(TPI_CLK_PORT), TPI_CLK_PIN ; CLK low
    ret


/* ---------------------------------------------------------
 *  DELAY (Safe against 0, uses r26/r27 to NOT clobber r24!)
 * -------------------------------------------------------*/
tpi_delay:
    lds r26, tpi_dly_cnt
    lds r27, tpi_dly_cnt+1
    sbiw r26, 0              ; Проверяем, если r27:r26 == 0
    breq delay_ret
1:  sbiw r26, 1
    brne 1b
delay_ret:
    ret

/* ---------------------------------------------------------
 *  UPDATE POINTER REGISTER (PR = r25:r24)
 * -------------------------------------------------------*/
tpi_pr_update:
    movw r22, r24            ; r23:r22 = addr
    ldi r24, TPI_OP_SSTPR(0) ; Макрос команды TPI (0x68)
    rcall tpi_send_byte
    mov r24, r22             ; addr low (SPL)
    rcall tpi_send_byte
    ldi r24, TPI_OP_SSTPR(1) ; Макрос команды TPI (0x69)
    rcall tpi_send_byte
    mov r24, r23             ; addr high (SPH)
    rcall tpi_send_byte
    ret

