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
#define DMA_AUDIO_BUFFER_HALFWORDS 512U
#define DMA_AUDIO_HALF_BUFFER_HALFWORDS (DMA_AUDIO_BUFFER_HALFWORDS / 2U)
#define DMA_AUDIO_FRAME_HALFWORDS 4U
#define DMA_AUDIO_HAL_TRANSFER_SIZE (DMA_AUDIO_BUFFER_HALFWORDS / 2U)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_i2s2_ext_rx;
DMA_HandleTypeDef hdma_spi2_tx;

/* USER CODE BEGIN PV */
static volatile uint8_t i2s2_ready = 0;
static volatile uint8_t audio_dma_running = 0;
static volatile uint8_t clip_playing = 0;
static volatile uint8_t clip_done_pending = 0;
static volatile uint32_t clip_index = 0;
static volatile uint8_t record_active = 0;
static volatile uint8_t record_playing = 0;
static volatile uint8_t record_play_done_pending = 0;
static volatile uint32_t record_index = 0;
static volatile uint32_t record_play_index = 0;
static volatile uint8_t record_done_pending = 0;
static volatile uint8_t audio_dma_error_pending = 0;
static volatile uint32_t record_invalid_count = 0;
static volatile uint32_t record_done_invalid_count = 0;
static volatile uint32_t record_done_lavg = 0;
static volatile uint32_t record_done_lpk = 0;
static volatile uint32_t record_done_ravg = 0;
static volatile uint32_t record_done_rpk = 0;
static volatile uint32_t record_done_ovr = 0;
static volatile uint32_t dma_ovr_count = 0;
static volatile uint32_t dma_error_count = 0;
static volatile uint64_t dma_channel_sum[2] = {0U, 0U};
static volatile uint32_t dma_channel_peak[2] = {0U, 0U};
static volatile uint32_t dma_channel_valid[2] = {0U, 0U};
static volatile uint32_t dma_raw_peak[2] = {0U, 0U};
static volatile uint32_t dma_output_peak = 0;
static volatile uint32_t dma_frame_count = 0;
static int32_t record_dc_estimate = 0;
static int32_t record_lpf = 0;
static int32_t loopback_dc_estimate = 0;
static int32_t loopback_output_filter = 0;
static int16_t loopback_output_sample = 0;
static int16_t record_buffer[RECORD_SAMPLE_COUNT];
static uint16_t dma_rx_buffer[DMA_AUDIO_BUFFER_HALFWORDS];
static uint16_t dma_tx_buffer[DMA_AUDIO_BUFFER_HALFWORDS];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_DMA_Init(void);
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
static void Test_ResetDmaStats(void);
static void Test_StartAudioDma(void);
static void Test_ServiceAudioDma(uint8_t mic_diagnostic_enabled, uint32_t *last_report_tick);
static void Test_ProcessAudioDmaBlock(uint32_t offset, uint32_t length);
static int16_t Test_GetNextDmaOutputSample(void);

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
    Test_SendUsbStatus("Audio clip disabled: I2S DMA init failed\r\n");
    return;
  }

  __disable_irq();
  record_active = 0U;
  record_playing = 0U;
  record_done_pending = 0U;
  record_play_done_pending = 0U;
  clip_index = 0U;
  clip_playing = 1U;
  clip_done_pending = 0U;
  __enable_irq();
  Test_SendStatus("K0 pressed: Audio clip playback\r\n");
}

static void Test_StartMicRecord(void) {
  if (i2s2_ready == 0U) {
    Test_SendUsbStatus("Mic record disabled: I2S DMA init failed\r\n");
    return;
  }

  __disable_irq();
  clip_playing = 0U;
  clip_done_pending = 0U;
  record_index = 0U;
  record_play_index = 0U;
  record_invalid_count = 0U;
  record_done_invalid_count = 0U;
  record_done_lavg = 0U;
  record_done_lpk = 0U;
  record_done_ravg = 0U;
  record_done_rpk = 0U;
  record_done_ovr = 0U;
  record_dc_estimate = 0;
  record_lpf = 0;
  record_done_pending = 0U;
  record_play_done_pending = 0U;
  Test_ResetDmaStats();
  __enable_irq();

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
    __disable_irq();
    record_active = 0U;
    record_playing = 1U;
    __enable_irq();
    Test_SendStatus("K1 pressed: TEST TONE playback\r\n");
  } else {
    __disable_irq();
    record_active = 1U;
    record_playing = 0U;
    __enable_irq();
    Test_SendStatus("K1 pressed: mic record start\r\n");
  }
}

