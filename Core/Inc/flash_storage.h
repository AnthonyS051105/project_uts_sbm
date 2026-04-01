/**
 * @file    flash_storage.h
 * @brief   Highscore storage ke FLASH internal STM32F401 — Sektor 3 (0x0800C000)
 *
 * Tata letak (2 × uint32_t = 8 byte):
 *   offset 0 → Charge & Release highscore  (FLASH_HS_IDX_CHARGE)
 *   offset 4 → Whack-a-LED highscore       (FLASH_HS_IDX_WHACK)
 *
 * Dipakai oleh Fase 2 (charge_game) dan Fase 3 (whack_game).
 */

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include "main.h"

/* Indeks highscore (parameter idx di Read/Write) */
#define FLASH_HS_IDX_CHARGE  0u
#define FLASH_HS_IDX_WHACK   1u

/**
 * Baca highscore dari FLASH.
 * @param idx  FLASH_HS_IDX_CHARGE atau FLASH_HS_IDX_WHACK
 * @return     Nilai highscore (0 jika belum pernah ditulis / blank)
 */
uint32_t FlashStorage_Read(uint8_t idx);

/**
 * Tulis highscore ke FLASH.
 * Seluruh sektor dierase lalu kedua slot ditulis ulang agar data lain tidak hilang.
 * @param idx    FLASH_HS_IDX_CHARGE atau FLASH_HS_IDX_WHACK
 * @param value  Nilai highscore baru
 */
void FlashStorage_Write(uint8_t idx, uint32_t value);

#endif /* FLASH_STORAGE_H */
