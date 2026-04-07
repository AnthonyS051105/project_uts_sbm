#include "rhythm_game.h"
#include <string.h>
#include <stdio.h>

void RhythmGame_UART_Transmit(const char *buf, uint16_t len);

typedef struct { GPIO_TypeDef *port; uint16_t pin; } _Led_t;

static const _Led_t _leds[8] = {
    {LED1_GPIO_Port, LED1_Pin}, {LED2_GPIO_Port, LED2_Pin},
    {LED3_GPIO_Port, LED3_Pin}, {LED4_GPIO_Port, LED4_Pin},
    {LED5_GPIO_Port, LED5_Pin}, {LED6_GPIO_Port, LED6_Pin},
    {LED7_GPIO_Port, LED7_Pin}, {LED8_GPIO_Port, LED8_Pin},
};

static void rg_set_leds(uint8_t pattern) {
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(_leds[i].port, _leds[i].pin,
            ((pattern >> i) & 1u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

extern volatile uint32_t adc_dma_val;

static const uint8_t LEVEL_LEN[5] = {3, 3, 4, 5, 6};

RhythmGame_t rg;

static volatile uint8_t rg_btn2_flag = 0;
volatile uint8_t rg_session_done = 0;

static void buzzer_beep(uint32_t on_ms) {
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
    HAL_Delay(on_ms);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}

static void buzzer_correct_perfect(void) {
}

static void buzzer_correct_near(void) {
}

static void buzzer_wrong(void) {
}

static uint32_t rg_calc_beat_ms(void) {
    uint32_t bpm = 60u + (adc_dma_val * 120u / 4095u);
    if (bpm < 60u) bpm = 60u;
    if (bpm > 180u) bpm = 180u;
    return 60000u / bpm;
}

static void rg_play_demo(void) {
    uint32_t on_ms = (rg.beat_ms > 600u) ? 300u : rg.beat_ms / 2u;
    uint32_t off_ms = rg.beat_ms - on_ms;

    for (uint8_t i = 0; i < rg.pat_len; i++) {
        rg_set_leds(0xFF);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        HAL_Delay(on_ms);
        rg_set_leds(0x00);
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        HAL_Delay(off_ms);
    }
}

/**
 * Sinyal siap: hitung mundur "3..2..1"
 *   "3": LED11 ON + beep sangat pendek (50ms)
 *   "2": LED10 ON + beep sangat pendek (50ms)
 *   "1": LED9 ON + beep panjang (400ms) = GO!
 */
static void rg_ready_signal(void) {
    HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    HAL_Delay(450);
    HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_RESET);
    HAL_Delay(500);

    HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    HAL_Delay(450);
    HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_RESET);
    HAL_Delay(500);

    rg.state = RG_INPUT;
    rg.input_start = HAL_GetTick();

    HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
    HAL_Delay(400);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_RESET);
}

static void rg_compute_errors(void) {
    for (uint8_t i = 0; i < rg.pat_len; i++) {
        if (i < rg.tap_count) {
            int32_t expected = (int32_t)(rg.input_start + (uint32_t)i * rg.beat_ms);
            rg.tap_err[i] = (int32_t)rg.tap_ts[i] - expected;
        } else {
            rg.tap_err[i] = (int32_t)rg.beat_ms;
        }
    }
}

static int32_t _abs32(int32_t v) { return v < 0 ? -v : v; }

static uint8_t rg_classify(int32_t err) {
    int32_t a = _abs32(err);
    if (a <= 50) return 0;
    if (a <= 150) return 1;
    return 2;
}

static uint32_t rg_calc_round_score(void) {
    uint32_t total = 0;
    uint8_t all_pef = 1;

    for (uint8_t i = 0; i < rg.pat_len; i++) {
        uint8_t cls = rg_classify(rg.tap_err[i]);
        if (cls == 0) {
            total += 100u;
        } else if (cls == 1) {
            int32_t pts = 60 - (_abs32(rg.tap_err[i]) - 50) / 3;
            total += (uint32_t)(pts > 0 ? pts : 0);
            all_pef = 0;
        } else {
            all_pef = 0;
        }
    }

    if (all_pef) {
        uint32_t bonus = 200u;
        if (rg.hint_used) bonus /= 2u;
        total += bonus;
    }
    return total;
}

