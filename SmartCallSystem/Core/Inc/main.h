/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define AD1_Pin GPIO_PIN_6
#define AD1_GPIO_Port GPIOA
#define AD2_Pin GPIO_PIN_7
#define AD2_GPIO_Port GPIOA
#define KEY0_Pin GPIO_PIN_5
#define KEY0_GPIO_Port GPIOC
#define TFT_BL_Pin GPIO_PIN_11
#define TFT_BL_GPIO_Port GPIOB
#define KEYS1_Pin GPIO_PIN_12
#define KEYS1_GPIO_Port GPIOB
#define KEYS2_Pin GPIO_PIN_13
#define KEYS2_GPIO_Port GPIOB
#define KEYS3_Pin GPIO_PIN_14
#define KEYS3_GPIO_Port GPIOB
#define KEYS4_Pin GPIO_PIN_15
#define KEYS4_GPIO_Port GPIOB
#define JRBUSY_Pin GPIO_PIN_7
#define JRBUSY_GPIO_Port GPIOC
#define TFT_SCL_Pin GPIO_PIN_8
#define TFT_SCL_GPIO_Port GPIOC
#define TFT_SDA_Pin GPIO_PIN_9
#define TFT_SDA_GPIO_Port GPIOC
#define LED0_Pin GPIO_PIN_8
#define LED0_GPIO_Port GPIOA
#define KEY1_Pin GPIO_PIN_15
#define KEY1_GPIO_Port GPIOA
#define TFT_RST_Pin GPIO_PIN_10
#define TFT_RST_GPIO_Port GPIOC
#define LED1_Pin GPIO_PIN_2
#define LED1_GPIO_Port GPIOD
#define KEYS5_Pin GPIO_PIN_3
#define KEYS5_GPIO_Port GPIOB
#define KEYS6_Pin GPIO_PIN_4
#define KEYS6_GPIO_Port GPIOB
#define KEYS7_Pin GPIO_PIN_5
#define KEYS7_GPIO_Port GPIOB
#define KEYS8_Pin GPIO_PIN_6
#define KEYS8_GPIO_Port GPIOB
#define TFT_DC_Pin GPIO_PIN_7
#define TFT_DC_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_8
#define TFT_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