static void Test_CancelMicRecordPlayback(void) {
  __disable_irq();
  record_active = 0U;
  record_playing = 0U;
  record_index = 0U;
  record_play_index = 0U;
  __enable_irq();
  Test_SendStatus("K1 pressed: record/playback cancel\r\n");
}

static void Test_ResetDmaStats(void) {
  dma_channel_sum[0] = 0U;
  dma_channel_sum[1] = 0U;
  dma_channel_peak[0] = 0U;
  dma_channel_peak[1] = 0U;
  dma_channel_valid[0] = 0U;
  dma_channel_valid[1] = 0U;
  dma_raw_peak[0] = 0U;
  dma_raw_peak[1] = 0U;
  dma_output_peak = 0U;
  dma_frame_count = 0U;
  dma_ovr_count = 0U;
}

static void Test_StartAudioDma(void) {
  memset(dma_rx_buffer, 0, sizeof(dma_rx_buffer));
  memset(dma_tx_buffer, 0, sizeof(dma_tx_buffer));
  Test_ResetDmaStats();

  if (HAL_I2SEx_TransmitReceive_DMA(&hi2s2, dma_tx_buffer, dma_rx_buffer,
                                    DMA_AUDIO_HAL_TRANSFER_SIZE) == HAL_OK) {
    audio_dma_running = 1U;
    i2s2_ready = 1U;
  } else {
    audio_dma_running = 0U;
    i2s2_ready = 0U;
    audio_dma_error_pending = 1U;
  }
}

static int16_t Test_GetNextDmaOutputSample(void) {
  int16_t tx_sample = 0;

  if (clip_playing != 0U) {
    tx_sample = audio_clip[clip_index];
    clip_index++;
    if (clip_index >= AUDIO_CLIP_SAMPLE_COUNT) {
      clip_playing = 0U;
      clip_index = 0U;
      clip_done_pending = 1U;
    }
  } else if (record_playing != 0U) {
    tx_sample = record_buffer[record_play_index];
    record_play_index++;
    if (record_play_index >= RECORD_SAMPLE_COUNT) {
      record_playing = 0U;
      record_play_index = 0U;
      record_play_done_pending = 1U;
    }
  } else if (LOOPBACK_SPEAKER_ENABLE != 0U) {
    tx_sample = loopback_output_sample;
  }

  return tx_sample;
}