/**
 * Feedback per ketukan lalu skor biner 1 detik.
 *   PERFECT : semua LED solid 400ms
 *   NEAR    : alternating 0x55/0xAA berkedip ×2 (simulasi amber)
 *   MISS    : flash sekali 200ms
 */
static void rg_show_feedback(void)
{
    for (uint8_t i = 0; i < rg.pat_len; i++) {
        switch (rg_classify(rg.tap_err[i])) {
            case 0:  /* PERFECT — LED solid + 2 beep cepat naik (riang) */
                rg_set_leds(0xFF);
                buzzer_correct_perfect();   /* 80 ms + 60 ms gap + 120 ms = 260 ms */
                HAL_Delay(140);             /* total LED on ≈ 400 ms */
                rg_set_leds(0x00);
                HAL_Delay(100);
                break;
            case 1:  /* NEAR — alternating LED + 1 beep sedang */
                for (int j = 0; j < 2; j++) {
                    rg_set_leds(0x55);
                    HAL_Delay(150);
                    rg_set_leds(0xAA);
                    HAL_Delay(150);
                }
                buzzer_correct_near();      /* 200 ms */
                rg_set_leds(0x00);
                HAL_Delay(50);
                break;
            default: /* MISS — flash LED + beep panjang berat */
                rg_set_leds(0xFF);
                buzzer_wrong();             /* 500 ms */
                rg_set_leds(0x00);
                HAL_Delay(100);
                break;
        }
    }

    /* Skor kumulatif sebagai biner (clamp ke 255) */
    rg_set_leds((rg.score > 255u) ? 0xFF : (uint8_t)rg.score);
    HAL_Delay(1000);
    rg_set_leds(0x00);
}

/** Animasi level up: N LED berkedip ×3 + buzzer ascending (3 beep naik durasi). */
static void rg_level_up_anim(void)
{
    uint8_t pat = (rg.level >= 8u) ? 0xFF
                : (uint8_t)((1u << rg.level) - 1u);
    uint32_t beep_dur[3] = {80, 120, 200}; /* durasi naik = ascending feel */
    for (int i = 0; i < 3; i++) {
        rg_set_leds(pat);
        // HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        HAL_Delay(beep_dur[i]);
        // HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        HAL_Delay(300 - beep_dur[i]);   /* sisa LED on */
        rg_set_leds(0x00);
        HAL_Delay(300);
    }
    HAL_Delay(200);
}

/**
 * Animasi game over: semua LED menyala → padam kanan ke kiri →
 * skor biner tampil 3 detik.
 * Buzzer: 3 beep descending (panjang → pendek) lalu hening.
 */
static void rg_game_over_anim(void)
{
    /* Kirim SESSION_END ke python bridge / dashboard */
    char buf[64];
    uint16_t len = (uint16_t)snprintf(buf, sizeof(buf),
        "RHYTHM,SESSION_END,SCORE:%lu\r\n", rg.score);
    RhythmGame_UART_Transmit(buf, len);

    /* 3 beep descending sebagai penanda kalah */
    uint32_t beep_dur[3] = {400, 250, 100};
    for (int i = 0; i < 3; i++) {
        rg_set_leds(0xFF);
        // HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        HAL_Delay(beep_dur[i]);
        // HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
        rg_set_leds(0x00);
        HAL_Delay(150);
    }
    HAL_Delay(200);

    /* Animasi padam kanan ke kiri */
    uint8_t pat = 0xFF;
    for (int i = 7; i >= 0; i--) {
        pat &= (uint8_t)~(1u << i);
        rg_set_leds(pat);
        HAL_Delay(150);
    }
    HAL_Delay(300);

    rg_set_leds((rg.score > 255u) ? 0xFF : (uint8_t)rg.score);
    HAL_Delay(3000);
    rg_set_leds(0x00);
}

/* ================================================================== *
 *  Step 5 — CubeMonitor via UART                                      *
 * ================================================================== */

