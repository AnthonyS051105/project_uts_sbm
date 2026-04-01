/**
 * @file    binary_game.c
 * @brief   Game Konversi Biner - Mode 4 (Fase 4)
 */

#include "binary_game.h"
#include <stdio.h>
#include <string.h>

static uint8_t binary_answer = 0;

/* Helper array LED terurut dari MSB (LED1) ke LSB (LED8) */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } _Led_t;

static const _Led_t _leds[8] = {
    {LED1_GPIO_Port, LED1_Pin}, /* MSB (bit 7) */
    {LED2_GPIO_Port, LED2_Pin}, /* bit 6       */
    {LED3_GPIO_Port, LED3_Pin}, /* bit 5       */
    {LED4_GPIO_Port, LED4_Pin}, /* bit 4       */
    {LED5_GPIO_Port, LED5_Pin}, /* bit 3       */
    {LED6_GPIO_Port, LED6_Pin}, /* bit 2       */
    {LED7_GPIO_Port, LED7_Pin}, /* bit 1       */
    {LED8_GPIO_Port, LED8_Pin}  /* LSB (bit 0) */
};

/* Menyalakan LED sesuai dengan bit pattern */
static void bg_set_leds(uint8_t pattern)
{
    for (int i = 0; i < 8; i++) {
        /* Karena _leds[0] adalah MSB (bit 7), maka kita geser ke kanan sebanyak (7 - i) */
        uint8_t bit = (pattern >> (7 - i)) & 1u;
        HAL_GPIO_WritePin(_leds[i].port, _leds[i].pin, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

/* Transmit UART Helper */
__weak void BinaryGame_UART_Transmit(const char *buf, uint16_t len)
{
    /* Implementasikan ini di main.c contoh: HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, 100); */
}

static void send_to_website(void)
{
    char buf[64];
    /* Kirim representasi biner dan nilainya ke web via UART */
    int pos = snprintf(buf, sizeof(buf), "BINARY,VAL:%d,BIN:", binary_answer);
    for (int i = 7; i >= 0; i--) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", (binary_answer >> i) & 1);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\n");
    
    if (pos > 0) {
        BinaryGame_UART_Transmit(buf, (uint16_t)pos);
    }
}

void BinaryGame_Init(void)
{
    binary_answer = 0;
    bg_set_leds(binary_answer);
}

void BinaryGame_Run(void)
{
    /* Game loop tidak perlu blocking/run logic banyak, input langsung diproses via interrupt BTN */
}

void BinaryGame_Reset(void)
{
    binary_answer = 0;
    bg_set_leds(0x00);
}

void BinaryGame_BTN1_Press(void)
{
    /* BTN1 adalah input angka '1' */
    binary_answer = (binary_answer << 1) | 1u;
    bg_set_leds(binary_answer);
    send_to_website();
}

void BinaryGame_BTN2_Press(void)
{
    /* BTN2 adalah input angka '0' */
    binary_answer = (binary_answer << 1) | 0u;
    bg_set_leds(binary_answer);
    send_to_website();
}
