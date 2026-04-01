/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rhythm_game.h"
#include "charge_game.h"
#include "whack_game.h"
#include "binary_game.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MY_LAST2         24    /* 2 digit terakhir NIM sendiri  */
#define PARTNER_LAST2    19    /* 2 digit terakhir NIM partner  */
#define SHIFT_DELAY_MS   300   /* kecepatan shift Mode 1        */
#define COUNTER_DELAY_MS 100   /* kecepatan counter Mode 2 (ms) */
#define ADC_POLL_MS      50    /* polling ADC Mode 3 (ms)       */
#define DEBOUNCE_MS      200   /* debounce tombol (ms)          */
#define MODE_GAME        6     /* Mode game — aktif via tekan BTN1+BTN2 bersamaan */
#define SIMULTANEOUS_MS  150   /* window waktu (ms) untuk deteksi tekan bersamaan */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* USER CODE BEGIN PV */
/* ---- kontrol mode ---- */
volatile uint8_t current_mode  = 1;   /* mode aktif: 1, 2, atau 3          */
volatile uint8_t btn1_flag     = 0;   /* set oleh EXTI BTN1                 */
volatile uint8_t btn2_flag     = 0;   /* set oleh EXTI BTN2                 */
uint32_t btn1_last_tick        = 0;
uint32_t btn2_last_tick        = 0;

/* ---- Mode 1: shift left ---- */
uint8_t shift_pos = 0;                /* posisi LED yang menyala (0-7)      */

/* ---- Mode 2: counter (dipantau Cube Monitor - Chart) ---- */
uint32_t counter_value = 0;           /* nilai counter saat ini             */
uint8_t  counter_phase = 0;           /* 0 = NIM sendiri, 1 = NIM partner   */

/* ---- Mode 3: ADC -> LED (dipantau Cube Monitor - Gauge) ---- */
volatile uint32_t adc_dma_val = 0;    /* buffer DMA hasil ADC               */
volatile uint32_t led_count   = 0;    /* jumlah LED menyala (0-8)           */

/* ---- Mode 4: dua kereta bertabrakan ---- */
uint8_t train_step = 0;               /* langkah animasi kereta (0-2)       */

/* ---- Mode 5: binary counter 0-255 ---- */
uint8_t binary_count = 0;            /* nilai counter biner saat ini (0-255) */

/* ---- Mode Game: deteksi tekan bersamaan ---- */
volatile uint8_t simultaneous_flag = 0; /* set jika BTN1+BTN2 ditekan bersamaan */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Tabel urutan LED 1-8 */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } LED_t;
static const LED_t leds[8] = {
    {LED1_GPIO_Port, LED1_Pin},
    {LED2_GPIO_Port, LED2_Pin},
    {LED3_GPIO_Port, LED3_Pin},
    {LED4_GPIO_Port, LED4_Pin},
    {LED5_GPIO_Port, LED5_Pin},
    {LED6_GPIO_Port, LED6_Pin},
    {LED7_GPIO_Port, LED7_Pin},
    {LED8_GPIO_Port, LED8_Pin},
};

/**
 * Set LED berdasarkan bit-pattern 8-bit.
 * bit-0 = LED1, bit-7 = LED8.
 */
