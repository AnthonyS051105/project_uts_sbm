#ifndef RHYTHM_GAME_H
#define RHYTHM_GAME_H

#include "main.h"

/* ------------------------------------------------------------------ *
 *  Rhythm Tap Game — Fase 1                                           *
 *  Hardware: 8 LED, BTN1 (ketukan), BTN2 (ulang demo), POT (BPM)     *
 * ------------------------------------------------------------------ */

/* --- State machine ------------------------------------------------ */
typedef enum {
    RG_IDLE = 0,
    RG_DEMO,        /* STM32 memperlihatkan pola */
    RG_READY,       /* Sinyal aba-aba sebelum input */
    RG_INPUT,       /* Rekam ketukan pemain (non-blocking) */
    RG_FEEDBACK,    /* Tampilkan hasil per ketukan */
    RG_LEVEL_UP,    /* Animasi naik level */
    RG_GAME_OVER,   /* Animasi game over / win */
} RG_State;

/* --- Data per ketukan --------------------------------------------- */
typedef struct {
    RG_State  state;
    uint8_t   level;            /* level aktif: 1–5 */
    uint32_t  score;            /* skor kumulatif */
    uint8_t   lives;            /* nyawa tersisa */
    uint8_t   hint_used;        /* B2 dipakai di ronde ini */
    uint8_t   miss_streak;      /* ronde berturut dengan MISS */

    /* timing pola */
    uint8_t   pat_len;          /* jumlah ketukan pola saat ini */
    uint32_t  beat_ms;          /* interval antar ketukan (ms) */

    /* input pemain */
    uint32_t  input_start;      /* tick saat INPUT dimulai */
    uint32_t  tap_ts[10];       /* timestamp tiap ketukan */
    int32_t   tap_err[10];      /* deviasi ms (negatif=cepat, positif=lambat) */
    uint8_t   tap_count;        /* ketukan yang sudah masuk */
    uint8_t   extra_taps;       /* ketukan ekstra (di atas panjang pola) */
} RhythmGame_t;

/* Akses state publik (hanya-baca dari luar) */
extern RhythmGame_t rg;

/* --- Public API --------------------------------------------------- */
void RhythmGame_Init(void);
void RhythmGame_Run(void);
void RhythmGame_Reset(void);

/* Dipanggil dari HAL_GPIO_EXTI_Callback di main.c */
void RhythmGame_BTN1_Tap(void);
void RhythmGame_BTN2_Press(void);

#endif /* RHYTHM_GAME_H */
