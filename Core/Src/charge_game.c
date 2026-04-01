#include "charge_game.h"
#include "flash_storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern ADC_HandleTypeDef hadc1;
extern volatile uint32_t adc_dma_val;

/* Custom UART Tx helper if available, or just ignore since we don't have UART set up in main.c? 
Wait, the planning says sending UART to Node-RED. 
I'll just print with printf and let syscalls.c handle _write if ITM or UART is used. */

typedef enum {
    CG_INIT,
    CG_SHOW_TARGET,
    CG_WAIT_CHARGE,
    CG_CHARGING,
    CG_RELEASE,
    CG_EVALUATE,
    CG_ROUND_END,
    CG_GAME_OVER
} ChargeGameState;

static ChargeGameState state = CG_INIT;
static uint32_t state_tick = 0;
static uint8_t target_led;
static uint8_t current_led;
static uint8_t released_led;
static uint32_t charge_speed_ms;

static uint8_t round_num = 1;
static uint32_t total_score = 0;
static uint8_t perfect_streak = 0;
static uint32_t highscore = 0;

static uint8_t btn1_state_prev = 1; // 1 means unpressed (pull-up)
static uint8_t btn2_state_prev = 1;
static uint32_t btn1_pressed_tick = 0;
static uint32_t btn2_pressed_tick = 0;

static int anim_step = 0;
static uint32_t anim_tick = 0;

/* Helper functions */
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

void ChargeGame_Init(void) {
    state = CG_INIT;
    round_num = 1;
    total_score = 0;
    perfect_streak = 0;
    highscore = FlashStorage_Read(FLASH_HS_IDX_CHARGE);
    
    btn1_state_prev = HAL_GPIO_ReadPin(BTN1_GPIO_Port, BTN1_Pin);
    btn2_state_prev = HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin);
    
    LED_WriteRaw(0);
}

static uint8_t GetADC_seed() {
    return (uint8_t)(adc_dma_val & 0xFF);
}

