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
#include "audio_clip.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
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
static void Test_PlayAudioClip(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Test_SendStatus(const char *message) {
  HAL_UART_Transmit(&huart1, (uint8_t *)message, strlen(message), HAL_MAX_DELAY);
  CDC_Transmit_FS((uint8_t *)message, strlen(message));
}

static void Test_SendUsbStatus(const char *message) {
  CDC_Transmit_FS((uint8_t *)message, strlen(message));
}

static void Test_SendPingIfDue(uint32_t *last_ping_tick) {
  const char ping[] = "PING\r\n";

  if ((HAL_GetTick() - *last_ping_tick) >= 1000U) {
    *last_ping_tick = HAL_GetTick();
    HAL_UART_Transmit(&huart1, (uint8_t *)ping, strlen(ping), HAL_MAX_DELAY);
    Test_SendUsbStatus("PING sent\r\n");
  }
}

static void Test_HandleUartRx(void) {
  static char line_buffer[16];
  static uint8_t line_index = 0;
  uint8_t rx_byte;
  char response[] = "RX: x\r\n";

  if (HAL_UART_Receive(&huart1, &rx_byte, 1, 0) == HAL_OK) {
    response[4] = (char)rx_byte;
    Test_SendStatus(response);

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

static void Test_PlayAudioClip(void) {
  uint16_t tx_buffer[128];
  uint32_t sample_index = 0;

  while (sample_index < AUDIO_CLIP_SAMPLE_COUNT) {
    uint16_t frames = 0;

    while ((frames < 64U) && (sample_index < AUDIO_CLIP_SAMPLE_COUNT)) {
      int16_t sample = audio_clip[sample_index++];
      tx_buffer[frames * 2U] = (uint16_t)sample;
      tx_buffer[(frames * 2U) + 1U] = (uint16_t)sample;
      frames++;
    }

    if (HAL_I2S_Transmit(&hi2s2, tx_buffer, frames * 2U, 100) != HAL_OK) {
      Test_SendUsbStatus("I2S audio clip error\r\n");
      return;
    }
  }

  Test_SendUsbStatus("I2S audio clip sent\r\n");
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
  MX_I2S2_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  uint32_t last_blink_tick = HAL_GetTick();
  uint32_t last_ping_tick = HAL_GetTick();
  uint8_t blink_state = 0;
  uint8_t last_button_state = 0xFF;

  HAL_GPIO_WritePin(LED_D2_GPIO_Port, LED_D2_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_D3_GPIO_Port, LED_D3_Pin, GPIO_PIN_SET);
  Test_SendStatus("GPIO/Button test ready\r\n");
  Test_SendStatus("USART1 USB-TTL echo test ready\r\n");
  Test_SendStatus("ESP32 UART PING/PONG test ready\r\n");
  Test_SendUsbStatus("I2S2 MAX98357A audio clip test ready\r\n");
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
        Test_SendStatus("K1 pressed\r\n");
        Test_PlayAudioClip();
      } else {
        Test_SendStatus("Buttons released\r\n");
      }
    }

    // 透過 USB 傳送資料
    Test_SendPingIfDue(&last_ping_tick);
    Test_HandleUartRx();
    HAL_Delay(20);

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
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_16K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
    Error_Handler();
  }
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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
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