static void set_leds(uint8_t pattern)
{
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(leds[i].port, leds[i].pin,
                          ((pattern >> i) & 1u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

static void all_leds_on(void)  { set_leds(0xFF); }
static void all_leds_off(void) { set_leds(0x00); }

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  /* Mulai ADC secara continuous via DMA; adc_dma_val diperbarui otomatis */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&adc_dma_val, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* ============================================================
     * BTN1 + BTN2 bersamaan: toggle Mode Game
     * ============================================================ */
    if (simultaneous_flag) {
      simultaneous_flag = 0;
      if (current_mode >= 6 && current_mode <= 9) {
        /* Keluar dari mode game -> kembali ke Mode 1 */
        current_mode  = 1;
        shift_pos     = 0;
        counter_value = 0;
        counter_phase = 0;
        train_step    = 0;
        binary_count  = 0;
        RhythmGame_Reset();
        ChargeGame_Reset();
        WhackGame_Reset();
        BinaryGame_Reset();
        all_leds_off();
      } else {
        /* Masuk ke mode game 1 */
        current_mode = 6;
        all_leds_off();
        RhythmGame_Init();
      }
    }

    /* ============================================================
     * BTN2 (INTERRUPT): semua LED nyala 5 detik, lalu kembali ke
     * mode yang sama sebelum interrupt secara otomatis.
     * Hanya aktif di mode 1–5, TIDAK di mode game.
     * ============================================================ */
    if (btn2_flag && current_mode < 6) {
      btn2_flag = 0;
      all_leds_on();
      HAL_Delay(5000);
      all_leds_off();
    }

    /* ============================================================
     * BTN1: ganti mode  1 -> 2 -> 3 -> 4 -> 5 -> 1 -> ...
     * Hanya aktif di mode 1–5, TIDAK di mode game.
     * ============================================================ */
    if (btn1_flag && current_mode < 6) {
      btn1_flag    = 0;
      current_mode = (current_mode % 5) + 1;
      shift_pos     = 0;
      counter_value = 0;
      counter_phase = 0;
      train_step    = 0;
      binary_count  = 0;
      all_leds_off();
    }

    /* ============================================================
     * Eksekusi mode aktif
     * ============================================================ */
    switch (current_mode)
    {
      /* ----------------------------------------------------------
       * MODE 1: Shift Left
       * Satu LED menyala bergeser dari LED1 -> LED2 -> ... -> LED8
       * lalu kembali ke LED1.
       * ---------------------------------------------------------- */
      case 1:
      {
        /* step 0-7: LED1..LED8 menyala satu per satu
         * step 8  : semua mati, lalu mulai ulang */
        if (shift_pos < 8) {
          set_leds((uint8_t)(1u << shift_pos));
        } else {
          set_leds(0x00);
        }
        shift_pos = (shift_pos + 1) % 9;
        HAL_Delay(SHIFT_DELAY_MS);
        break;
      }

      /* ----------------------------------------------------------
       * MODE 2: Counter + Cube Monitor (Chart)
       * LED semua padam.
       * counter_value: 0 -> MY_LAST2 (24), reset ke 0,
       *                0 -> PARTNER_LAST2 (19), reset ke 0, dst.
       * Pantau variabel "counter_value" di Cube Monitor (Chart).
       * ---------------------------------------------------------- */
      case 2:
      {
        all_leds_off();

        uint32_t target = (counter_phase == 0) ? MY_LAST2 : PARTNER_LAST2;
        HAL_Delay(COUNTER_DELAY_MS);
        counter_value++;
        if (counter_value > target) {
          counter_value = 0;
          counter_phase ^= 1u;
        }
        break;
      }

      /* ----------------------------------------------------------
       * MODE 3: Potensiometer -> LED + Cube Monitor (Gauge)
       * ADC 12-bit (0-4095):
       *   adc = 0       -> 0 LED menyala
       *   adc >= 895    -> 8 LED menyala
       *   di antaranya  -> proporsional (LED1..LEDn menyala)
       * Pantau variabel "led_count" di Cube Monitor (Gauge, 0-8).
       * ---------------------------------------------------------- */
      case 3:
      {
        uint32_t adc = adc_dma_val;

        if (adc <= 10) {
          led_count = 0;
        } else if (adc >= 4000) {
          led_count = 8;
        } else {
          led_count = (adc * 8u) / 4095u;
          if (led_count == 0) led_count = 1; 
        }

        /* Konversi jumlah led_count menjadi bit pattern (00000001, 00000011, dst) */
        uint8_t pattern = (led_count >= 8) ? 0xFFu : (uint8_t)((1u << led_count) - 1u);
        set_leds(pattern);
        
        HAL_Delay(ADC_POLL_MS);
        break;
      }

      /* ----------------------------------------------------------
       * MODE 4: Dua kereta bertabrakan (panjang 3 LED masing-masing)
       * Kereta kiri  : mulai LED1..LED3, bergerak ke kanan.
       * Kereta kanan : mulai LED6..LED8, bergerak ke kiri.
       * Saat step-2 keduanya bertemu di tengah (tabrakan) -> flash
       * semua LED, lalu animasi diulang dari awal.
       * Potensiometer: ADC makin besar -> delay makin kecil -> makin cepat.
       *   ADC = 0    -> delay 500 ms (lambat)
       *   ADC = 4095 -> delay  50 ms (cepat)
       * ---------------------------------------------------------- */
      case 4:
      {
        /* 14-langkah animasi kereta: terbentuk -> bertabrakan -> berpencar
         *
         * step  0: ○○○○○○○○  semua mati (awal siklus)
         * step  1: ●○○○○○○●  1 LED tiap sisi
         * step  2: ●●○○○○●●  2 LED tiap sisi
         * step  3: ●●●○○●●●  3 LED tiap sisi (kereta terbentuk penuh)
         * step  4: ○●●●●●●○  bergerak ke tengah
         * step  5: ○○●●●●○○  semakin dekat
         * step  6: ○○○●●○○○  tabrakan — menyatu di tengah
         * step  7: ○○○○○○○○  semua mati (puncak tabrakan)
         * step  8: ○○○●●○○○  muncul kembali di tengah
         * step  9: ○○●●●●○○  berpencar keluar
         * step 10: ○●●●●●●○  semakin menjauh
         * step 11: ●●●○○●●●  kereta kiri tiba di kanan & sebaliknya
         * step 12: ●●○○○○●●  menghilang — 2 LED
         * step 13: ●○○○○○○●  menghilang — 1 LED
         */
        static const uint8_t pat[14] = {
            0x00, 0x81, 0xC3, 0xE7,  /* step  0- 3 */
            0x7E, 0x3C, 0x18, 0x00,  /* step  4- 7 */
            0x18, 0x3C, 0x7E, 0xE7,  /* step  8-11 */
            0xC3, 0x81               /* step 12-13 */
        };

        uint32_t adc      = adc_dma_val;
        uint32_t delay_ms = 500u - (adc * 450u / 4095u);  /* 500..50 ms */

        set_leds(pat[train_step]);
        HAL_Delay(delay_ms);

        train_step = (train_step + 1u) % 14u;
        break;
      }

      /* ----------------------------------------------------------
       * MODE 5: Binary Counter 0–255 + Potensiometer -> kecepatan
       * LED menampilkan nilai biner: bit-0 = LED1, bit-7 = LED8.
       *   0   (0b00000000) -> semua LED mati
       *   255 (0b11111111) -> semua LED nyala
       * Setelah 255, counter kembali ke 0.
       * Potensiometer: ADC makin besar -> delay makin kecil -> makin cepat.
       *   ADC = 0    -> delay 500 ms (lambat)
       *   ADC = 4095 -> delay  50 ms (cepat)
       * ---------------------------------------------------------- */
      case 5:
      {
        uint32_t adc      = adc_dma_val;
        uint32_t delay_ms = 500u - (adc * 450u / 4095u);  /* 500..50 ms */

        set_leds(binary_count);
        HAL_Delay(delay_ms);

        binary_count++;   /* uint8_t wraps 255 -> 0 secara otomatis */
        break;
      }

      /* ----------------------------------------------------------
       * MODE 6: Rhythm Tap Game (Fase 1)
       * Keluar dengan menekan BTN1+BTN2 bersamaan.
       * Double click BTN1 berpindah ke MODE 7.
       * ---------------------------------------------------------- */
      case 6:
        RhythmGame_Run();
        break;

      /* ----------------------------------------------------------
       * MODE 7: Charge & Release Game (Fase 2)
       * Double click BTN1 berpindah ke MODE 8.
       * Keluar dengan menekan BTN1+BTN2 bersamaan.
       * ---------------------------------------------------------- */
      case 7:
        ChargeGame_Run();
        break;

      /* ----------------------------------------------------------
       * MODE 8: Whack-a-LED Game (Fase 3)
       * Double click BTN1 berpindah ke MODE 9.
       * Keluar dengan menekan BTN1+BTN2 bersamaan.
       * ---------------------------------------------------------- */
      case 8:
        WhackGame_Run();
        break;

      /* ----------------------------------------------------------
       * MODE 9: Game Konversi Biner (Fase 4)
       * Keluar dengan menekan BTN1+BTN2 bersamaan.
       * ---------------------------------------------------------- */
      case 9:
        BinaryGame_Run();
        break;

      default:
        current_mode = 1;
        break;
    }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED8_Pin|LED4_Pin|LED3_Pin|LED2_Pin
                          |LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED7_Pin|LED6_Pin|LED5_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BTN1_Pin BTN2_Pin */
  GPIO_InitStruct.Pin = BTN1_Pin|BTN2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  /* button ke GND -> aktif LOW */
  GPIO_InitStruct.Pull = GPIO_PULLUP;            /* pull-up internal */
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED8_Pin LED4_Pin LED3_Pin LED2_Pin
                           LED1_Pin */
  GPIO_InitStruct.Pin = LED8_Pin|LED4_Pin|LED3_Pin|LED2_Pin
                          |LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : LED7_Pin LED6_Pin LED5_Pin */
  GPIO_InitStruct.Pin = LED7_Pin|LED6_Pin|LED5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * Callback EXTI (dipanggil oleh HAL_GPIO_EXTI_IRQHandler di stm32f4xx_it.c).
 *
 * Logika tekan bersamaan (simultaneous):
 *   Jika BTN1 ditekan dan BTN2 sudah ditekan dalam SIMULTANEOUS_MS ms terakhir
 *   (atau sebaliknya), set simultaneous_flag dan batalkan flag tombol tunggal.
 *   Ini memungkinkan toggle Mode Game tanpa salah terpicu sebagai mode-change.
 *
 * Di Mode Game (current_mode == MODE_GAME):
 *   BTN1 → RhythmGame_BTN1_Tap()   (rekam timestamp ketukan)
 *   BTN2 → RhythmGame_BTN2_Press() (set flag ulang demo / hint)
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  if (GPIO_Pin == BTN1_Pin) {
    if ((now - btn1_last_tick) > DEBOUNCE_MS) {
      uint32_t diff = now - btn1_last_tick;
      btn1_last_tick = now;

      if ((now - btn2_last_tick) < SIMULTANEOUS_MS) {
        /* BTN2 baru saja ditekan → tekan bersamaan */
        btn2_flag = 0;
        simultaneous_flag = 1;
      } else if (current_mode == 6) {
        if (diff < 400) {
            /* Double click pada mode 6 untuk pindah ke mode 7 */
            current_mode = 7;
            RhythmGame_Reset();
            all_leds_off();
            ChargeGame_Init();
            return;
        }
        RhythmGame_BTN1_Tap();
      } else if (current_mode == 7) {
        if (diff < 400) {
            /* Double click pada mode 7 untuk pindah ke mode 8 */
            current_mode = 8;
            ChargeGame_Reset();
            all_leds_off();
            WhackGame_Init();
            return;
        }
      } else if (current_mode == 8) {
        if (diff < 400) {
            /* Double click pada mode 8 untuk pindah ke mode 9 */
            current_mode = 9;
            WhackGame_Reset();
            all_leds_off();
            BinaryGame_Init();
            return;
        }
        WhackGame_BTN1_Press();
      } else if (current_mode == 9) {
        if (diff < 400) {
            /* Double click pada mode 9 untuk kembali ke mode 6 */
            current_mode = 6;
            BinaryGame_Reset();
            all_leds_off();
            RhythmGame_Init();
            return;
        }
        BinaryGame_BTN1_Press();
      } else {
        btn1_flag = 1;
      }
    }
  }

  if (GPIO_Pin == BTN2_Pin) {
    if ((now - btn2_last_tick) > DEBOUNCE_MS) {
      btn2_last_tick = now;

      if ((now - btn1_last_tick) < SIMULTANEOUS_MS) {
        /* BTN1 baru saja ditekan → tekan bersamaan */
        btn1_flag = 0;
        simultaneous_flag = 1;
      } else if (current_mode == 6) {
        RhythmGame_BTN2_Press();
      } else if (current_mode == 7) {
        ChargeGame_BTN2_Press();
      } else if (current_mode == 8) {
        WhackGame_BTN2_Press();
      } else if (current_mode == 9) {
        BinaryGame_BTN2_Press();
      } else {
        btn2_flag = 1;
      }
    }
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
