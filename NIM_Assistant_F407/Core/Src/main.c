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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
I2S_HandleTypeDef hi2s2;

/* USER CODE BEGIN PV */
static uint8_t i2s2_ready = 0;

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
static void Test_FeedI2STx(void);
static void Test_ReportMicLevelOnce(void);
static void Test_ReportMicLevelIfDue(uint8_t mic_monitor_enabled,
                                     uint32_t *last_mic_tick);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Test_SendStatus(const char *message) {
  HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), 10);
  Test_SendUsbStatus(message);
}

static void Test_SendUsbStatus(const char *message) {
  uint8_t retries = 3;

  while ((CDC_Transmit_FS((uint8_t *)message, strlen(message)) == USBD_BUSY) &&
         (retries > 0U)) {
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
      Test_SendUsbStatus("UART RX overflow\r\n");
    }
  }
}

/*
 * Feed I2S master TX DR to keep BCLK/WS running.
 * STM32 I2S master only generates clocks while DR has data to send.
 * Call this frequently (every main loop iteration) to keep mic clocked.
 */
static void Test_FeedI2STx(void) {
  if (i2s2_ready == 0U)
    return;
  if (hi2s2.Instance->SR & I2S_FLAG_TXE) {
    hi2s2.Instance->DR = 0U;
  }
}

static void Test_ReportMicLevelOnce(void) {
  char message[120];

  if (i2s2_ready == 0U) {
    Test_SendUsbStatus("MIC I2S init failed\r\n");
    return;
  }

  /* Clear OVR: read DR then SR per reference manual */
  {
    volatile uint32_t dummy;
    dummy = I2SxEXT(hi2s2.Instance)->DR;
    dummy = I2SxEXT(hi2s2.Instance)->SR;
    (void)dummy;
  }

  /*
   * ICS43434 with SELECT=GND outputs on Right channel (CHSIDE=1).
   * Read 512 RXNE events, extract Right channel 24-bit samples.
   */
  uint32_t level_sum = 0;
  uint32_t peak = 0;
  uint32_t count = 0;

  uint16_t high_word = 0;
  uint8_t got_high = 0;

  for (uint32_t n = 0; n < 512U; n++) {
    uint32_t guard = 300000U;
    while (((I2SxEXT(hi2s2.Instance)->SR & I2S_FLAG_RXNE) == 0U) &&
           (guard > 0U)) {
      guard--;
    }
    if (guard == 0U) {
      snprintf(message, sizeof(message), "MIC RX timeout at n=%lu\r\n",
               (unsigned long)n);
      Test_SendUsbStatus(message);
      return;
    }

    uint16_t sr = (uint16_t)(I2SxEXT(hi2s2.Instance)->SR);
    uint16_t dr = (uint16_t)(I2SxEXT(hi2s2.Instance)->DR);

    if (hi2s2.Instance->SR & I2S_FLAG_TXE) {
      hi2s2.Instance->DR = 0U;
    }

    /* Right channel = CHSIDE 1 = ICS43434 mic data */
    if (sr & I2S_FLAG_CHSIDE) {
      if (got_high == 0U) {
        high_word = dr;
        got_high = 1U;
      } else {
        uint32_t raw = ((uint32_t)high_word << 16) | dr;
        int32_t sample = ((int32_t)raw) >> 8;
        uint32_t mag = (sample < 0) ? (uint32_t)(-sample) : (uint32_t)sample;
        level_sum += mag;
        if (mag > peak) {
          peak = mag;
        }
        count++;
        got_high = 0U;
      }
    } else {
      got_high = 0U; /* Left channel — reset */
    }
  }

  uint32_t avg = (count > 0U) ? (level_sum / count) : 0U;

  snprintf(message, sizeof(message), "MIC avg:%lu peak:%lu n:%lu\r\n",
           (unsigned long)avg, (unsigned long)peak, (unsigned long)count);
  Test_SendUsbStatus(message);
}

static void Test_ReportMicLevelIfDue(uint8_t mic_monitor_enabled,
                                     uint32_t *last_mic_tick) {
  if (mic_monitor_enabled == 0U) {
    return;
  }
  if ((HAL_GetTick() - *last_mic_tick) < 300U) {
    return;
  }
  *last_mic_tick = HAL_GetTick();
  Test_ReportMicLevelOnce();
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
  uint32_t last_mic_tick = HAL_GetTick();
  uint8_t blink_state = 0;
  uint8_t last_button_state = 0xFF;
  uint8_t mic_monitor_enabled = 0;

  Test_SendStatus("GPIO/Button test ready\r\n");
  Test_SendStatus("USART1 USB-TTL echo test ready\r\n");
  Test_SendStatus("ESP32 UART PING/PONG test ready\r\n");
  if (i2s2_ready != 0U) {
    Test_SendUsbStatus("INMP441 mic monitor ready: K1 toggles ON/OFF\r\n");
  } else {
    Test_SendUsbStatus("INMP441 mic level test disabled: I2S init failed\r\n");
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
        Test_SendStatus("K0 pressed\r\n");
      } else if (button_state == 0x02) {
        mic_monitor_enabled ^= 1U;
        if (mic_monitor_enabled != 0U) {
          Test_SendStatus("K1 pressed: MIC monitor ON\r\n");
          last_mic_tick = 0;
        } else {
          Test_SendStatus("K1 pressed: MIC monitor OFF\r\n");
        }
      } else {
        Test_SendStatus("Buttons released\r\n");
      }
    }

    /* Keep ESP32 UART bridge ping running while mic monitor is active. */
    Test_SendPingIfDue(&last_ping_tick);
    Test_HandleUartRx();
    Test_ReportMicLevelIfDue(mic_monitor_enabled, &last_mic_tick);

    /* Wait ~20ms while keeping I2S BCLK/WS alive */
    {
      uint32_t wait_start = HAL_GetTick();
      while ((HAL_GetTick() - wait_start) < 20U) {
        Test_FeedI2STx();
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
