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

/* CRC8-CCITT Lookup Table (Polynomial 0x07) */
static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

uint8_t CRC8_Calculate(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++)
    {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

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
    uint16_t len = strlen(data);
    uint8_t byte;
    uint8_t got_prompt = 0;
    uint32_t start;
    char resp[64] = {0};
    uint16_t ri = 0;

    /* Xoa co overrun UART */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    
    /* Flush nhanh - chi doc toi da 32 byte con lai trong buffer */
    {
        uint8_t dummy;
        int flush_count = 0;
        while (flush_count < 32 && HAL_UART_Receive(&huart1, &dummy, 1, 1) == HAL_OK)
        {
            flush_count++;
        }
    }
    
    /* Gui lenh CIPSEND */
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", len);
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 500);

    /* Doi dau '>' - timeout ngan 1s */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 1000)
    {
        if (HAL_UART_Receive(&huart1, &byte, 1, 20) == HAL_OK)
        {
            if (byte == '>')
            {
                got_prompt = 1;
                break;
            }
            /* Phat hien CLOSED -> connection da mat */
            if (byte == 'C' || byte == 'E')
            {
                /* Doc them vai byte xem co phai CLOSED/ERROR khong */
                resp[0] = byte;
                ri = 1;
                uint32_t s2 = HAL_GetTick();
                while (ri < 10 && (HAL_GetTick() - s2) < 100)
                {
                    if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK)
                    {
                        resp[ri++] = byte;
                        resp[ri] = '\0';
                    }
                }
                if (strstr(resp, "CLOSED") != NULL || strstr(resp, "ERROR") != NULL)
                {
                    return 2; /* Ma loi dac biet: connection mat */
                }
            }
        }
    }

    /* Khong return 0 khi miss '>' nua.
       UART Polling + FreeRTOS rat hay bi Overrun mat byte '>'.
       ESP-01 van chap nhan data ngay ca khi ta gui truoc khi doc duoc '>'. */

    /* Gui data ngay lap tuc */
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 1000);

    /* Doi SEND OK - timeout ngan 50ms thoi, vi UART polling hay bi miss byte */
    ri = 0;
    memset(resp, 0, sizeof(resp));
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 50)
    {
        if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK)
        {
            if (ri < sizeof(resp) - 1)
            {
                resp[ri++] = byte;
                resp[ri] = '\0';
            }
            if (strstr(resp, "SEND OK") != NULL)
            {
                return 1; /* Thanh cong */
            }
            if (strstr(resp, "CLOSED") != NULL)
            {
                return 2; /* Connection mat */
            }
        }
    }
    
    /* Do dung FreeRTOS ma chi dung Polling UART nen thuong xuyen miss mat chu "SEND OK".
       Ta cu mac dinh la gui thanh cong neu khong thay bao CLOSED, de toc do giu nguyen 10Hz */
    return 1;
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
