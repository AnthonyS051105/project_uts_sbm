#include "whack_game.h"
#include "flash_storage.h"
#include <stdio.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1;
extern volatile uint32_t adc_dma_val;

__weak void WhackGame_UART_Transmit(const char *buf, uint16_t len) {
    (void)buf; (void)len;
}

static void whack_send_uart(const char* buf) {
    WhackGame_UART_Transmit(buf, strlen(buf));
}

static void LED_WriteRaw(uint8_t pattern) {
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, (pattern & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, (pattern & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, (pattern & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, (pattern & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, (pattern & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, (pattern & 0x20) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, (pattern & 0x40) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, (pattern & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

typedef enum {
    WG_IDLE,
    WG_SPAWN,
    WG_WAIT,
    WG_RESULT,
    WG_GAME_OVER
} WhackGameState;

static WhackGameState state = WG_IDLE;
static uint32_t state_tick = 0;
static uint32_t anim_tick = 0;
static int anim_step = 0;

static uint8_t max_lives = 3;
static int8_t lives = 3;
static uint32_t score = 0;
static uint32_t hit_streak = 0;
static uint32_t total_hits = 0;
static uint32_t hit_count_l = 0;
static uint32_t hit_count_r = 0;
static uint32_t miss_count_l = 0;
static uint32_t miss_count_r = 0;

static uint32_t base_window_ms = 900;
static uint32_t current_window_ms = 900;
static uint8_t target_led_1 = 0;
static uint8_t target_led_2 = 0;
static uint8_t is_double_spawn = 0;

static volatile uint32_t wg_window_counter = 0;
static volatile uint8_t wg_timeout_flag = 0;
static volatile uint32_t wg_time_remaining = 0;
static volatile uint8_t wg_tim3_running = 0;

static uint32_t spawn_tick = 0;
static uint8_t last_pos = 255;

// Inputs 
static volatile uint8_t wg_b1_pressed = 0;
static volatile uint8_t wg_b2_pressed = 0;
static volatile uint32_t b1_tick = 0;
static volatile uint32_t b2_tick = 0;

static uint8_t last_result_type = 0; // 0=HIT, 1=WRONG, 2=TIMEOUT, 3=PARTIAL
static uint8_t last_hit_button = 0; // 1 or 2
static uint32_t last_reaction_time = 0;

void WhackGame_TIM3_ISR(void) {
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = ~TIM_SR_UIF;
        
        if (wg_tim3_running) {
            if (wg_window_counter > 0) {
                wg_window_counter--;
                wg_time_remaining = wg_window_counter;
            }
            
            if (wg_window_counter == 0) {
                wg_timeout_flag = 1;
                wg_tim3_running = 0;
            }
        }
    }
}

void WhackGame_Init(void) {
    if ((RCC->APB1ENR & RCC_APB1ENR_TIM3EN) == 0) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        TIM3->PSC = 16 - 1; /* 16MHz / 16 = 1MHz */
        TIM3->CR1 = 0; /* Auto-reload preload disable, edge-aligned, upcounter */
        TIM3->ARR = 1000 - 1; /* 1MHz / 1000 = 1kHz (1ms update) */
        
        HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
    }
    WhackGame_Reset();
}

void WhackGame_Reset(void) {
    score = 0;
    hit_streak = 0;
    total_hits = 0;
    hit_count_l = hit_count_r = 0;
    miss_count_l = miss_count_r = 0;
    
    state = WG_IDLE;
    state_tick = HAL_GetTick();
    anim_step = 0;
    anim_tick = HAL_GetTick();
    
    wg_tim3_running = 0;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->DIER &= ~TIM_DIER_UIE;
    LED_WriteRaw(0);
}

void WhackGame_BTN1_Press(void) {
    if (state == WG_IDLE) {
        // Start game
        if (HAL_GetTick() - state_tick > 1000) {
            // Apply ADC mapping
            max_lives = 1 + (adc_dma_val * 4 / 4095);
            lives = max_lives;
            // 1200ms to 600ms
            base_window_ms = 1200 - (adc_dma_val * 600 / 4095);
            current_window_ms = base_window_ms;
            
            state = WG_SPAWN;
            LED_WriteRaw(0);
        }
    } else if (state == WG_WAIT) {
        if (!wg_b1_pressed) {
            wg_b1_pressed = 1;
            b1_tick = HAL_GetTick();
        }
    }
}

void WhackGame_BTN2_Press(void) {
    if (state == WG_WAIT) {
        if (!wg_b2_pressed) {
            wg_b2_pressed = 1;
            b2_tick = HAL_GetTick();
        }
    }
}

static uint8_t getCorrectButton(uint8_t ledPos) {
    return (ledPos <= 3) ? 1 : 2;
}

static void evaluate_hit(void) {
    char buf[128];
    uint32_t now = HAL_GetTick();
    
    if (is_double_spawn) {
        // Needs both B1 and B2 within 200ms
        if (wg_b1_pressed && wg_b2_pressed) {
            uint32_t diff = b1_tick > b2_tick ? b1_tick - b2_tick : b2_tick - b1_tick;
            if (diff <= 200) {
                // HIT (Double)
                last_result_type = 0;
                hit_streak++;
                total_hits++;
                score += 50 + (hit_streak * 5);
                hit_count_l++;
                hit_count_r++;
                if (lives < max_lives) lives++;
                
                last_reaction_time = (b1_tick > b2_tick ? b1_tick : b2_tick) - spawn_tick;
                snprintf(buf, sizeof(buf), "WHACK,POS:DOUBLE,BTN:BOTH,RT:%lu,RESULT:HIT,SCORE:%lu\r\n", last_reaction_time, score);
                whack_send_uart(buf);
            } else {
                // PARTIAL (Too far apart but both pressed)
                last_result_type = 3;
                hit_streak = 0;
            }
        } else if (wg_b1_pressed || wg_b2_pressed) {
            if (wg_timeout_flag) {
                // Time's up, only one pressed -> PARTIAL
                last_result_type = 3;
                hit_streak = 0;
            } else {
                // Still waiting for the other button
                return; 
            }
        } else if (wg_timeout_flag) {
            // TIMEOUT
            last_result_type = 2;
            hit_streak = 0;
            miss_count_l++; // Just count as both miss
            miss_count_r++;
        } else {
            // Still waiting
            return;
        }
    } else {
        // Single spawn
        uint8_t correctBtn = getCorrectButton(target_led_1);
        
        if (wg_timeout_flag) {
            last_result_type = 2; // TIMEOUT
            hit_streak = 0;
            if (correctBtn == 1) miss_count_l++; else miss_count_r++;
        } else if (wg_b1_pressed || wg_b2_pressed) {
            uint8_t pressedBtn = wg_b1_pressed ? 1 : 2;
            if (pressedBtn == correctBtn) {
                // HIT
                last_result_type = 0; 
                hit_streak++;
                total_hits++;
                score += 10 + (hit_streak * 5);
                if (correctBtn == 1) hit_count_l++; else hit_count_r++;
                
                last_reaction_time = (pressedBtn == 1 ? b1_tick : b2_tick) - spawn_tick;
                snprintf(buf, sizeof(buf), "WHACK,POS:%u,BTN:%u,RT:%lu,RESULT:HIT,SCORE:%lu\r\n", 
                    target_led_1, pressedBtn, last_reaction_time, score);
                whack_send_uart(buf);
            } else {
                // WRONG
                last_result_type = 1;
                hit_streak = 0;
                if (correctBtn == 1) miss_count_l++; else miss_count_r++;
            }
        } else {
            // No button pressed yet, keep waiting
            return;
        }
    }
    
    // If we reached here, a decision was made. Stop timer.
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->DIER &= ~TIM_DIER_UIE;
    wg_tim3_running = 0;
    
    // Apply damage
    if (last_result_type == 1 || last_result_type == 2) {
        lives--;
    }
    
    state = WG_RESULT;
    state_tick = HAL_GetTick();
    anim_step = 0;
    anim_tick = HAL_GetTick();
}

void WhackGame_Run(void) {
    uint32_t now = HAL_GetTick();
    
    switch(state) {
        case WG_IDLE:
            if (now - anim_tick > 100) {
                anim_tick = now;
                anim_step++;
                // Show currently selected lives from POT by blinking them
                uint8_t temp_lives = 1 + (adc_dma_val * 4 / 4095);
                uint8_t pattern = 0;
                for (uint8_t i = 0; i < temp_lives; i++) {
                    pattern |= (1 << i);
                }
                if (anim_step % 4 < 2) {
                    LED_WriteRaw(pattern);
                } else {
                    LED_WriteRaw(0);
                }
            }
            break;
            
        case WG_SPAWN:
            {
                uint8_t pos;
                do {
                    pos = (HAL_GetTick() ^ (adc_dma_val << 4)) % 8;
                } while (pos == last_pos);
                last_pos = pos;
                
                target_led_1 = pos;
                is_double_spawn = 0;
                
                if (total_hits > 0 && total_hits % 10 == 0) {
                    is_double_spawn = 1;
                    target_led_1 = (HAL_GetTick() ^ (adc_dma_val << 4)) % 4; // 0..3
                    target_led_2 = 4 + ((HAL_GetTick() ^ (adc_dma_val >> 2)) % 4); // 4..7
                }
                
                // Difficulty scale
                current_window_ms = base_window_ms - (total_hits / 5) * 60;
                if (current_window_ms < 200 || current_window_ms > 2000) current_window_ms = 200; // Underflow protect
                
                wg_b1_pressed = 0;
                wg_b2_pressed = 0;
                b1_tick = 0;
                b2_tick = 0;
                wg_timeout_flag = 0;
                wg_window_counter = current_window_ms;
                wg_time_remaining = current_window_ms;
                spawn_tick = HAL_GetTick();
                
                state = WG_WAIT;
                
                wg_tim3_running = 1;
                TIM3->EGR = TIM_EGR_UG;
                TIM3->SR = ~TIM_SR_UIF;
                TIM3->DIER |= TIM_DIER_UIE;
                TIM3->CR1 |= TIM_CR1_CEN;
            }
            break;
            
        case WG_WAIT:
            {
                // Proximity warning
                if (wg_time_remaining <= current_window_ms * 0.3) {
                    uint32_t blink_rate_ms = wg_time_remaining / 5;
                    if (blink_rate_ms < 15) blink_rate_ms = 15;
                    if (now - anim_tick > blink_rate_ms) {
                        anim_tick = now;
                        anim_step = !anim_step;
                        uint8_t pattern = is_double_spawn ? ((1<<target_led_1)|(1<<target_led_2)) : (1<<target_led_1);
                        LED_WriteRaw(anim_step ? pattern : 0);
                    }
                } else {
                    // Normal display
                    // is_double_spawn uses amber blink in PLANNING, we emulate with faster blink 
                    if (is_double_spawn) {
                        if (now - anim_tick > 100) {
                            anim_tick = now;
                            anim_step = !anim_step;
                            LED_WriteRaw(anim_step ? ((1<<target_led_1)|(1<<target_led_2)) : 0);
                        }
                    } else {
                        LED_WriteRaw(1 << target_led_1);
                    }
                }
                
                evaluate_hit();
            }
            break;
            
        case WG_RESULT:
            if (last_result_type == 0) { // HIT
                LED_WriteRaw(0);
                if (now - state_tick > 300) {
                    state = WG_SPAWN;
                }
            } else if (last_result_type == 3) { // PARTIAL
                LED_WriteRaw(0); // Maybe distinct anim? Just turn off for now.
                if (now - state_tick > 300) {
                    state = WG_SPAWN;
                }
            } else { // WRONG or TIMEOUT (damage taken)
                if (now - state_tick < 300) {
                    LED_WriteRaw(0xFF); // Flash red (all LEDs)
                } else if (now - state_tick < 800) {
                    // Show remaining lives: N left LEDs
                    uint8_t pattern = 0;
                    for (int i=0; i<lives; i++) pattern |= (1<<i);
                    LED_WriteRaw(pattern);
                } else {
                    LED_WriteRaw(0);
                    if (lives <= 0) {
                        state = WG_GAME_OVER;
                        state_tick = now;
                        anim_step = 0;
                        anim_tick = now;
                        LED_WriteRaw(0xFF);
                    } else {
                        state = WG_SPAWN;
                    }
                }
            }
            break;
            
        case WG_GAME_OVER:
            if (now - anim_tick > 150) {
                anim_tick = now;
                anim_step++;
                if (anim_step <= 8) {
                    // Turn off one by one from right to left
                    uint8_t pattern = 0;
                    for (int i=0; i<8 - anim_step; i++) pattern |= (1<<i);
                    LED_WriteRaw(pattern);
                } else if (anim_step == 9) {
                    LED_WriteRaw((score > 255) ? 0xFF : score); // Show score
                    // Send UART stats
                    char buf[128];
                    snprintf(buf, sizeof(buf), "WHACK_STAT,HITS_L:%lu,HITS_R:%lu,MISS_L:%lu,MISS_R:%lu\r\n", 
                        hit_count_l, hit_count_r, miss_count_l, miss_count_r);
                    whack_send_uart(buf);
                    
                    // Flash storage score handling
                    uint32_t hs = FlashStorage_Read(FLASH_HS_IDX_WHACK);
                    if (score > hs) {
                        FlashStorage_Write(FLASH_HS_IDX_WHACK, score);
                        // Optional: new record anim could be put here
                    }
                } else if (anim_step == 30) { // roughly 3 seconds later (21 * 150ms)
                    LED_WriteRaw(0);
                    WhackGame_Reset();
                }
            }
            break;
    }
}