static void Test_ProcessAudioDmaBlock(uint32_t offset, uint32_t length) {
  uint32_t end = offset + length;

  for (uint32_t i = offset; (i + 3U) < end; i += DMA_AUDIO_FRAME_HALFWORDS) {
    uint32_t raw[2];
    int32_t sample[2];
    uint32_t magnitude[2];
    uint8_t valid_sample[2];
    int16_t tx_sample;

    raw[0] = ((uint32_t)dma_rx_buffer[i] << 16) | dma_rx_buffer[i + 1U];
    raw[1] = ((uint32_t)dma_rx_buffer[i + 2U] << 16) | dma_rx_buffer[i + 3U];
    sample[0] = ((int32_t)raw[0]) >> 8;
    sample[1] = ((int32_t)raw[1]) >> 8;

    for (uint8_t channel = 0U; channel < 2U; channel++) {
      magnitude[channel] = (sample[channel] < 0) ? (uint32_t)(-sample[channel]) : (uint32_t)sample[channel];
      valid_sample[channel] = (magnitude[channel] < MIC_INVALID_MAGNITUDE) ? 1U : 0U;

      if (magnitude[channel] > dma_raw_peak[channel]) {
        dma_raw_peak[channel] = magnitude[channel];
      }

      if (valid_sample[channel] != 0U) {
        dma_channel_sum[channel] += magnitude[channel];
        if (magnitude[channel] > dma_channel_peak[channel]) {
          dma_channel_peak[channel] = magnitude[channel];
        }
        dma_channel_valid[channel]++;
      }
    }

    if ((record_active != 0U) && (MIC_LOOPBACK_CHANNEL < 2U)) {
      int32_t pcm = 0;
      uint8_t target_channel = MIC_LOOPBACK_CHANNEL;

      if (valid_sample[target_channel] != 0U) {
        record_dc_estimate += (sample[target_channel] - record_dc_estimate) >> LOOPBACK_DC_SHIFT;
        int32_t pcm_raw = ((sample[target_channel] - record_dc_estimate) >> 8) * RECORD_GAIN;
        record_lpf += (pcm_raw - record_lpf) >> 3;
      } else {
        record_lpf -= record_lpf >> 3;
        record_invalid_count++;
      }

      pcm = record_lpf;
      if (pcm > 32767) {
        pcm = 32767;
      } else if (pcm < -32768) {
        pcm = -32768;
      }

      if ((pcm < RECORD_NOISE_GATE) && (pcm > -RECORD_NOISE_GATE)) {
        pcm = 0;
      }

      if (record_index < RECORD_SAMPLE_COUNT) {
        record_buffer[record_index] = (int16_t)pcm;
        record_index++;
      }

      if (record_index >= RECORD_SAMPLE_COUNT) {
        record_done_invalid_count = record_invalid_count;
        record_done_lavg = (dma_channel_valid[0] > 0U) ? (uint32_t)(dma_channel_sum[0] / dma_channel_valid[0]) : 0U;
        record_done_ravg = (dma_channel_valid[1] > 0U) ? (uint32_t)(dma_channel_sum[1] / dma_channel_valid[1]) : 0U;
        record_done_lpk = dma_channel_peak[0];
        record_done_rpk = dma_channel_peak[1];
        record_done_ovr = dma_ovr_count;
        record_active = 0U;
        record_playing = 1U;
        record_play_index = 0U;
        record_done_pending = 1U;
      }
    }

    if ((LOOPBACK_SPEAKER_ENABLE != 0U) && (MIC_LOOPBACK_CHANNEL < 2U)) {
      uint8_t target_channel = MIC_LOOPBACK_CHANNEL;
      if (valid_sample[target_channel] != 0U) {
        int32_t scaled;
        uint32_t output_magnitude;
        loopback_dc_estimate += (sample[target_channel] - loopback_dc_estimate) >> LOOPBACK_DC_SHIFT;
        scaled = ((sample[target_channel] - loopback_dc_estimate) * LOOPBACK_GAIN) >> 8;
        loopback_output_filter += (scaled - loopback_output_filter) >> LOOPBACK_LPF_SHIFT;
        scaled = loopback_output_filter;

        if ((scaled < LOOPBACK_NOISE_GATE) && (scaled > -LOOPBACK_NOISE_GATE)) {
          scaled = 0;
        }

        if (scaled > LOOPBACK_OUTPUT_LIMIT) {
          scaled = LOOPBACK_OUTPUT_LIMIT;
        } else if (scaled < -LOOPBACK_OUTPUT_LIMIT) {
          scaled = -LOOPBACK_OUTPUT_LIMIT;
        }

        loopback_output_sample = (int16_t)scaled;
        output_magnitude = (scaled < 0) ? (uint32_t)(-scaled) : (uint32_t)scaled;
        if (output_magnitude > dma_output_peak) {
          dma_output_peak = output_magnitude;
        }
      } else {
        loopback_output_filter -= loopback_output_filter >> LOOPBACK_LPF_SHIFT;
        loopback_output_sample = (int16_t)loopback_output_filter;
      }
    } else {
      loopback_output_sample = 0;
      loopback_output_filter = 0;
      loopback_dc_estimate = 0;
    }

    tx_sample = Test_GetNextDmaOutputSample();
    dma_tx_buffer[i] = (uint16_t)tx_sample;
    dma_tx_buffer[i + 1U] = 0U;
    dma_tx_buffer[i + 2U] = (uint16_t)tx_sample;
    dma_tx_buffer[i + 3U] = 0U;
    dma_frame_count++;
  }
}