/**
 * Override fungsi ini di file lain setelah UART dikonfigurasi di .ioc.
 * Contoh: void RhythmGame_UART_Transmit(const char *buf, uint16_t len) {
 *             HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, 100); }
 */
__weak void RhythmGame_UART_Transmit(const char *buf, uint16_t len)
{
    (void)buf; (void)len;
}

static void rg_send_uart_result(void)
{
    char buf[128];
    int  pos = 0;

    /* Rata-rata |error| */
    int32_t avg = 0;
    for (uint8_t i = 0; i < rg.pat_len; i++) avg += _abs32(rg.tap_err[i]);
    if (rg.pat_len > 0) avg /= (int32_t)rg.pat_len;

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                    "RHYTHM,LEVEL:%u,SCORE:%lu",
                    (unsigned)rg.level, (unsigned long)rg.score);

    for (uint8_t i = 0; i < rg.pat_len && i < 6u; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ",ERR%u:%ld", (unsigned)i, (long)rg.tap_err[i]);
    }

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                    ",AVG:%ld\r\n", (long)avg);

    RhythmGame_UART_Transmit(buf, (uint16_t)pos);
}

/* ================================================================== *
 *  Public API                                                          *
 * ================================================================== */

void RhythmGame_Init(void)
{
    memset(&rg, 0, sizeof(rg));
    rg.state     = RG_IDLE;
    rg.level     = 1;
    rg.lives     = 3;
    rg_btn2_flag  = 0;
    rg_session_done = 0;
    rg_set_leds(0x00);
}

void RhythmGame_Reset(void)
{
    RhythmGame_Init();
}

uint8_t RhythmGame_GetLevel(void)
{
    return rg.level;
}

/** Dipanggil dari ISR — rekam timestamp ketukan BTN1. */
void RhythmGame_BTN1_Tap(void)
{
    if (rg.state != RG_INPUT) return;

    uint32_t now = HAL_GetTick();
    /* Sinkronkan fase (titik 0 ms) di ketukan PERTAMA pemain */
    if (rg.tap_count == 0) {
        rg.input_start = now;
    }

    if (rg.tap_count < rg.pat_len) {
        rg.tap_ts[rg.tap_count++] = now;
    } else {
        rg.extra_taps++;
    }
}

/** Dipanggil dari ISR — set flag B2 untuk deteksi di state INPUT. */
void RhythmGame_BTN2_Press(void)
{
    rg_btn2_flag = 1;
}

/**
 * State machine utama.
 * Dipanggil setiap iterasi while(1) saat current_mode == MODE_GAME.
 *
 * Blocking (HAL_Delay):  IDLE, DEMO, READY, FEEDBACK, LEVEL_UP, GAME_OVER
 * Non-blocking:          INPUT — hanya cek flag/waktu, tidak ada Delay
 */
