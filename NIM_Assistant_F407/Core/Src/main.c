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
#include "audio_clip.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MIC_LOOPBACK_CHANNEL 0U
#define LOOPBACK_GAIN 32
#define LOOPBACK_DC_SHIFT 8U
#define LOOPBACK_OUTPUT_LIMIT 900
#define LOOPBACK_LPF_SHIFT 4U
#define LOOPBACK_NOISE_GATE 180
#define MIC_INVALID_MAGNITUDE 500000U
#define LOOPBACK_SPEAKER_ENABLE 0U
#define RECORD_SAMPLE_COUNT 8000U
#define RECORD_GAIN 12
#define RECORD_NOISE_GATE 80
#define RECORD_TEST_TONE 0U  /* 1=fill with test tone, 0=record from mic */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
I2S_HandleTypeDef hi2s2;

/* USER CODE BEGIN PV */
static uint8_t i2s2_ready = 0;
static uint8_t audio_tx_phase = 0;
static uint8_t clip_playing = 0;
static uint32_t clip_index = 0;
static uint8_t record_active = 0;
static uint8_t record_playing = 0;
static uint32_t record_index = 0;
static uint32_t record_play_index = 0;
static volatile uint8_t record_done_pending = 0;
static char record_done_msg[192];
static uint32_t record_invalid_count = 0;
static int32_t record_dc_estimate = 0;
static int32_t record_lpf = 0;
static int16_t record_buffer[RECORD_SAMPLE_COUNT];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2S2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void Test_SendStatus(const char *message);
static void Test_SendUsbStatus(const char *message);
static void Test_SendPingIfDue(uint32_t *last_ping_tick);
static void Test_HandleUartRx(void);
static void Test_StartAudioClip(void);
static void Test_StartMicRecord(void);
static void Test_CancelMicRecordPlayback(void);
static void Test_ServiceAudioLoopback(uint8_t mic_diagnostic_enabled, uint32_t *last_report_tick);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Test_SendStatus(const char *message) {
  HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), 10);
  Test_SendUsbStatus(message);
}

static void Test_SendUsbStatus(const char *message) {
  uint8_t retries = 3;

  while ((CDC_Transmit_FS((uint8_t *)message, strlen(message)) == USBD_BUSY) && (retries > 0U)) {
    HAL_Delay(1);
    retries--;
  }
}

static void Test_SendPingIfDue(uint32_t *last_ping_tick) {
  const char ping[] = "PING\r\n";

  if ((HAL_GetTick() - *last_ping_tick) >= 1000U) {
    *last_ping_tick = HAL_GetTick();
    HAL_UART_Transmit(&huart1, (uint8_t *)ping, strlen(ping), 10);
  }
}

static void Test_HandleUartRx(void) {
  static char line_buffer[16];
  static uint8_t line_index = 0;
  uint8_t rx_byte;

  if (HAL_UART_Receive(&huart1, &rx_byte, 1, 0) == HAL_OK) {
    if ((rx_byte == '\r') || (rx_byte == '\n')) {
      if (line_index > 0U) {
        line_buffer[line_index] = '\0';
        if (strcmp(line_buffer, "PONG") == 0) {
          Test_SendStatus("ESP32 PONG OK\r\n");
        }
        line_index = 0;
      }
    } else if (line_index < (sizeof(line_buffer) - 1U)) {
      line_buffer[line_index++] = (char)rx_byte;
    } else {
      line_index = 0;
    }
  }
}

static void Test_StartAudioClip(void) {
  if (i2s2_ready == 0U) {
    Test_SendUsbStatus("Audio clip disabled: I2S init failed\r\n");
    return;
  }

  record_active = 0U;
  record_playing = 0U;
  clip_index = 0U;
  clip_playing = 1U;
  audio_tx_phase = 0U;
  Test_SendStatus("K0 pressed: Audio clip playback\r\n");
}

