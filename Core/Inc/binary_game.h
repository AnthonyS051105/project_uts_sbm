#ifndef __BINARY_GAME_H
#define __BINARY_GAME_H

#include "main.h"
#include <stdint.h>

#define BG_MAX_ROUNDS  10U   /* jumlah soal per sesi */

extern volatile uint8_t bg_session_done;

/**
 * Inisialisasi game: generate target, jalankan countdown 3-2-1 dengan buzzer,
 * lalu tunggu input user. Dipanggil saat masuk Mode 8.
 */
void BinaryGame_Init(void);

/**
 * Game-loop tick. Dipanggil setiap iterasi while(1) saat Mode 8 aktif.
 * (Saat ini kosong — logik diproses via interrupt BTN.)
 */
void BinaryGame_Run(void);

/**
 * Reset semua state dan matikan LED + buzzer. Dipanggil saat keluar Mode 8.
 */
void BinaryGame_Reset(void);

/**
 * Dipanggil dari HAL_GPIO_EXTI_Callback saat BTN1 ditekan.
 * Memasukkan bit '1' ke jawaban.
 */
void BinaryGame_BTN1_Press(void);

/**
 * Dipanggil dari HAL_GPIO_EXTI_Callback saat BTN2 ditekan.
 * Memasukkan bit '0' ke jawaban.
 */
void BinaryGame_BTN2_Press(void);

/**
 * Return current target byte for displaying on frontend dashboard
 */
uint8_t BinaryGame_GetTarget(void);
uint8_t BinaryGame_GetScore(void);
uint8_t BinaryGame_GetRound(void);

/**
 * Override fungsi ini di main.c untuk mengirim string ke USB CDC / UART.
 * Contoh:
 *   void BinaryGame_UART_Transmit(const char *buf, uint16_t len) {
 *       CDC_Transmit_FS((uint8_t*)buf, len);
 *   }
 */
void BinaryGame_UART_Transmit(const char *buf, uint16_t len);

#endif /* __BINARY_GAME_H */
