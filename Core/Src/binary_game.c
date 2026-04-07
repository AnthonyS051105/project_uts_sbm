#include "binary_game.h"
#include <stdio.h>
#include <string.h>

static uint8_t bg_target = 0;
static uint8_t bg_input = 0;
static uint8_t bg_bit_count = 0;
static uint8_t bg_score = 0;
static uint8_t bg_round = 0;
static uint32_t bg_rng = 0;

volatile uint8_t bg_session_done = 0;
volatile uint8_t bg_process_pending = 0;

typedef struct { GPIO_TypeDef *port; uint16_t pin; } _Led_t;

static const _Led_t _leds[8] = {
    {LED1_GPIO_Port, LED1_Pin}, {LED2_GPIO_Port, LED2_Pin},
    {LED3_GPIO_Port, LED3_Pin}, {LED4_GPIO_Port, LED4_Pin},
    {LED5_GPIO_Port, LED5_Pin}, {LED6_GPIO_Port, LED6_Pin},
    {LED7_GPIO_Port, LED7_Pin}, {LED8_GPIO_Port, LED8_Pin},
};

static void bg_set_leds(uint8_t pattern) {
    for (int i = 0; i < 8; i++) {
        uint8_t bit = (pattern >> (7 - i)) & 1u;
        HAL_GPIO_WritePin(_leds[i].port, _leds[i].pin, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

static void bg_all_leds_off(void) { bg_set_leds(0x00); }
static void bg_all_leds_on(void)  { bg_set_leds(0xFF); }

static void bg_buzzer_on(void) { HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET); }
static void bg_buzzer_off(void) { HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET); }

static void bg_beep(uint32_t on_ms, uint32_t off_ms) {
    bg_buzzer_on();
    HAL_Delay(on_ms);
    bg_buzzer_off();
    if (off_ms > 0) HAL_Delay(off_ms);
}

static void bg_buzz_countdown(uint8_t count) {
    if (count == 1) {
        bg_beep(600, 0);
    } else {
        for (uint8_t i = 0; i < count; i++) bg_beep(120, 150);
    }
}

static void bg_buzz_correct(void) {
    bg_beep(80, 70);
    bg_beep(80, 70);
    bg_beep(120, 0);
}

static void bg_buzz_wrong(void) {
    bg_beep(350, 150);
    bg_beep(350, 0);
}

__weak void BinaryGame_UART_Transmit(const char *buf, uint16_t len) {
    (void)buf; (void)len;
}

static void bg_transmit_fmt(const char *buf, uint16_t len) {
    BinaryGame_UART_Transmit(buf, len);
}

static uint8_t bg_rand_byte(void) {
    bg_rng = bg_rng * 1664525u + 1013904223u;
    return (uint8_t)((bg_rng >> 16) & 0xFF);
}

static void bg_new_round(void) {
    bg_bit_count = 0;
    bg_input = 0;
    bg_round++;

    bg_rng += HAL_GetTick();
    bg_target = bg_rand_byte();
    if (bg_target == 0) bg_target = 1;

    char buf[64];
    uint16_t len = (uint16_t)snprintf(buf, sizeof(buf),
        "BINARY,ROUND:%u,TARGET:%u\r\n", (unsigned)bg_round, (unsigned)bg_target);
    bg_transmit_fmt(buf, len);

    for (int cnt = 3; cnt >= 1; cnt--) {
        if (cnt == 3) HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_SET);
        else if (cnt == 2) HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_SET);
        else if (cnt == 1) HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_SET);

        bg_buzz_countdown((uint8_t)cnt);
        HAL_Delay(300);

        if (cnt == 3) HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_RESET);
        else if (cnt == 2) HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_RESET);
        else if (cnt == 1) HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_RESET);
    }
    bg_all_leds_off();
}

/* ================================================================== *
 *  API publik                                                          *
 * ================================================================== */

void BinaryGame_Init(void) {
    bg_target = 0; bg_input = 0; bg_bit_count = 0;
    bg_score = 0; bg_round = 0; bg_session_done = 0;
    bg_process_pending = 0; bg_rng = HAL_GetTick();

    bg_all_leds_off();
    bg_buzzer_off();
    bg_new_round();
}

uint8_t BinaryGame_GetTarget(void) { return bg_target; }
uint8_t BinaryGame_GetScore(void) { return bg_score; }
uint8_t BinaryGame_GetRound(void) { return bg_round; }

void BinaryGame_Run(void) {
    if (bg_process_pending) {
        bg_process_pending = 0;
        char buf[72];
        uint16_t len;

        if (bg_input == bg_target) {
            bg_score++;
            for (int f = 0; f < 2; f++) {
                bg_all_leds_on(); HAL_Delay(100);
                bg_all_leds_off(); HAL_Delay(100);
            }
            bg_buzz_correct();
            len = (uint16_t)snprintf(buf, sizeof(buf), "BINARY,RESULT:CORRECT,SCORE:%u,TARGET:%u,INPUT:%u\r\n", (unsigned)bg_score, (unsigned)bg_target, (unsigned)bg_input);
            bg_transmit_fmt(buf, len);
        } else {
            bg_set_leds(bg_input); HAL_Delay(600);
            bg_set_leds(bg_target); HAL_Delay(600);
            bg_buzz_wrong();
            len = (uint16_t)snprintf(buf, sizeof(buf), "BINARY,RESULT:WRONG,SCORE:%u,TARGET:%u,INPUT:%u\r\n", (unsigned)bg_score, (unsigned)bg_target, (unsigned)bg_input);
            bg_transmit_fmt(buf, len);
        }

        HAL_Delay(500);

        if (bg_round >= BG_MAX_ROUNDS) {
            len = (uint16_t)snprintf(buf, sizeof(buf), "BINARY,SESSION_END,SCORE:%u,ROUNDS:%u\r\n", (unsigned)bg_score, (unsigned)bg_round);
            bg_transmit_fmt(buf, len);

            bg_set_leds(bg_score);
            HAL_Delay(2000);
            uint8_t pat = bg_score;
            for (int i = 0; i < 8; i++) {
                pat &= (uint8_t)~(0x80u >> i);
                bg_set_leds(pat);
                HAL_Delay(120);
            }
            bg_all_leds_off();
            bg_session_done = 1;
        } else {
            bg_new_round();
        }
    }
}

void BinaryGame_Reset(void) {
    bg_target = 0; bg_input = 0; bg_bit_count = 0;
    bg_score = 0; bg_round = 0; bg_session_done = 0;
    bg_process_pending = 0;
    bg_all_leds_off();
    bg_buzzer_off();
}

static void bg_process_bit(uint8_t bit) {
    if (bg_bit_count >= 8) return;
    bg_input = (bg_input << 1) | (bit & 1u);
    bg_bit_count++;
    bg_set_leds((uint8_t)(bg_input << (8u - bg_bit_count)));
    if (bg_bit_count == 8) bg_process_pending = 1;
}

void BinaryGame_BTN1_Press(void) { bg_process_bit(1); }
void BinaryGame_BTN2_Press(void) { bg_process_bit(0); }