static void Test_StartMicRecord(void) {
  if (i2s2_ready == 0U) {
    Test_SendUsbStatus("Mic record disabled: I2S init failed\r\n");
    return;
  }

  clip_playing = 0U;
  record_index = 0U;
  record_play_index = 0U;
  record_invalid_count = 0U;
  record_dc_estimate = 0;
  record_lpf = 0;
  record_done_pending = 0U;
  audio_tx_phase = 0U;

  if (RECORD_TEST_TONE != 0U) {
    /* Fill buffer with a ~400 Hz triangle wave for playback-path testing */
    const uint32_t period = 40U; /* 16000/400 = 40 samples per cycle */
    const int16_t amplitude = 8000;
    for (uint32_t i = 0U; i < RECORD_SAMPLE_COUNT; i++) {
      uint32_t phase = i % period;
      int16_t val;
      if (phase < (period / 2U)) {
        val = (int16_t)(-amplitude + (int32_t)(phase * 2U * amplitude / (period / 2U)));
      } else {
        val = (int16_t)(amplitude - (int32_t)((phase - period / 2U) * 2U * amplitude / (period / 2U)));
      }
      record_buffer[i] = val;
    }
    record_active = 0U;
    record_playing = 1U;
    Test_SendStatus("K1 pressed: TEST TONE playback\r\n");
  } else {
    record_active = 1U;
    record_playing = 0U;
    Test_SendStatus("K1 pressed: mic record start\r\n");
  }
}

static void Test_CancelMicRecordPlayback(void) {
  record_active = 0U;
  record_playing = 0U;
  record_index = 0U;
  record_play_index = 0U;
  Test_SendStatus("K1 pressed: record/playback cancel\r\n");
}

/*
 * Keep I2S TX fed so BCLK/WS stay alive, read the target mic channel, and
 * optionally record it into RAM before playback.
 */