static void Test_ServiceAudioDma(uint8_t mic_diagnostic_enabled, uint32_t *last_report_tick) {
  char message[192];

  if ((hi2s2.Instance != NULL) &&
      ((I2SxEXT(hi2s2.Instance)->SR & I2S_FLAG_OVR) != 0U)) {
    __HAL_I2SEXT_CLEAR_OVRFLAG(&hi2s2);
    dma_ovr_count++;
  }

  if (audio_dma_error_pending != 0U) {
    audio_dma_error_pending = 0U;
    snprintf(message, sizeof(message),
             "I2S DMA error count:%lu hal:0x%08lX\r\n",
             (unsigned long)dma_error_count,
             (unsigned long)HAL_I2S_GetError(&hi2s2));
    Test_SendStatus(message);
  }

  if (i2s2_ready == 0U) {
    return;
  }

  if (clip_done_pending != 0U) {
    clip_done_pending = 0U;
    Test_SendUsbStatus("Audio clip done\r\n");
  }

  if (record_play_done_pending != 0U) {
    record_play_done_pending = 0U;
    Test_SendUsbStatus("Record playback done\r\n");
  }

  if (mic_diagnostic_enabled == 0U) {
    Test_ResetDmaStats();
    *last_report_tick = HAL_GetTick();
    return;
  }

  if ((HAL_GetTick() - *last_report_tick) >= 1000U) {
    uint64_t channel_sum_snapshot[2];
    uint32_t channel_peak_snapshot[2];
    uint32_t channel_valid_snapshot[2];
    uint32_t raw_peak_snapshot[2];
    uint32_t output_peak_snapshot;
    uint32_t frame_count_snapshot;
    uint32_t ovr_count_snapshot;
    uint32_t lavg;
    uint32_t ravg;

    __disable_irq();
    channel_sum_snapshot[0] = dma_channel_sum[0];
    channel_sum_snapshot[1] = dma_channel_sum[1];
    channel_peak_snapshot[0] = dma_channel_peak[0];
    channel_peak_snapshot[1] = dma_channel_peak[1];
    channel_valid_snapshot[0] = dma_channel_valid[0];
    channel_valid_snapshot[1] = dma_channel_valid[1];
    raw_peak_snapshot[0] = dma_raw_peak[0];
    raw_peak_snapshot[1] = dma_raw_peak[1];
    output_peak_snapshot = dma_output_peak;
    frame_count_snapshot = dma_frame_count;
    ovr_count_snapshot = dma_ovr_count;
    Test_ResetDmaStats();
    __enable_irq();

    lavg = (channel_valid_snapshot[0] > 0U) ? (uint32_t)(channel_sum_snapshot[0] / channel_valid_snapshot[0]) : 0U;
    ravg = (channel_valid_snapshot[1] > 0U) ? (uint32_t)(channel_sum_snapshot[1] / channel_valid_snapshot[1]) : 0U;
    *last_report_tick = HAL_GetTick();
    snprintf(message, sizeof(message),
             "DMA Mic L avg:%lu pk:%lu raw:%lu n:%lu | R avg:%lu pk:%lu raw:%lu n:%lu | out:%lu frames:%lu ovr:%lu rec:%lu inv:%lu\r\n",
             (unsigned long)lavg, (unsigned long)channel_peak_snapshot[0],
             (unsigned long)raw_peak_snapshot[0], (unsigned long)channel_valid_snapshot[0],
             (unsigned long)ravg, (unsigned long)channel_peak_snapshot[1],
             (unsigned long)raw_peak_snapshot[1], (unsigned long)channel_valid_snapshot[1],
             (unsigned long)output_peak_snapshot, (unsigned long)frame_count_snapshot,
             (unsigned long)ovr_count_snapshot, (unsigned long)record_index,
             (unsigned long)record_invalid_count);
    Test_SendUsbStatus(message);
  }
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    Test_ProcessAudioDmaBlock(0U, DMA_AUDIO_HALF_BUFFER_HALFWORDS);
  }
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    Test_ProcessAudioDmaBlock(DMA_AUDIO_HALF_BUFFER_HALFWORDS, DMA_AUDIO_HALF_BUFFER_HALFWORDS);
  }
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    audio_dma_running = 0U;
    i2s2_ready = 0U;
    dma_error_count++;
    audio_dma_error_pending = 1U;
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
  MX_DMA_Init();
  MX_GPIO_Init();

  HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_SET);

  MX_I2S2_Init();
  Test_StartAudioDma();

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
    Test_SendUsbStatus("I2S2 full-duplex DMA audio ready\r\n");
    Test_SendUsbStatus("Audio clip ready: K0 plays Koharu login clip\r\n");
    Test_SendUsbStatus("Mic record ready: K1 records 0.5s then plays back\r\n");
  } else {
    Test_SendUsbStatus("Mic record disabled: I2S DMA init failed\r\n");
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
    Test_ServiceAudioDma(record_active, &last_loopback_report_tick);

    /* Print record-done log outside the DMA callback to avoid audio starvation */
    if (record_done_pending != 0U) {
      char record_done_msg[192];
      char dump[128];
      record_done_pending = 0U;
      snprintf(record_done_msg, sizeof(record_done_msg),
               "Mic record done: playback start inv:%lu Lavg:%lu Lpk:%lu Ravg:%lu Rpk:%lu ovr:%lu\r\n",
               (unsigned long)record_done_invalid_count,
               (unsigned long)record_done_lavg,
               (unsigned long)record_done_lpk,
               (unsigned long)record_done_ravg,
               (unsigned long)record_done_rpk,
               (unsigned long)record_done_ovr);
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
 * Enable DMA controller clock and interrupt priority.
 */
static void MX_DMA_Init(void) {

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
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
