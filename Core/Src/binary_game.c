/**
 * @file    binary_game.c
 * @brief   Game Konversi Biner - Mode 8
 *
 * Alur game:
 *   1. BinaryGame_Init() → generate target → countdown 3,2,1 (buzzer) → siap input
 *   2. User tekan BTN1 (bit '1') atau BTN2 (bit '0'), LED menampilkan progress
 *   3. Setelah 8 bit masuk, cek jawaban:
 *        Benar → buzzer correct (3 beep cepat) → round baru
 *        Salah → buzzer wrong (2 beep panjang) → round baru
 *
 * Pola Buzzer:
 *   Countdown "3" : 3 beep pendek (120ms on / 150ms off)
 *   Countdown "2" : 2 beep pendek (120ms on / 150ms off)
 *   Countdown "1" : 1 beep panjang (600ms on)
 *   Jawaban Benar : 3 beep cepat rapat (80ms on / 80ms off)
 *   Jawaban Salah : 2 beep panjang lambat (350ms on / 150ms off)
 */

#include "binary_game.h"
#include <stdio.h>
#include <string.h>

/* ================================================================== *
 *  State internal game                                                *
 * ================================================================== */
static uint8_t bg_target    = 0;  /* bilangan desimal yang harus dikonversi  */
static uint8_t bg_input     = 0;  /* jawaban biner yang sedang diinput user  */
static uint8_t bg_bit_count = 0;  /* jumlah bit yang sudah diinput (0–8)     */
static uint8_t bg_score     = 0;  /* jumlah jawaban benar                    */
static uint8_t bg_round     = 0;  /* nomor round saat ini                    */
static uint32_t bg_rng      = 0;  /* state pseudo-random LCG                 */

/* ================================================================== *
 *  LED mapping — MSB (bit7) = LED1, LSB (bit0) = LED8               *
 * ================================================================== */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } _Led_t;

static const _Led_t _leds[8] = {
    {LED1_GPIO_Port, LED1_Pin}, /* bit 7 */
    {LED2_GPIO_Port, LED2_Pin}, /* bit 6 */
    {LED3_GPIO_Port, LED3_Pin}, /* bit 5 */
    {LED4_GPIO_Port, LED4_Pin}, /* bit 4 */
    {LED5_GPIO_Port, LED5_Pin}, /* bit 3 */
    {LED6_GPIO_Port, LED6_Pin}, /* bit 2 */
    {LED7_GPIO_Port, LED7_Pin}, /* bit 1 */
    {LED8_GPIO_Port, LED8_Pin}, /* bit 0 */
};

