/**
 * \brief Header for tpi
 * \file tpi.h
 */
#ifndef __TPI_H__
#define __TPI_H__
#include <stdint.h>


/* Globals */
/** Number of iterations in tpi_delay loop */
extern uint16_t tpi_dly_cnt;


/* Functions */
/**
 * TPI init
 */
void tpi_init(void);
/**
 * Send raw byte by TPI
 * \param b Byte to send
 */
void tpi_send_byte(uint8_t b);
/**
 * Receive one raw byte from TPI
 * \return Received byte
 */
uint8_t tpi_recv_byte(void);
/**
 * Read block
 * \param addr Address of block
 * \param dptr Pointer to dest memory block
 * \param len Length of read
 */

void tpi_pr_update(uint16_t addr);

#endif /*__TPI_H__*/