static void Test_ServiceAudioLoopback(uint8_t mic_diagnostic_enabled, uint32_t *last_report_tick) {
  static uint16_t mic_high_word[2] = {0U, 0U};
  static uint8_t mic_got_high[2] = {0U, 0U};
  static int16_t output_sample = 0;
  static uint64_t channel_sum[2] = {0U, 0U};
  static uint32_t channel_peak[2] = {0U, 0U};
  static uint32_t channel_valid[2] = {0U, 0U};
  static uint32_t raw_peak[2] = {0U, 0U};
  static uint32_t output_peak = 0;
  static uint32_t count = 0;
  static uint32_t ovr_count = 0;
  static int32_t dc_estimate = 0;
  static int32_t output_filter = 0;
  char message[192];

  if (i2s2_ready == 0U) {
    return;
  }

  if (mic_diagnostic_enabled == 0U) {
    output_sample = 0;
    output_filter = 0;
    dc_estimate = 0;
    channel_sum[0] = 0U;
    channel_sum[1] = 0U;
    channel_peak[0] = 0U;
    channel_peak[1] = 0U;
    channel_valid[0] = 0U;
    channel_valid[1] = 0U;
    raw_peak[0] = 0U;
    raw_peak[1] = 0U;
    output_peak = 0;
    count = 0;
    ovr_count = 0;
    *last_report_tick = HAL_GetTick();
  }

  for (uint8_t rx_budget = 0U; rx_budget < 16U; rx_budget++) {
    uint16_t sr = (uint16_t)(I2SxEXT(hi2s2.Instance)->SR);

    if (sr & I2S_FLAG_OVR) {
      __HAL_I2SEXT_CLEAR_OVRFLAG(&hi2s2);
      mic_got_high[0] = 0U;
      mic_got_high[1] = 0U;
      output_sample = 0;
      output_filter = 0;
      ovr_count++;
      continue;
    }

    if ((sr & I2S_FLAG_RXNE) == 0U) {
      break;
    }

    uint16_t dr = (uint16_t)(I2SxEXT(hi2s2.Instance)->DR);
    uint8_t channel = (sr & I2S_FLAG_CHSIDE) ? 1U : 0U;

    if (mic_got_high[channel] == 0U) {
      mic_high_word[channel] = dr;
      mic_got_high[channel] = 1U;
    } else {
      uint32_t raw = ((uint32_t)mic_high_word[channel] << 16) | dr;
      int32_t sample = ((int32_t)raw) >> 8;
      int32_t scaled = 0;
      uint32_t magnitude = (sample < 0) ? (uint32_t)(-sample) : (uint32_t)sample;
      uint32_t output_magnitude;
      uint8_t valid_sample = (magnitude < MIC_INVALID_MAGNITUDE) ? 1U : 0U;

      if (magnitude > raw_peak[channel]) {
        raw_peak[channel] = magnitude;
      }

      if ((mic_diagnostic_enabled != 0U) && (valid_sample != 0U)) {
        channel_sum[channel] += magnitude;
        if (magnitude > channel_peak[channel]) {
          channel_peak[channel] = magnitude;
        }
        channel_valid[channel]++;
      }

      if ((record_active != 0U) && (channel == MIC_LOOPBACK_CHANNEL)) {
        int32_t pcm = 0;

        if (valid_sample != 0U) {
          record_dc_estimate += (sample - record_dc_estimate) >> LOOPBACK_DC_SHIFT;
          int32_t pcm_raw = ((sample - record_dc_estimate) >> 8) * RECORD_GAIN;
          /* IIR low-pass filter: smooths noise spikes, alpha ~0.125 */
          record_lpf += (pcm_raw - record_lpf) >> 3;
        } else {
          /* Decay toward zero for invalid samples instead of hard cut */
          record_lpf -= record_lpf >> 3;
          record_invalid_count++;
        }

        pcm = record_lpf;
        if (pcm > 32767) {
          pcm = 32767;
        } else if (pcm < -32768) {
          pcm = -32768;
        }

        /* Noise gate: kill low-level background hiss */
        if ((pcm < RECORD_NOISE_GATE) && (pcm > -RECORD_NOISE_GATE)) {
          pcm = 0;
        }

        record_buffer[record_index] = (int16_t)pcm;
        record_index++;
        if (record_index >= RECORD_SAMPLE_COUNT) {
          uint32_t lavg_done = (channel_valid[0] > 0U) ? (uint32_t)(channel_sum[0] / channel_valid[0]) : 0U;
          uint32_t ravg_done = (channel_valid[1] > 0U) ? (uint32_t)(channel_sum[1] / channel_valid[1]) : 0U;
          record_active = 0U;
          record_playing = 1U;
          record_play_index = 0U;
          audio_tx_phase = 0U;
          snprintf(record_done_msg, sizeof(record_done_msg),
                   "Mic record done: playback start inv:%lu Lavg:%lu Lpk:%lu Ravg:%lu Rpk:%lu ovr:%lu\r\n",
                   (unsigned long)record_invalid_count,
                   (unsigned long)lavg_done,
                   (unsigned long)channel_peak[0],
                   (unsigned long)ravg_done,
                   (unsigned long)channel_peak[1],
                   (unsigned long)ovr_count);
          record_done_pending = 1U;
        }
      }

      if ((mic_diagnostic_enabled != 0U) &&
          (channel == MIC_LOOPBACK_CHANNEL) &&
          (valid_sample != 0U) &&
          (LOOPBACK_SPEAKER_ENABLE != 0U)) {
        dc_estimate += (sample - dc_estimate) >> LOOPBACK_DC_SHIFT;
        scaled = ((sample - dc_estimate) * LOOPBACK_GAIN) >> 8;
        output_filter += (scaled - output_filter) >> LOOPBACK_LPF_SHIFT;
        scaled = output_filter;

        if ((scaled < LOOPBACK_NOISE_GATE) && (scaled > -LOOPBACK_NOISE_GATE)) {
          scaled = 0;
        }

        if (scaled > LOOPBACK_OUTPUT_LIMIT) {
          scaled = LOOPBACK_OUTPUT_LIMIT;
        } else if (scaled < -LOOPBACK_OUTPUT_LIMIT) {
          scaled = -LOOPBACK_OUTPUT_LIMIT;
        }

        output_sample = (int16_t)scaled;
        output_magnitude = (scaled < 0) ? (uint32_t)(-scaled) : (uint32_t)scaled;
        if (output_magnitude > output_peak) {
          output_peak = output_magnitude;
        }
      } else if ((mic_diagnostic_enabled != 0U) &&
                 (channel == MIC_LOOPBACK_CHANNEL) &&
                 (valid_sample == 0U) &&
                 (LOOPBACK_SPEAKER_ENABLE != 0U)) {
        output_filter -= output_filter >> LOOPBACK_LPF_SHIFT;
        output_sample = (int16_t)output_filter;
      }

      if (mic_diagnostic_enabled != 0U) {
        count++;
      }

      mic_got_high[channel] = 0U;
    }
  }

  if (hi2s2.Instance->SR & I2S_FLAG_TXE) {
    uint16_t tx_sample = 0U;
    uint16_t tx_word = 0U;

    if (clip_playing != 0U) {
      tx_sample = (uint16_t)audio_clip[clip_index];
    } else if (record_playing != 0U) {
      tx_sample = (uint16_t)record_buffer[record_play_index];
    } else if ((mic_diagnostic_enabled != 0U) && (LOOPBACK_SPEAKER_ENABLE != 0U)) {
      tx_sample = (uint16_t)output_sample;
    }

    /*
     * I2S_DATAFORMAT_24B uses two 16-bit writes per channel. Send mono to
     * both stereo channels so a MAX98357A board hears the same sample no
     * matter whether its L/R select listens to left or right.
     */
    tx_word = ((audio_tx_phase == 0U) || (audio_tx_phase == 2U)) ? tx_sample : 0U;
    hi2s2.Instance->DR = tx_word;

    audio_tx_phase++;
    if (audio_tx_phase >= 4U) {
      audio_tx_phase = 0U;
      if (clip_playing != 0U) {
        clip_index++;
        if (clip_index >= AUDIO_CLIP_SAMPLE_COUNT) {
          clip_playing = 0U;
          clip_index = 0U;
          Test_SendUsbStatus("Audio clip done\r\n");
        }
      } else if (record_playing != 0U) {
        record_play_index++;
        if (record_play_index >= RECORD_SAMPLE_COUNT) {
          record_playing = 0U;
          record_play_index = 0U;
          Test_SendUsbStatus("Record playback done\r\n");
        }
      }
    }
  }

  if ((mic_diagnostic_enabled != 0U) &&
      ((HAL_GetTick() - *last_report_tick) >= 1000U)) {
    uint32_t lavg = (channel_valid[0] > 0U) ? (uint32_t)(channel_sum[0] / channel_valid[0]) : 0U;
    uint32_t ravg = (channel_valid[1] > 0U) ? (uint32_t)(channel_sum[1] / channel_valid[1]) : 0U;
    *last_report_tick = HAL_GetTick();
    snprintf(message, sizeof(message),
             "Mic L avg:%lu pk:%lu raw:%lu n:%lu | R avg:%lu pk:%lu raw:%lu n:%lu | out:%lu total:%lu ovr:%lu rec:%lu inv:%lu\r\n",
             (unsigned long)lavg, (unsigned long)channel_peak[0],
             (unsigned long)raw_peak[0], (unsigned long)channel_valid[0],
             (unsigned long)ravg, (unsigned long)channel_peak[1],
             (unsigned long)raw_peak[1], (unsigned long)channel_valid[1],
             (unsigned long)output_peak, (unsigned long)count,
             (unsigned long)ovr_count, (unsigned long)record_index,
             (unsigned long)record_invalid_count);
    Test_SendUsbStatus(message);
    channel_sum[0] = 0U;
    channel_sum[1] = 0U;
    channel_peak[0] = 0U;
    channel_peak[1] = 0U;
    channel_valid[0] = 0U;
    channel_valid[1] = 0U;
    raw_peak[0] = 0U;
    raw_peak[1] = 0U;
    output_peak = 0;
    count = 0;
    ovr_count = 0;
  }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_SET);

  MX_I2S2_Init();

  /* Start I2S and kick-start clocks so mic begins warming up immediately */
  if (i2s2_ready != 0U) {
    __HAL_I2SEXT_ENABLE(&hi2s2);
    __HAL_I2S_ENABLE(&hi2s2);
    /* Write first zero to DR to start BCLK/WS generation */
    hi2s2.Instance->DR = 0U;
  }

  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  uint32_t last_blink_tick = HAL_GetTick();
  uint32_t last_ping_tick = HAL_GetTick();
  uint32_t last_loopback_report_tick = HAL_GetTick();
  uint8_t blink_state = 0;
  uint8_t last_button_state = 0xFF;

  Test_SendStatus("GPIO/Button test ready\r\n");
  Test_SendStatus("USART1 ESP32 bridge test ready\r\n");
  Test_SendStatus("ESP32 UART PING/PONG test ready\r\n");
  if (i2s2_ready != 0U) {
    Test_SendUsbStatus("Audio clip ready: K0 plays test.wav\r\n");
    Test_SendUsbStatus("Mic record ready: K1 records 0.5s then plays back\r\n");
  } else {
    Test_SendUsbStatus("Mic record disabled: I2S init failed\r\n");
  }
  Test_SendStatus("Type characters in Tera Term\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    GPIO_PinState k0 = HAL_GPIO_ReadPin(KEY_K0_GPIO_Port, KEY_K0_Pin);
    GPIO_PinState k1 = HAL_GPIO_ReadPin(KEY_K1_GPIO_Port, KEY_K1_Pin);
    uint8_t button_state = 0;

    if (k0 == GPIO_PIN_RESET) {
      button_state |= 0x01;
    }
    if (k1 == GPIO_PIN_RESET) {
      button_state |= 0x02;
    }

    if (button_state == 0x03) {
      HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_RESET);
    } else if (button_state == 0x01) {
      HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_SET);
    } else if (button_state == 0x02) {
      HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_RESET);
    } else if ((HAL_GetTick() - last_blink_tick) >= 500U) {
      last_blink_tick = HAL_GetTick();
      blink_state ^= 1U;
      HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin,
                        blink_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
      HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin,
                        blink_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    if (button_state != last_button_state) {
      last_button_state = button_state;
      if (button_state == 0x03) {
        Test_SendStatus("K0+K1 pressed\r\n");
      } else if (button_state == 0x01) {
        Test_StartAudioClip();
      } else if (button_state == 0x02) {
        if ((record_active != 0U) || (record_playing != 0U)) {
          Test_CancelMicRecordPlayback();
        } else {
          Test_StartMicRecord();
          last_loopback_report_tick = HAL_GetTick();
        }
      } else {
        Test_SendStatus("Buttons released\r\n");
      }
    }

    /* Keep ESP32 UART bridge ping running while audio tests are active. */
    Test_SendPingIfDue(&last_ping_tick);
    Test_HandleUartRx();
    Test_ServiceAudioLoopback(record_active, &last_loopback_report_tick);

    /* Print record-done log outside the service loop to avoid TX starvation */
    if (record_done_pending != 0U) {
      char dump[128];
      record_done_pending = 0U;
      Test_SendStatus(record_done_msg);
      /* Dump first 16 recorded PCM samples for debug */
      snprintf(dump, sizeof(dump),
               "REC[0..15]: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
               record_buffer[0], record_buffer[1], record_buffer[2], record_buffer[3],
               record_buffer[4], record_buffer[5], record_buffer[6], record_buffer[7],
               record_buffer[8], record_buffer[9], record_buffer[10], record_buffer[11],
               record_buffer[12], record_buffer[13], record_buffer[14], record_buffer[15]);
      Test_SendUsbStatus(dump);
    }

    /* Wait ~20ms while keeping I2S BCLK/WS alive */
    {
      uint32_t wait_start = HAL_GetTick();
      while ((HAL_GetTick() - wait_start) < 20U) {
        Test_ServiceAudioLoopback(record_active, &last_loopback_report_tick);
      }
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    Error_Handler();
  }

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
  PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief I2S2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2S2_Init(void) {

  /* USER CODE BEGIN I2S2_Init 0 */

  /* USER CODE END I2S2_Init 0 */

  /* USER CODE BEGIN I2S2_Init 1 */

  /* USER CODE END I2S2_Init 1 */
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_24B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_16K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
    i2s2_ready = 0;
    return;
  }
  i2s2_ready = 1;
  /* USER CODE BEGIN I2S2_Init 2 */

  /* USER CODE END I2S2_Init 2 */
}

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void) {

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_D2_Pin | LED_D3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : KEY_K1_Pin KEY_K0_Pin */
  GPIO_InitStruct.Pin = KEY_K1_Pin | KEY_K0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_D2_Pin LED_D3_Pin */
  GPIO_InitStruct.Pin = LED_D2_Pin | LED_D3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = LED_D2_Pin | LED_D3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  while (1) {
    HAL_GPIO_TogglePin(LED_D2_GPIO_Port, LED_D2_Pin);
    HAL_GPIO_TogglePin(LED_D3_GPIO_Port, LED_D3_Pin);
    HAL_Delay(100);
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