static void bg_set_leds(uint8_t pattern)
{
    for (int i = 0; i < 8; i++) {
        uint8_t bit = (pattern >> (7 - i)) & 1u;
        HAL_GPIO_WritePin(_leds[i].port, _leds[i].pin,
            bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

static void bg_all_leds_off(void) { bg_set_leds(0x00); }
static void bg_all_leds_on(void)  { bg_set_leds(0xFF); }

/* ================================================================== *
 *  Buzzer helpers                                                      *
 * ================================================================== */
static void bg_buzzer_on(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
}

static void bg_buzzer_off(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}

/** Satu beep: nyala on_ms → mati off_ms */
static void bg_beep(uint32_t on_ms, uint32_t off_ms)
{
    bg_buzzer_on();
    HAL_Delay(on_ms);
    bg_buzzer_off();
    if (off_ms > 0) HAL_Delay(off_ms);
}

/**
 * Countdown buzzer.
 *   count == 3 → tiga beep pendek
 *   count == 2 → dua beep pendek
 *   count == 1 → satu beep panjang
 */
static void bg_buzz_countdown(uint8_t count)
{
    if (count == 1) {
        bg_beep(600, 0);          /* "1" : satu nada panjang (tanda start!) */
    } else {
        for (uint8_t i = 0; i < count; i++) {
            bg_beep(120, 150);    /* "3"/"2" : beep pendek sejumlah count    */
        }
    }
}

/**
 * Buzzer jawaban BENAR.
 * Pola: 3 beep cepat rapat → terdengar "bip-bip-bip" riang.
 */
static void bg_buzz_correct(void)
{
    bg_beep(80, 70);
    bg_beep(80, 70);
    bg_beep(120, 0);
}

/**
 * Buzzer jawaban SALAH.
 * Pola: 2 beep panjang lambat → terdengar "buuu---buuu---" berat.
 */
static void bg_buzz_wrong(void)
{
    bg_beep(350, 150);
    bg_beep(350, 0);
}

/* ================================================================== *
 *  UART / USB CDC transmit (override di main.c)                       *
 * ================================================================== */
__weak void BinaryGame_UART_Transmit(const char *buf, uint16_t len)
{
    (void)buf; (void)len;
    /* Implementasikan di main.c, contoh:
     * void BinaryGame_UART_Transmit(const char *buf, uint16_t len) {
     *     CDC_Transmit_FS((uint8_t*)buf, len);
     * }
     */
}

static void bg_transmit_fmt(const char *buf, uint16_t len)
{
    BinaryGame_UART_Transmit(buf, len);
}

/* ================================================================== *
 *  Pseudo-random (LCG)                                                *
 * ================================================================== */
static uint8_t bg_rand_byte(void)
{
    bg_rng = bg_rng * 1664525u + 1013904223u;
    return (uint8_t)((bg_rng >> 16) & 0xFF);
}

/* ================================================================== *
 *  Logik round baru                                                    *
 * ================================================================== */
static void bg_new_round(void)
{
    bg_bit_count = 0;
    bg_input     = 0;
    bg_round++;

    /* Perbarui seed dengan tick agar lebih acak setiap round */
    bg_rng += HAL_GetTick();
    bg_target = bg_rand_byte();
    if (bg_target == 0) bg_target = 1; /* hindari target 0 */

    /* Kirim info target ke host (web/serial monitor) */
    char buf[64];
    uint16_t len = (uint16_t)snprintf(buf, sizeof(buf),
        "BINARY,ROUND:%u,TARGET:%u\r\n",
        (unsigned)bg_round, (unsigned)bg_target);
    bg_transmit_fmt(buf, len);

    /* ----------------------------------------------------------------
     * Countdown visual + audio: 3 → 2 → 1
     *   LED: tampilkan jumlah LED menyala dari kiri (3/2/1 LED)
     *   Buzzer: beep sejumlah angka countdown
     * ---------------------------------------------------------------- */
    static const uint8_t cnt_patterns[3] = {
        0xE0,  /* "3": LED1-LED3 nyala  (0b11100000) */
        0xC0,  /* "2": LED1-LED2 nyala  (0b11000000) */
        0x80,  /* "1": LED1 saja nyala  (0b10000000) */
    };

    for (int cnt = 3; cnt >= 1; cnt--) {
        bg_set_leds(cnt_patterns[3 - cnt]);
        bg_buzz_countdown((uint8_t)cnt);
        HAL_Delay(300);
    }

    /* Siap menerima input: matikan semua LED */
    bg_all_leds_off();
}

/* ================================================================== *
 *  API publik                                                          *
 * ================================================================== */

void BinaryGame_Init(void)
{
    bg_target    = 0;
    bg_input     = 0;
    bg_bit_count = 0;
    bg_score     = 0;
    bg_round     = 0;
    bg_rng       = HAL_GetTick();

    bg_all_leds_off();
    bg_buzzer_off();

    bg_new_round();
}

void BinaryGame_Run(void)
{
    /* Semua logik diproses lewat BTN callback (interrupt-driven).
     * Fungsi ini sengaja kosong — dipanggil di main loop setiap iterasi. */
}

void BinaryGame_Reset(void)
{
    bg_target    = 0;
    bg_input     = 0;
    bg_bit_count = 0;
    bg_score     = 0;
    bg_round     = 0;
    bg_all_leds_off();
    bg_buzzer_off();
}

/* ------------------------------------------------------------------ *
 *  Proses satu bit input                                               *
 * ------------------------------------------------------------------ */
static void bg_process_bit(uint8_t bit)
{
    /* Abaikan jika sudah 8 bit (sedang dalam fase feedback) */
    if (bg_bit_count >= 8) return;

    bg_input = (bg_input << 1) | (bit & 1u);
    bg_bit_count++;

    /* Tampilkan progress: bit yang sudah dimasukkan rata kiri di LED */
    bg_set_leds((uint8_t)(bg_input << (8u - bg_bit_count)));

    if (bg_bit_count == 8) {
        char buf[72];
        uint16_t len;

        if (bg_input == bg_target) {
            /* ---- BENAR ---- */
            bg_score++;

            /* Visual: flash semua LED 2× */
            for (int f = 0; f < 2; f++) {
                bg_all_leds_on();
                HAL_Delay(100);
                bg_all_leds_off();
                HAL_Delay(100);
            }

            bg_buzz_correct();

            len = (uint16_t)snprintf(buf, sizeof(buf),
                "BINARY,RESULT:CORRECT,SCORE:%u,TARGET:%u,INPUT:%u\r\n",
                (unsigned)bg_score, (unsigned)bg_target, (unsigned)bg_input);
            bg_transmit_fmt(buf, len);

        } else {
            /* ---- SALAH ---- */

            /* Visual: tampilkan jawaban user sebentar, lalu tampilkan jawaban benar */
            bg_set_leds(bg_input);
            HAL_Delay(600);
            bg_set_leds(bg_target);
            HAL_Delay(600);

            bg_buzz_wrong();

            len = (uint16_t)snprintf(buf, sizeof(buf),
                "BINARY,RESULT:WRONG,SCORE:%u,TARGET:%u,INPUT:%u\r\n",
                (unsigned)bg_score, (unsigned)bg_target, (unsigned)bg_input);
            bg_transmit_fmt(buf, len);
        }

        /* Jeda singkat sebelum round baru */
        HAL_Delay(500);
        bg_new_round();
    }
}

void BinaryGame_BTN1_Press(void)
{
    bg_process_bit(1); /* Tombol 1 = bit '1' */
}

void BinaryGame_BTN2_Press(void)
{
    bg_process_bit(0); /* Tombol 2 = bit '0' */
}