void RhythmGame_Run(void)
{
    switch (rg.state) {

        /* ---------------------------------------------------------- */
        case RG_IDLE:
            rg_set_leds(0x00);
            HAL_Delay(500);
            /* Tampilkan nyawa: N LED paling kiri menyala sesaat */
            {
                uint8_t lp = (rg.lives >= 8u)
                           ? 0xFF : (uint8_t)((1u << rg.lives) - 1u);
                rg_set_leds(lp);
                HAL_Delay(600);
                rg_set_leds(0x00);
                HAL_Delay(400);
            }
            rg.pat_len = LEVEL_LEN[rg.level - 1u];
            rg.state   = RG_DEMO;
            break;

        /* ---------------------------------------------------------- */
        case RG_DEMO:
            rg.beat_ms    = rg_calc_beat_ms();
            rg.hint_used  = 0;
            rg.tap_count  = 0;
            rg.extra_taps = 0;
            rg_btn2_flag  = 0;

            /* Tunda sebentar sebelum demo mulai */
            HAL_Delay(500);
            rg_play_demo();
            /* Jeda 1 detik agar suara demo dan aba-aba terpisah jelas */
            HAL_Delay(1000);

            rg.state = RG_READY;
            break;

        /* ---------------------------------------------------------- */
        case RG_READY:
            rg.tap_count   = 0;
            rg.extra_taps  = 0;
            rg_ready_signal(); /* rg.state = RG_INPUT di-set di dalam ini sebelum beep GO */
            break;

        /* ---------------------------------------------------------- */
        case RG_INPUT: {
            /*
             * Non-blocking: hanya cek kondisi lalu return.
             * Jika belum ada aksi sama sekali, waktu tunggu panjang: 5 detik.
             * Jika ketukan pertama sudah terjadi, batas waktu dinamik:
             * start + pat_len × beat_ms + 0.5 beat untuk toleransi.
             */
            uint32_t now      = HAL_GetTick();
            uint32_t deadline = (rg.tap_count == 0)
                               ? (rg.input_start + 5000u)
                               : (rg.input_start + ((uint32_t)rg.pat_len * rg.beat_ms) + (rg.beat_ms / 2u));

            /* B2 ditekan → ulang demo (hint), skor tidak direset */
            if (rg_btn2_flag) {
                rg_btn2_flag = 0;
                rg.hint_used = 1;
                rg.state     = RG_DEMO;
                break;
            }

            /* Semua ketukan masuk ATAU waktu habis */
            if (rg.tap_count >= rg.pat_len || now >= deadline) {
                rg_compute_errors();
                rg.state = RG_FEEDBACK;
            }
            break;
        }

        /* ---------------------------------------------------------- */
        case RG_FEEDBACK: {
            uint32_t round_score = rg_calc_round_score();
            rg.score += round_score;

            /* Hitung MISS dan hit dalam ronde ini */
            uint8_t miss_cnt = 0, hit_cnt = 0;
            for (uint8_t i = 0; i < rg.pat_len; i++) {
                if (rg_classify(rg.tap_err[i]) == 2) miss_cnt++;
                else                                   hit_cnt++;
            }

            rg_show_feedback();

            /* Flash singkat jika ada ketukan ekstra */
            if (rg.extra_taps > 0) {
                rg_set_leds(0xFF); HAL_Delay(100);
                rg_set_leds(0x00); HAL_Delay(100);
            }

            /* Kirim data ke CubeMonitor */
            rg_send_uart_result();

            uint32_t acc = (uint32_t)hit_cnt * 100u / rg.pat_len;

            /* Setiap ada MISS atau akurasi < 70%, nyawa seketika berkurang 1 */
            if (miss_cnt > 0u || acc < 70u || rg.extra_taps > 0) {
                if (rg.lives > 0u) rg.lives--;
            }

            if (rg.lives == 0u) {
                rg.state = RG_GAME_OVER;
                break;
            }

            /* Main maksimal 5 ronde, maju ronde tiap iterasi baik sukses maupun gagal */
            if (rg.level < 5u) {
                if (miss_cnt == 0u && acc >= 70u && rg.extra_taps == 0) {
                    rg.state = RG_LEVEL_UP;
                } else {
                    /* Gagal di level ini, tapi masih lanjut ke level berikutnya tanpa animasi */
                    rg.level++;
                    rg.pat_len = LEVEL_LEN[rg.level - 1u];
                    rg.state = RG_DEMO;
                }
            } else {
                rg.state = RG_GAME_OVER; /* Win / Selesai bermain 5 permainan */
            }
            break;
        }

        /* ---------------------------------------------------------- */
        case RG_LEVEL_UP:
            rg.level++;
            rg.pat_len = LEVEL_LEN[rg.level - 1u];
            rg_level_up_anim();
            rg.state = RG_DEMO;
            break;

        /* ---------------------------------------------------------- */
        case RG_GAME_OVER:
            rg_game_over_anim();
            /* Reset state internal agar siap sesi baru jika dipanggil lagi */
            rg.level       = 1;
            rg.score       = 0;
            rg.lives       = 3;
            rg.miss_streak = 0;
            rg.state       = RG_IDLE;
            /* Beritahu main.c bahwa sesi ini selesai → kembali ke lobby */
            rg_session_done = 1;
            break;

        default:
            rg.state = RG_IDLE;
            break;
    }
}
