/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
void ESP_FlushUART(void);

uint8_t ESP_SendCommand(const char *cmd,
                        const char *expected,
                        uint32_t timeout);

uint8_t ESP_Init(const char *serverIP,
                 uint16_t port);

uint8_t ESP_SendData(const char *data);

uint8_t CRC8_Calculate(const uint8_t *data, uint16_t len);

void OTA_TriggerUpdate(void);
/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

