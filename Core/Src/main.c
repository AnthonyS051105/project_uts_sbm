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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
#include "rhythm_game.h"
#include "binary_game.h"
#include "dht.h"
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
#define MODE_GAME        8     /* Mode game — aktif via tekan BTN1+BTN2 bersamaan */
#define SIMULTANEOUS_MS  150   /* window waktu (ms) untuk deteksi tekan bersamaan */
#define MODE_LOBBY       10    /* Mode lobby pemilihan game */
#define SERVO_POT_MS     20    /* interval update servo dari potensiometer (ms)   */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

/* USER CODE BEGIN PV */
/* ---- kontrol mode ---- */
volatile uint8_t current_mode  = 1;   /* mode aktif: 1, 2, atau 3          */
volatile uint8_t btn1_flag     = 0;   /* set oleh EXTI BTN1                 */
volatile uint8_t btn2_flag     = 0;   /* set oleh EXTI BTN2                 */
uint32_t btn1_last_tick        = 0;
uint32_t btn2_last_tick        = 0;

/* ---- Variabel Kontrol Servo ---- */
uint32_t servo_last_tick = 0;         /* Menyimpan waktu update terakhir */
uint8_t servo_step = 0;               /* Menyimpan state/posisi servo saat ini */

/* ---- Sensor HC-SR04 Variabel ---- */
uint8_t  hcsr04_state      = 0;       /* 0: Idle, 1: Trig, 2: Wait Rising, 3: Wait Falling */
uint32_t hcsr04_last_tick  = 0;       /* Waktu polling terakhir */
uint16_t hcsr04_trig_start = 0;       /* Catatan waktu mulai trigger */
uint16_t hcsr04_ic_val1    = 0;       /* Capture nilai saat sinyal Echo naik */
uint16_t hcsr04_ic_val2    = 0;       /* Capture nilai saat sinyal Echo turun */
uint32_t distance_cm       = 0;       /* Hasil pembacaan jarak dalam cm */

/* ---- Mode 1: shift left ---- */
uint8_t shift_pos = 0;                /* posisi LED yang menyala (0-7)      */

/* ---- Mode 2: counter (dipantau Cube Monitor - Chart) ---- */
uint32_t counter_value = 0;           /* nilai counter saat ini             */
uint8_t  counter_phase = 0;           /* 0 = NIM sendiri, 1 = NIM partner   */

/* ---- Mode 3: ADC -> LED (dipantau Cube Monitor - Gauge) ---- */
volatile uint32_t adc_dma_val = 0;    /* buffer DMA hasil ADC               */
volatile uint32_t led_count   = 0;    /* jumlah LED menyala (0-8)           */
uint8_t current_led_mask      = 0;    /* snapshot bitmask LED aktif untuk JSON */

/* ---- Mode 4: dua kereta bertabrakan ---- */
uint8_t train_step = 0;               /* langkah animasi kereta (0-2)       */

/* ---- Mode 5: binary counter 0-255 ---- */
uint8_t binary_count = 0;            /* nilai counter biner saat ini (0-255) */

/* ---- Mode 6: LED pattern right shift ---- */
uint8_t mode6_step = 0;
uint8_t mode6_pattern = 0;

/* ---- Mode Game: deteksi tekan bersamaan ---- */
volatile uint8_t simultaneous_flag  = 0; /* set jika BTN1+BTN2 ditekan bersamaan */
volatile uint8_t game_lobby         = 0; /* 1=game1(rhythm) dipilih, 2=game2(binary) dipilih */
volatile uint8_t game_start_pending = 0; /* BTN2 double-click di lobby → mulai game */
volatile uint8_t btn2_first_click_pending = 0; /* defer single click btn2 di lobby */
volatile uint32_t btn2_first_click_tick   = 0; /* timestamp untuk btn2 defer */

/* ---- Sensor DHT11 ---- */
DHT_t    dht11;
float    dht11_temp     = 0.0f;   /* suhu terakhir (°C)  */
float    dht11_hum      = 0.0f;   /* kelembaban terakhir (%) */
uint32_t dht11_last_tick = 0;     /* timestamp pembacaan terakhir */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include <string.h>
#include <stdlib.h>

