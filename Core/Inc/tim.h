#ifndef __TIM_H__
#define __TIM_H__

#include "stm32f4xx_hal.h"

/* TIM2: digunakan untuk DHT11 microsecond timing (1 MHz) */
extern TIM_HandleTypeDef htim2;

/* TIM3: digunakan untuk HC-SR04 Input Capture */
extern TIM_HandleTypeDef htim3;

/* TIM4: digunakan untuk Servo PWM */
extern TIM_HandleTypeDef htim4;

#endif /* __TIM_H__ */