void ChargeGame_Run(void) {
    uint32_t now = HAL_GetTick();
    uint8_t btn1_state = HAL_GPIO_ReadPin(BTN1_GPIO_Port, BTN1_Pin); // 0 = pressed
    uint8_t btn2_state = HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin);
    
    // Polling handling with debounce logic
    uint8_t btn1_just_pressed = (btn1_state == 0 && btn1_state_prev == 1);
    uint8_t btn1_just_released = (btn1_state == 1 && btn1_state_prev == 0);
    uint8_t btn2_just_pressed = (btn2_state == 0 && btn2_state_prev == 1);
    
    if (btn1_just_pressed) btn1_pressed_tick = now;
    if (btn2_just_pressed) btn2_pressed_tick = now;
    
    btn1_state_prev = btn1_state;
    btn2_state_prev = btn2_state;

    switch (state) {
        case CG_INIT:
            target_led = ((now ^ GetADC_seed()) % 8) + 1; // 1-8
            state = CG_SHOW_TARGET;
            state_tick = now;
            anim_step = 0;
            anim_tick = now;
            LED_WriteRaw(0);
            break;
            
        case CG_SHOW_TARGET:
            if (now - anim_tick >= 100) {
                anim_tick = now;
                anim_step++;
                
                if (anim_step <= 10) {
                    // Flash target LED 5 times (100ms on/off)
                    if (anim_step % 2 == 1) {
                        LED_WriteRaw(1 << (target_led - 1));
                    } else {
                        LED_WriteRaw(0);
                    }
                } else if (anim_step == 11) {
                    LED_WriteRaw(1 << (target_led - 1)); // Solid for 500ms
                } else if (anim_step == 17) { // 500ms / 100ms + margin = a proxy
                    LED_WriteRaw(0);
                } else if (anim_step == 22) { // Wait a bit then countdown
                    LED_WriteRaw(1); // Left 1
                } else if (anim_step == 26) {
                    LED_WriteRaw(3); // Left 2
                } else if (anim_step == 30) {
                    LED_WriteRaw(7); // Left 3
                } else if (anim_step >= 34) {
                    LED_WriteRaw(0);
                    state = CG_WAIT_CHARGE;
                    state_tick = now;
                }
            }
            break;
            
        case CG_WAIT_CHARGE:
            // Check BTN2 single press or hold
            if (btn2_just_pressed) {
                // Ignore for hold, handled differently. 
            } else if (btn2_state == 0 && (now - btn2_pressed_tick > 2000)) {
                // Hold BTN2 -> skip
                btn2_pressed_tick = now; // reset
                released_led = 0;
                target_led = 1; // dummy positive delta
                state = CG_EVALUATE;
                break;
            } else if (btn2_state == 1 && btn2_state_prev == 0 && (now - btn2_pressed_tick < 1000)) {
                // Short press BTN2 -> show target again
                state = CG_SHOW_TARGET;
                anim_step = 0;
                anim_tick = now;
                break;
            }

            if (btn1_just_pressed) {
                state = CG_CHARGING;
                current_led = 0;
                
                // Map ADC (0-4095) to speed (80-400ms)
                // Invert mapping: pot left (adc=0)=400, pot right (adc=4095)=80. Or vice versa depending on setup.
                charge_speed_ms = 400 - (adc_dma_val * 320 / 4095); 
                
                state_tick = now;
                anim_tick = now;
            }
            // IDLE timeout -> show target again after 5s
            if (now - state_tick > 5000) {
                state = CG_SHOW_TARGET;
                anim_step = 0;
                anim_tick = now;
            }
            break;
            
        case CG_CHARGING:
            if (now - anim_tick >= charge_speed_ms) {
                anim_tick = now;
                current_led++;
                if (current_led <= 8) {
                    LED_WriteRaw((1 << current_led) - 1);
                } else {
                    // Over-charge / BUSTED
                    released_led = 9; // greater than 8 indicates busted
                    state = CG_EVALUATE;
                    state_tick = now;
                    break;
                }
            }
            
            if (btn1_just_released) {
                released_led = current_led;
                state = CG_RELEASE;
                state_tick = now;
            }
            break;
            
        case CG_RELEASE:
            if (now - state_tick >= 800) {
                state = CG_EVALUATE;
                state_tick = now;
                anim_step = 0;
            }
            break;
            
        case CG_EVALUATE:
        {
            int delta = released_led - target_led;
            int score = 0;
            const char* clazz = "";
            
            if (released_led > 8) {
                score = 0;
                perfect_streak = 0;
                clazz = "BUSTED";
                // Animation busted: flash red all fast 5x
                if (anim_step == 0) {
                    if (now - state_tick >= 100) {
                        state_tick = now;
                        static int flash_cnt = 0;
                        LED_WriteRaw((flash_cnt % 2 == 0) ? 0xFF : 0);
                        flash_cnt++;
                        if (flash_cnt > 10) {
                            flash_cnt = 0;
                            anim_step = 1;
                        }
                    }
                }
            } else if (delta == 0) {
                score = 100;
                perfect_streak++;
                if (perfect_streak >= 3) {
                    score += 150; // Bonus
                }
                clazz = "PERFECT";
                // Animation Perfect
                if (anim_step == 0) {
                    LED_WriteRaw(0xFF); // all on
                    if (now - state_tick > 300) {
                        state_tick = now;
                        anim_step = 1;
                    }
                } else if (anim_step <= 8) {
                    if (now - state_tick > 100) {
                        state_tick = now;
                        LED_WriteRaw(0xFF >> anim_step); // waterfall
                        anim_step++;
                    }
                } else {
                    anim_step = 99; // done
                }
            } else if (abs(delta) == 1) {
                score = 50;
                perfect_streak = 0;
                clazz = "NEAR";
                anim_step = 99;
            } else if (abs(delta) == 2) {
                score = 25;
                perfect_streak = 0;
                clazz = "CLOSE";
                anim_step = 99;
            } else {
                score = 0;
                perfect_streak = 0;
                clazz = "FAR";
                anim_step = 99;
            }
            
            if (anim_step == 99) {
                total_score += score;
                printf("CHARGE,R%d,TARGET:%d,LED:%d,CLASS:%s,SCORE:%d\n", round_num, target_led, released_led > 8 ? 8 : released_led, clazz, score);
                state = CG_ROUND_END;
                state_tick = now;
                LED_WriteRaw(0);
            }
        }
            break;
            
        case CG_ROUND_END:
            if (now - state_tick > 1000) {
                round_num++;
                if (round_num > 10) {
                    state = CG_GAME_OVER;
                    state_tick = now;
                    anim_step = 0;
                } else {
                    state = CG_INIT;
                }
            }
            break;
            
        case CG_GAME_OVER:
            if (anim_step == 0) {
                LED_WriteRaw(total_score & 0xFF); // display binary total score
                if (now - state_tick > 3000) {
                    LED_WriteRaw(0);
                    state_tick = now;
                    if (total_score > highscore) {
                        FlashStorage_Write(FLASH_HS_IDX_CHARGE, total_score);
                        highscore = total_score;
                        anim_step = 1; // new record anim
                    } else {
                        anim_step = 2; // finish
                    }
                }
            } else if (anim_step == 1) {
                if (now - state_tick > 300) {
                    state_tick = now;
                    static int blinks = 0;
                    LED_WriteRaw((blinks % 2 == 0) ? 0xF0 : 0x0F);
                    blinks++;
                    if (blinks > 6) {
                        LED_WriteRaw(0);
                        anim_step = 2;
                    }
                }
            } else if (anim_step == 2) {
                // Game completely over, restart after a while
                if (now - state_tick > 2000) {
                    ChargeGame_Init(); // Loop back to start
                }
            }
            break;
    }
}

void ChargeGame_Reset(void) {
    LED_WriteRaw(0);
    state = CG_INIT;
}

void ChargeGame_BTN2_Press(void) {
    // Ignored, we handle button internally
}