volatile uint8_t usb_cmd_ready = 0;
char usb_cmd_buffer[64];
volatile uint8_t web_force_led_mode = 0; /* 0=none, 1=all OFF, 2=all ON */

void Process_USB_Command(uint8_t* buf, uint32_t len) {
    static uint32_t idx = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            usb_cmd_buffer[idx] = '\0';
            if (idx > 0) usb_cmd_ready = 1;
            idx = 0;
        } else {
            if (idx < sizeof(usb_cmd_buffer) - 1) {
                usb_cmd_buffer[idx++] = buf[i];
            }
        }
    }
}

/* Override BinaryGame_UART_Transmit → kirim via USB CDC */
void BinaryGame_UART_Transmit(const char *buf, uint16_t len)
{
    CDC_Transmit_FS((uint8_t*)buf, len);
}

/* Tabel urutan LED 1-8 (LED8 sebagai LSB/kanan, LED1 sebagai MSB/kiri) */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } LED_t;
static const LED_t leds[8] = {
    {LED8_GPIO_Port, LED8_Pin}, /* bit 0 */
    {LED7_GPIO_Port, LED7_Pin}, /* bit 1 */
    {LED6_GPIO_Port, LED6_Pin}, /* bit 2 */
    {LED5_GPIO_Port, LED5_Pin}, /* bit 3 */
    {LED4_GPIO_Port, LED4_Pin}, /* bit 4 */
    {LED3_GPIO_Port, LED3_Pin}, /* bit 5 */
    {LED2_GPIO_Port, LED2_Pin}, /* bit 6 */
    {LED1_GPIO_Port, LED1_Pin}, /* bit 7 */
};

/**
 * Set LED berdasarkan bit-pattern 8-bit.
 * bit-0 = LED1, bit-7 = LED8.
 */
