/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "string.h"
#include <stdio.h>
#include "cmsis_os2.h"

uint8_t esp_step = 0;

void ESP_FlushUART(void)
{
    uint8_t dummy;
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    while (HAL_UART_Receive(&huart1, &dummy, 1, 10) == HAL_OK)
    {
    }
}

uint8_t ESP_SendCommand(const char *cmd, const char *expected, uint32_t timeout)
{
    char rxBuf[256] = {0};
    uint8_t ch;
    uint16_t index = 0;
    uint32_t startTick;

    ESP_FlushUART();
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);
    
    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < timeout)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1, 50) == HAL_OK)
        {
            if (index < sizeof(rxBuf) - 1)
            {
                rxBuf[index++] = ch;
                rxBuf[index] = '\0';
            }
            if (strstr(rxBuf, expected) != NULL)
            {
                return 1;
            }
        }
    }
    
    if (strstr(rxBuf, expected) != NULL)
    {
        return 1;
    }
    return 0;
}

uint8_t ESP_Init(const char *serverIP, uint16_t port)
{
    char cmd[100];

    /* Test ESP */
    esp_step = 1;
    if (!ESP_SendCommand("AT\r\n", "OK", 2000)) return 0;
    osDelay(100);
    
    /* Tat Echo */
    ESP_SendCommand("ATE0\r\n", "OK", 1000);
    osDelay(100);

    /* Station mode */
    esp_step = 2;
    if (!ESP_SendCommand("AT+CWMODE=1\r\n", "OK", 2000)) return 0;
    osDelay(100);

    /* Ket noi WiFi nhu code cu cua ban */
    esp_step = 3;
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"tuikycuc\",\"18032005\"\r\n");
    if (!ESP_SendCommand(cmd, "OK", 20000))
    {
        if (!ESP_SendCommand("AT+CIFSR\r\n", "STAIP", 3000)) return 0;
    }
    osDelay(500);

    /* Close any existing connection */
    esp_step = 4;
    ESP_SendCommand("AT+CIPCLOSE\r\n", "OK", 2000);
    osDelay(200);

    /* Single connection */
    ESP_SendCommand("AT+CIPMUX=0\r\n", "OK", 2000);
    osDelay(200);

    /* Connect TCP */
    esp_step = 5;
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", serverIP, port);
    if (!ESP_SendCommand(cmd, "OK", 10000)) return 0;
    
    osDelay(500);
    ESP_FlushUART();

    return 1;
}

uint8_t ESP_SendData(const char *data)
{
    char cmd[32];
    char resp[64] = {0};
    uint16_t len = strlen(data);
    uint8_t byte;
    uint8_t got_prompt = 0;
    uint16_t ri = 0;
    uint32_t start;

    ESP_FlushUART();
    
    /* Gui lenh CIPSEND */
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", len);
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);

    /* Doi dau '>' */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        if (HAL_UART_Receive(&huart1, &byte, 1, 100) == HAL_OK)
        {
            if (byte == '>')
            {
                got_prompt = 1;
                break;
            }
        }
    }

    if (got_prompt)
    {
        osDelay(10);
        HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 2000);

        /* Doi SEND OK (xac nhan ESP da gui qua TCP) */
        ri = 0;
        memset(resp, 0, sizeof(resp));
        start = HAL_GetTick();
        while ((HAL_GetTick() - start) < 3000)
        {
            if (HAL_UART_Receive(&huart1, &byte, 1, 100) == HAL_OK)
            {
                if (ri < sizeof(resp) - 1)
                {
                    resp[ri++] = byte;
                    resp[ri] = '\0';
                }
                if (strstr(resp, "SEND OK") != NULL)
                {
                    // Nhay LED PA5 (bao hieu gui OK)
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
                    osDelay(30);
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
                    return 1;
                }
                if (strstr(resp, "ERROR") != NULL || strstr(resp, "CLOSED") != NULL)
                {
                    break;
                }
            }
        }
    }

    // Nhay LED PA7 (bao loi)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
    osDelay(30);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
    
    return 0;
}

void OTA_TriggerUpdate(void)
{
    /* Ghi co OTA vao vung nho SRAM (an toan cho moi loai chip) */
    /* Dia chi 0x20004000 nam o khoang giua SRAM, khong bi xoa boi C Startup */
    *((volatile uint32_t *)0x20004000) = 0xDEADBEEF;

    /* Reset MCU - Bootloader se doc co nay */
    NVIC_SystemReset();
}
/* USER CODE END 0 */

UART_HandleTypeDef huart1;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

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
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