static void set_leds(uint8_t pattern)
{
    if (web_force_led_mode == 1) pattern = 0x00;
    else if (web_force_led_mode == 2) pattern = 0xFF;

    current_led_mask = pattern;
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
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
  HAL_Delay(100); // Beri jeda 100ms agar Windows sadar USB dicabut
  MX_USB_DEVICE_Init();
  MX_TIM4_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  /* Mulai ADC secara continuous via DMA; adc_dma_val diperbarui otomatis */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&adc_dma_val, 1);

  /* PAKSA INISIALISASI PIN PB8 UNTUK PWM TIM4 (ALTERNATE FUNCTION 2) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;       // Mode Alternate Function Push Pull
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM4;    // Hubungkan pin ini ke TIM4
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PAKSA INISIALISASI PIN PA6 UNTUK ECHO HC-SR04 (TIM3 CH1 - AF2) */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStructEcho = {0};
  GPIO_InitStructEcho.Pin = GPIO_PIN_6;
  GPIO_InitStructEcho.Mode = GPIO_MODE_AF_PP;    // Mode Alternate Function
  GPIO_InitStructEcho.Pull = GPIO_NOPULL;
  GPIO_InitStructEcho.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStructEcho.Alternate = GPIO_AF2_TIM3; // Hubungkan ke TIM3
  HAL_GPIO_Init(GPIOA, &GPIO_InitStructEcho);

  /* Mulai PWM untuk Servo di TIM4 Channel 3 */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

  /* Mulai timer 3 untuk HC-SR04 (1 MHz = 1 tick per 1 mikrodetik) */
  HAL_TIM_Base_Start(&htim3);
  HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1); // Polling mode untuk baca Echo

  /* ---- Init DHT11 ----
   * Pin  : PB0 (DHT11_Pin / DHT11_GPIO_Port, sudah terdefinisi di main.h)
   * Timer: TIM2 dikonfigurasi library ke 1 MHz (bus 16 MHz, prescaler = 15)
   * Library menunggu hingga HAL_GetTick() >= 2000 ms sebagai warm-up DHT. */
  HAL_TIM_Base_Start(&htim2);
  DHT_init(&dht11, DHT_Type_DHT11, &htim2, 84, DHT11_GPIO_Port, DHT11_Pin);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t current_tick = HAL_GetTick();

    /* ============================================================
     * Proses USB Command dari Website/Python
     * ============================================================ */
    if (usb_cmd_ready) {
      usb_cmd_ready = 0;
      if (strncmp(usb_cmd_buffer, "MODE:", 5) == 0) {
        uint8_t m = atoi(&usb_cmd_buffer[5]);
        if (m >= 1 && m <= 9) {
          current_mode = m;
          shift_pos     = 0;
          counter_value = 0;
          counter_phase = 0;
          train_step    = 0;
          binary_count  = 0;
          mode6_step    = 0;
          mode6_pattern = 0;
          web_force_led_mode = 0; /* Kembalikan kontrol LED ke per-mode */
          
          if (m == 8) {
            game_lobby = 1;
            game_start_pending = 1; // langsung start rhythm game
          } else if (m == 9) {
            game_lobby = 2;
            game_start_pending = 1; // langsung start binary game
          } else {
            game_lobby = 0;
            game_start_pending = 0;
          }
          all_leds_off(); // panggil ini agar mereset current_led_mask sesuai override (0x00)
        }
      } else if (strcmp(usb_cmd_buffer, "ISR") == 0) {
        web_force_led_mode = 0; // Kembalikan kontrol LED statis saat ISR!
        btn2_flag = 1;
      } else if (strcmp(usb_cmd_buffer, "LED:FF") == 0) {
        web_force_led_mode = 2; // Paksa semua LED NYALA
        all_leds_on();
      } else if (strcmp(usb_cmd_buffer, "LED:00") == 0) {
        web_force_led_mode = 1; // Paksa semua LED MATI
        all_leds_off();
      } else if (strcmp(usb_cmd_buffer, "RESET") == 0) {
        RhythmGame_Reset();
        BinaryGame_Reset();
      }
    }

    /* ============================================================
     * Kontrol Servo SG90 otomatis (0° -> 90° -> 180°) — Mode 1–6
     * Non-blocking, jeda 1 detik antar posisi.
     * ============================================================ */
    if (current_mode < 7) {
      if ((current_tick - servo_last_tick) >= 1000) {
        servo_last_tick = current_tick;
        switch (servo_step) {
          case 0: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 500);  servo_step = 1; break;
          case 1: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 1500); servo_step = 2; break;
          case 2: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 2500); servo_step = 0; break;
        }
      }
    }

    /* USER CODE BEGIN 3 */

    /* ============================================================
     * Sensor HC-SR04 (Non-Blocking Polling dengan State Machine)
     * ============================================================ */
    switch (hcsr04_state) {
      case 0: /* IDLE: Tunggu 100ms untuk pembacaan berikutnya */
        if ((current_tick - hcsr04_last_tick) >= 150) {
          hcsr04_last_tick = current_tick;

          /* Berikan sinyal HIGH ke pin Trigger */
          HAL_GPIO_WritePin(GPIOA, TRIG_PIN_Pin, GPIO_PIN_SET);
          hcsr04_trig_start = __HAL_TIM_GET_COUNTER(&htim3);
          hcsr04_state = 1;
        }
        break;

      case 1: /* TRIGGERING: Tahan Trigger tetap HIGH minimal 10us */
        if ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - hcsr04_trig_start) >= 10) {
          /* Turunkan sinyal Trigger menjadi LOW */
          HAL_GPIO_WritePin(GPIOA, TRIG_PIN_Pin, GPIO_PIN_RESET);

          /* Bersihkan flag Capture dan atur agar sensitif terhadap sinyal NAIK (Rising Edge) */
          __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);
          __HAL_TIM_SET_CAPTUREPOLARITY(&htim3, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
          hcsr04_state = 2;
        }
        break;

      case 2: /* WAIT RISING: Tunggu sinyal Echo menjadi HIGH dari sensor */
        if (__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_CC1)) {
          /* Simpan waktu saat sinyal mulai HIGH */
          hcsr04_ic_val1 = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);

          /* Ubah sensitivitas untuk membaca sinyal TURUN (Falling Edge) */
          __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);
          __HAL_TIM_SET_CAPTUREPOLARITY(&htim3, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_FALLING);
          hcsr04_state = 3;
        }
        /* Timeout Protection (~50ms) jika terjadi error (misal kabel lepas) */
        else if ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - hcsr04_trig_start) > 50000) {
          hcsr04_state = 0;
        }
        break;

      case 3: /* WAIT FALLING: Tunggu sinyal Echo kembali LOW */
        if (__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_CC1)) {
          hcsr04_ic_val2 = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);
          
          uint32_t diff = (hcsr04_ic_val2 > hcsr04_ic_val1) ? 
                          (hcsr04_ic_val2 - hcsr04_ic_val1) : 
                          (0xFFFF - hcsr04_ic_val1 + hcsr04_ic_val2);
          
          uint32_t raw_dist = diff / 58;
          if (raw_dist > 400) raw_dist = 400;

          /* Filter yang lebih agresif untuk menahan drift */
          static uint32_t f_dist = 0;
          if (f_dist == 0) f_dist = raw_dist;
          
          // Menggunakan pembagian bit shift (>>2) atau (>>3) lebih cepat dari pembagian biasa
          // Rumus: 90% lama + 10% baru
          f_dist = ((f_dist * 9) + raw_dist) / 10;
          
          distance_cm = f_dist;
          hcsr04_state = 0;
        }
        /* Timeout Protection jika sensor out of range */
        else if ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - hcsr04_ic_val1) > 30000) {
          hcsr04_state = 0;
        }
        break;
    }

    /* ============================================================
     * BTN1 + BTN2 bersamaan: toggle Mode Game
     * ============================================================ */
    if (simultaneous_flag) {
      simultaneous_flag = 0;
      if (current_mode >= 8) {
        /* Keluar dari mode game/lobby -> kembali ke Mode 1 */
        current_mode       = 1;
        game_lobby         = 0;
        game_start_pending = 0;
        shift_pos     = 0;
        counter_value = 0;
        counter_phase = 0;
        train_step    = 0;
        binary_count  = 0;
        mode6_step    = 0;
        mode6_pattern = 0;
        web_force_led_mode = 0;
        RhythmGame_Reset();
        BinaryGame_Reset();
        all_leds_off();
      } else {
        /* Masuk ke lobby: belum ada game yang dipilih, semua LED padam */
        current_mode = MODE_LOBBY;
        game_lobby   = 0;
        btn2_first_click_pending = 0;
        web_force_led_mode = 0;
        all_leds_off();
      }
    }

    /* ============================================================
     * BTN2 (INTERRUPT): semua LED nyala 5 detik, lalu kembali ke
     * mode yang sama sebelum interrupt secara otomatis.
     * Hanya aktif di mode 1–6, TIDAK di mode game.
     * ============================================================ */
    if (btn2_flag && current_mode < 8) {
      btn2_flag = 0;
      uint8_t prev_force = web_force_led_mode;
      web_force_led_mode = 0;
      all_leds_on();
      HAL_Delay(5000);
      all_leds_off();
      web_force_led_mode = prev_force; /* Kembalikan ke mode statis jika sebelumnya statis */
    }

    /* ============================================================
     * BTN1: ganti mode  1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 1 -> ...
     * Hanya aktif di mode 1–6, TIDAK di mode game.
     * ============================================================ */
    if (btn1_flag && current_mode < 8) {
      btn1_flag    = 0;
      current_mode = (current_mode % 7) + 1;
      shift_pos     = 0;
      counter_value = 0;
      counter_phase = 0;
      train_step    = 0;
      binary_count  = 0;
      mode6_step    = 0;
      mode6_pattern = 0;
      web_force_led_mode = 0;
      all_leds_off();
    }

    /* ============================================================
     * Timeout pendeteksi tekan BTN2 sekali untuk memilih Binary Game
     * ============================================================ */
    if (current_mode == MODE_LOBBY && btn2_first_click_pending) {
      if (HAL_GetTick() - btn2_first_click_tick > 400) {
        btn2_first_click_pending = 0;
        game_lobby = 2;
        all_leds_off();
        HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_SET);
      }
    }

    /* ============================================================
     * Mulai game: countdown LED9→LED10→LED11 ("3,2,1") + buzzer,
     * lalu jalankan game yang dipilih di lobby.
     * ============================================================ */
    if (game_start_pending) {
      game_start_pending = 0;
      uint8_t sel = game_lobby;
      /* Set mode sebelum countdown agar UI React mulai render & hitung mundur bareng MCU */
      current_mode = (sel == 1) ? 8 : 9;
      game_lobby = 0;
      all_leds_off();
      
      // Kirim satu frame JSON langsung ke USB / serial (force) agar UI bisa mulai countdown 3..2..1
      {
        char buffer[230];
        uint32_t c_score = (sel == 2) ? BinaryGame_GetScore() : rg.score;
        uint8_t c_lives  = (sel == 2) ? 0 : rg.lives;

        uint16_t len = snprintf(buffer, sizeof(buffer),
          "{\"mode\":%d,\"gameLobby\":%d,\"adc\":%lu,\"counter\":%lu,\"ledMask\":%d,\"dist\":%lu,\"temp\":%d,\"hum\":%d,\"score\":%lu,\"lives\":%d,\"binaryTarget\":%d,\"binaryRound\":%d,\"rhythmLevel\":%d}\r\n",
          current_mode, game_lobby, adc_dma_val, counter_value, current_led_mask, distance_cm,
          (int)dht11_temp, (int)dht11_hum, c_score, (int)c_lives, (int)BinaryGame_GetTarget(), (int)BinaryGame_GetRound(), (int)RhythmGame_GetLevel());
        CDC_Transmit_FS((uint8_t*)buffer, len);
      }

      /* Countdown "3" — LED11 menyala, satu beep */
      HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      HAL_Delay(300);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
      HAL_Delay(700);
      HAL_GPIO_WritePin(LED11_GPIO_Port, LED11_Pin, GPIO_PIN_RESET);

      /* Countdown "2" — LED10 menyala, satu beep */
      HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      HAL_Delay(300);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
      HAL_Delay(700);
      HAL_GPIO_WritePin(LED10_GPIO_Port, LED10_Pin, GPIO_PIN_RESET);

      /* Countdown "1" — LED9 menyala, satu beep */
      HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      HAL_Delay(300);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
      HAL_Delay(700);
      HAL_GPIO_WritePin(LED9_GPIO_Port, LED9_Pin, GPIO_PIN_RESET);

      all_leds_off();

      /* Inisialisasi game yang dipilih */
      if (sel == 1) {
        RhythmGame_Init();
      } else {
        BinaryGame_Init();
      }
    }

    /* ============================================================
     * Eksekusi mode aktif (NON-BLOCKING)
     * ============================================================ */
    static uint32_t mode_last_tick = 0;
    
    switch (current_mode)
    {
      /* ----------------------------------------------------------
       * MODE 1: Shift Left
       * Satu LED menyala bergeser dari LED1 -> LED2 -> ... -> LED8
       * lalu kembali ke LED1.
       * ---------------------------------------------------------- */
      case 1:
      {
        if (current_tick - mode_last_tick >= SHIFT_DELAY_MS) {
          mode_last_tick = current_tick;
          if (shift_pos < 8) set_leds((uint8_t)(1u << shift_pos));
          else set_leds(0x00);
          shift_pos = (shift_pos + 1) % 9;
        }
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
        if (current_tick - mode_last_tick >= COUNTER_DELAY_MS) {
          mode_last_tick = current_tick;
          all_leds_off();
          uint32_t target = (counter_phase == 0) ? MY_LAST2 : PARTNER_LAST2;
          counter_value++;
          if (counter_value > target) {
            counter_value = 0;
            counter_phase ^= 1u;
          }
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
        if (current_tick - mode_last_tick >= ADC_POLL_MS) {
          mode_last_tick = current_tick;
          static uint32_t filtered_adc = 0;
          filtered_adc = (filtered_adc * 7 + adc_dma_val) / 8;
          uint32_t logical_adc = 4095 - filtered_adc;
          if (logical_adc <= 100) led_count = 0;
          else if (logical_adc >= 4000) led_count = 8;
          else {
            led_count = (logical_adc * 8u) / 4095u;
            if (led_count == 0) led_count = 1; 
          }
          uint8_t pattern = (led_count == 0) ? 0x00 : (uint8_t)(0xFF00u >> led_count);
          set_leds(pattern);
        }
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
        uint32_t adc = adc_dma_val;
        uint32_t delay_ms = 50u + (adc * 450u / 4095u);
        if (current_tick - mode_last_tick >= delay_ms) {
          mode_last_tick = current_tick;
          static const uint8_t pat[14] = {
              0x00, 0x81, 0xC3, 0xE7, 0x7E, 0x3C, 0x18, 0x00,
              0x18, 0x3C, 0x7E, 0xE7, 0xC3, 0x81
          };
          set_leds(pat[train_step]);
          train_step = (train_step + 1u) % 14u;
        }
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
        uint32_t adc = adc_dma_val;
        uint32_t delay_ms = 50u + (adc * 450u / 4095u);
        if (current_tick - mode_last_tick >= delay_ms) {
          mode_last_tick = current_tick;
          set_leds(binary_count);
          binary_count++;
        }
        break;
      }

      /* ----------------------------------------------------------
       * MODE 6: LED Menyala Selang 2, Bergeser ke Kanan
       * Visual:
       * ●○○○○○○○ -> ○●○○○○○○ -> ○○●○○○○○ -> ●○○●○○○○ ...
       * (di mana bit-0 = LED1, bit-7 = LED8)
       * ---------------------------------------------------------- */
      case 6:
      {
        uint32_t adc = adc_dma_val;
        uint32_t delay_ms = 50u + (adc * 450u / 4095u);
        if (current_tick - mode_last_tick >= delay_ms) {
          mode_last_tick = current_tick;
          if (mode6_step == 0) mode6_pattern = (mode6_pattern >> 1) | 0x80u;
          else mode6_pattern = (mode6_pattern >> 1);
          set_leds(mode6_pattern);
          mode6_step = (mode6_step + 1) % 3;
        }
        break;
      }

      /* ----------------------------------------------------------
       * MODE 7: Kontrol Servo via Potensiometer
       * ADC 12-bit (0–4095) dipetakan ke pulsa PWM servo:
       *   ADC = 0    -> 500  µs (~0°)
       *   ADC = 2047 -> 1500 µs (~90°)
       *   ADC = 4095 -> 2500 µs (~180°)
       * LED bar menampilkan posisi servo (0–8 LED menyala).
       * ---------------------------------------------------------- */
      case 7:
      {
        if (current_tick - mode_last_tick >= SERVO_POT_MS) {
          mode_last_tick = current_tick;

          /* Filter IIR ringan untuk meredam noise ADC */
          static uint32_t servo_adc_filtered = 0;
          servo_adc_filtered = (servo_adc_filtered * 7 + adc_dma_val) / 8;

          /* Peta ADC 0–4095 ke pulse width 500–2500 µs */
          uint32_t pulse = 500u + (servo_adc_filtered * 2000u / 4095u);
          __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse);

          /* LED bar: 0 LED (ADC=0) hingga 8 LED (ADC=4095) */
          uint8_t led_bar = (uint8_t)(servo_adc_filtered * 8u / 4095u);
          uint8_t pattern = (led_bar == 0) ? 0x00u : (uint8_t)(0xFF00u >> led_bar);
          set_leds(pattern);
        }
        break;
      }

      /* ----------------------------------------------------------
       * MODE 8: Rhythm Tap Game
       * Keluar manual : BTN1+BTN2 bersamaan → Mode 1.
       * Keluar otomatis: setelah GAME_OVER → kembali ke lobby (Mode 10).
       * ---------------------------------------------------------- */
      case 8:
        if (rg_session_done) {
          rg_session_done = 0;
          /* Kembali ke lobby dengan pilihan rhythm (LED8 menyala) */
          current_mode = MODE_LOBBY;
          game_lobby   = 1;
          all_leds_off();
          HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_SET);
        } else {
          RhythmGame_Run();
        }
        break;

      /* ----------------------------------------------------------
       * MODE 9: Game Konversi Biner
       * Keluar manual : BTN1+BTN2 bersamaan → Mode 1.
       * Keluar otomatis: setelah BG_MAX_ROUNDS soal → kembali ke lobby (Mode 10).
       * ---------------------------------------------------------- */
      case 9:
        if (bg_session_done) {
          bg_session_done = 0;
          /* Kembali ke lobby dengan pilihan binary (LED7+LED8 menyala) */
          current_mode = MODE_LOBBY;
          game_lobby   = 2;
          all_leds_off();
          HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);
          HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_SET);
        } else {
          BinaryGame_Run();
        }
        break;

      /* ----------------------------------------------------------
       * MODE 10: Lobby pemilihan game
       * LED8 menyala (game 1) atau LED7+LED8 menyala (game 2).
       * LED sudah diatur saat masuk/berganti mode — tidak ada aksi
       * di loop, cukup tunggu input user.
       * ---------------------------------------------------------- */
      case 10:
        break;

      default:
        current_mode = 1;
        break;
    }

    /* ============================================================
     * Baca DHT11 setiap 2000ms (non-blocking menggunakan HAL_GetTick)
     * DHT_readData mengirim start-signal lalu menunggu data lewat EXTI.
     * ============================================================ */
    if ((current_tick - dht11_last_tick) >= 2000) {
      dht11_last_tick = current_tick;
      DHT_readData(&dht11, &dht11_temp, &dht11_hum);
    }

    /* ============================================================
     * Kirim data ke USB CDC tiap 200ms
     * ============================================================ */
    static uint32_t usb_last_tick = 0;
    if (current_tick - usb_last_tick >= 200) {
      usb_last_tick = current_tick;

      // Siapkan buffer untuk semua data termasuk DHT11
      char buffer[220];

      // Format JSON — field names sesuai dengan STM32Data di dashboard
      // Gunakan %d (integer) agar tidak perlu linker flag -u _printf_float
      uint32_t current_score = (current_mode == 9 || game_lobby == 2) ? BinaryGame_GetScore() : rg.score;
      uint8_t current_lives  = (current_mode == 9 || game_lobby == 2) ? 0 : rg.lives;

      uint16_t len = snprintf(buffer, sizeof(buffer),
        "{\"mode\":%d,\"gameLobby\":%d,\"adc\":%lu,\"counter\":%lu,\"ledMask\":%d,\"dist\":%lu,\"temp\":%d,\"hum\":%d,\"score\":%lu,\"lives\":%d,\"binaryTarget\":%d,\"binaryRound\":%d,\"rhythmLevel\":%d}\r\n",
        current_mode,
        game_lobby,
        adc_dma_val,
        counter_value,
        current_led_mask,
        distance_cm,
        (int)dht11_temp,
        (int)dht11_hum,
        current_score,
        (int)current_lives,
        (int)BinaryGame_GetTarget(),
        (int)BinaryGame_GetRound(),
        (int)RhythmGame_GetLevel());

      // Kirim data menggunakan USB CDC
      CDC_Transmit_FS((uint8_t*)buffer, len);
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
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
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 83;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 83;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 83;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 19999;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, TRIG_PIN_Pin|BUZZER_Pin|LED7_Pin|LED6_Pin
                          |LED5_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED11_Pin|LED10_Pin|LED9_Pin|LED8_Pin
                          |LED4_Pin|LED3_Pin|LED2_Pin|LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BTN1_Pin BTN2_Pin */
  GPIO_InitStruct.Pin = BTN1_Pin|BTN2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : TRIG_PIN_Pin BUZZER_Pin LED7_Pin LED6_Pin
                           LED5_Pin */
  GPIO_InitStruct.Pin = TRIG_PIN_Pin|BUZZER_Pin|LED7_Pin|LED6_Pin
                          |LED5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : DHT11_Pin */
  GPIO_InitStruct.Pin = DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED11_Pin LED10_Pin LED9_Pin LED8_Pin
                           LED4_Pin LED3_Pin LED2_Pin LED1_Pin */
  GPIO_InitStruct.Pin = LED11_Pin|LED10_Pin|LED9_Pin|LED8_Pin
                          |LED4_Pin|LED3_Pin|LED2_Pin|LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

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
 * Di Mode Lobby (current_mode == MODE_LOBBY):
 *   BTN1          → pilih Rhythm Tap  (LED8 menyala)
 *   BTN2 (slow)   → pilih Tebak Biner (LED7+LED8 menyala), hanya jika belum ada pilihan
 *   BTN2×2 (<400ms) → mulai game yang dipilih (game_start_pending = 1)
 *
 * Di Mode Game (current_mode == 8 / Rhythm):
 *   BTN1 → RhythmGame_BTN1_Tap()   (rekam timestamp ketukan)
 *   BTN2 → RhythmGame_BTN2_Press() (set flag ulang demo / hint)
 *
 * Di Mode Game (current_mode == 9 / Binary):
 *   BTN1 → BinaryGame_BTN1_Press() (input bit '1')
 *   BTN2 → BinaryGame_BTN2_Press() (input bit '0')
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  if(GPIO_Pin == DHT11_Pin){
	  DHT_pinChangeCallBack(&dht11);
  }

  if (GPIO_Pin == BTN1_Pin) {
    if ((now - btn1_last_tick) > DEBOUNCE_MS) {
      btn1_last_tick = now;

      if ((now - btn2_last_tick) < SIMULTANEOUS_MS) {
        /* BTN2 baru saja ditekan → tekan bersamaan */
        btn2_flag = 0;
        simultaneous_flag = 1;
      } else if (current_mode == MODE_LOBBY) {
        /* Single click: pilih Rhythm Tap → LED8 menyala */
        game_lobby = 1;
        btn2_first_click_pending = 0;
        all_leds_off();
        HAL_GPIO_WritePin(LED8_GPIO_Port, LED8_Pin, GPIO_PIN_SET);
      } else if (current_mode == 8 && !rg_session_done) {
        RhythmGame_BTN1_Tap();
      } else if (current_mode == 9 && !bg_session_done) {
        BinaryGame_BTN1_Press();
      } else {
        btn1_flag = 1;
      }
    }
  }

  if (GPIO_Pin == BTN2_Pin) {
    if ((now - btn2_last_tick) > DEBOUNCE_MS) {
      uint32_t diff = now - btn2_last_tick;
      btn2_last_tick = now;

      if ((now - btn1_last_tick) < SIMULTANEOUS_MS) {
        /* BTN1 baru saja ditekan → tekan bersamaan */
        btn1_flag = 0;
        simultaneous_flag = 1;
      } else if (current_mode == MODE_LOBBY) {
        if (diff < 400 && btn2_first_click_pending) {
          /* Double click: mulai game yang dipilih (jika sudah ada pilihan) */
          btn2_first_click_pending = 0;
          if (game_lobby != 0) {
            game_start_pending = 1;
          }
        } else {
          /* Single click: tahan dulu (defer) selama 400ms */
          btn2_first_click_pending = 1;
          btn2_first_click_tick = now;
        }
      } else if (current_mode == 8 && !rg_session_done) {
        RhythmGame_BTN2_Press();
      } else if (current_mode == 9 && !bg_session_done) {
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
