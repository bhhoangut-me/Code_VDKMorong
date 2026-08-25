/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : OTA Bootloader cho STM32F103C8T6 + ESP-01 (HAL version)
  *
  *   BOOTLOADER ROM : 0x08000000 - 0x080027FF (10KB)
  *   APP ROM        : 0x08002800 - 0x0800FFFF (54KB)
  *
  *   Kiem tra co OTA trong SRAM tai 0x20004000.
  *   Neu == 0xDEADBEEF: Tai firmware moi tu HTTP Server qua ESP-01
  *   Neu khong:         Nhay thang vao App tai 0x08002800
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDRESS         0x08002800U
#define OTA_FLAG_MAGIC      0xDEADBEEFU
#define OTA_FLAG_ADDRESS    0x20004000U
#define FLASH_PAGE_SIZE     0x400U        /* 1KB per page (STM32F103) */
#define FLASH_APP_END       0x08010000U   /* 64KB total flash */
#define APP_MAX_SIZE        (FLASH_APP_END - APP_ADDRESS)

/* WiFi & Server config */
#define WIFI_SSID           "tuikycuc"
#define WIFI_PASS           "18032005"
#define HTTP_SERVER_IP      "172.20.10.7"
#define HTTP_SERVER_PORT    "8080"
#define FIRMWARE_PATH       "/update.bin"
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
UART_HandleTypeDef huart1;
static char rxBuf[512];
static uint16_t rxIdx;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
void Error_Handler(void);

/* USER CODE BEGIN PFP */
static void     LED_AllOn(void);
static void     LED_AllOff(void);
static void     UART_Flush(void);
static uint8_t  ESP_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms);
static uint8_t  ESP_Init_Boot(void);
static uint8_t  OTA_Download(void);
static void     JumpToApp(void);
/* USER CODE END PFP */

/* ======================= LED ======================= */
static void LED_AllOn(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_SET);
}

static void LED_AllOff(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);
}

/* ======================= UART Helpers ======================= */
static void UART_Flush(void)
{
    uint8_t dummy;
    /* Xoa overrun flag neu co */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    /* Doc het du lieu con trong buffer */
    while (HAL_UART_Receive(&huart1, &dummy, 1, 10) == HAL_OK);
}

/* ======================= ESP AT COMMANDS ======================= */
static uint8_t ESP_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint8_t ch;
    rxIdx = 0;
    memset(rxBuf, 0, sizeof(rxBuf));

    UART_Flush();
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1, 50) == HAL_OK)
        {
            if (rxIdx < sizeof(rxBuf) - 1)
            {
                rxBuf[rxIdx++] = (char)ch;
                rxBuf[rxIdx] = '\0';
            }
            if (strstr(rxBuf, expect))
                return 1;
        }
    }
    return 0;
}



static uint8_t ESP_Init_Boot(void)
{
    char cmd[128];

    HAL_Delay(2000); /* Cho ESP-01 boot */

    if (!ESP_SendCmd("AT\r\n", "OK", 2000)) return 0;
    HAL_Delay(100);
    ESP_SendCmd("ATE0\r\n", "OK", 1000);
    HAL_Delay(100);

    if (!ESP_SendCmd("AT+CWMODE=1\r\n", "OK", 2000)) return 0;
    HAL_Delay(100);

    /* Ket noi WiFi */
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    if (!ESP_SendCmd(cmd, "OK", 20000))
    {
        /* Co the da ket noi roi, thu kiem tra IP */
        if (!ESP_SendCmd("AT+CIFSR\r\n", "STAIP", 3000)) return 0;
    }
    HAL_Delay(500);

    ESP_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);
    HAL_Delay(200);
    if (!ESP_SendCmd("AT+CIPMUX=0\r\n", "OK", 2000))
    {
        return 0;
    }
    HAL_Delay(200);

    /* Bat Transparent Mode TRUOC khi CIPSTART (bat buoc o mot so firmware ESP8266) */
    ESP_SendCmd("AT+CIPMODE=1\r\n", "OK", 2000);
    HAL_Delay(200);

    /* Ket noi TCP den HTTP server */
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n",
             HTTP_SERVER_IP, HTTP_SERVER_PORT);
    if (!ESP_SendCmd(cmd, "OK", 10000)) return 0;
    HAL_Delay(500);

    return 1;
}

/* ======================= OTA DOWNLOAD ======================= */
static uint8_t OTA_Download(void)
{
    uint32_t contentLength = 0;
    uint32_t received = 0;
    uint8_t ch;
    char headerBuf[512];
    uint16_t hdrPos = 0;
    uint32_t writeAddr = APP_ADDRESS;
    uint8_t lowByte = 0;
    uint8_t haveLowByte = 0;
    uint32_t start;
    uint8_t gotPrompt = 0;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError = 0;
    uint32_t addr;
    uint8_t headerSkipped = 0;
    uint32_t lastDataTime;
    char *endHdr, *p, *dataStart;
    uint16_t remaining;
    uint16_t halfWord;
    uint16_t i;
    char httpReq[128];

    /* 1. Tao HTTP GET */
    snprintf(httpReq, sizeof(httpReq),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             FIRMWARE_PATH, HTTP_SERVER_IP);

    /* 2. AT+CIPSEND (Vi da bat CIPMODE=1 tu truoc) */
    UART_Flush();
    HAL_UART_Transmit(&huart1, (uint8_t *)"AT+CIPSEND\r\n", 12, 1000);

    /* Cho dau '>' */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1, 100) == HAL_OK)
        {
            if (ch == '>') { gotPrompt = 1; break; }
        }
    }
    
    if (!gotPrompt)
    {
        goto cleanup;
    }

    /* 4. Unlock va xoa Flash TRUOC KHI gui GET de khong mat du lieu TCP */
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
    eraseInit.Banks       = FLASH_BANK_1;
    eraseInit.NbPages     = 1;

    for (addr = APP_ADDRESS; addr < FLASH_APP_END; addr += FLASH_PAGE_SIZE)
    {
        eraseInit.PageAddress = addr;
        HAL_FLASHEx_Erase(&eraseInit, &pageError);
        
        if (((addr - APP_ADDRESS) / FLASH_PAGE_SIZE) % 4 == 0)
        {
            LED_AllOn();
            HAL_Delay(20);
            LED_AllOff();
        }
    }

    /* 5. Gui HTTP GET */
    HAL_Delay(10);
    HAL_UART_Transmit(&huart1, (uint8_t *)httpReq, strlen(httpReq), 2000);

    /* 6. Doc Header (Tim HTTP 200 OK va Content-Length) */
    lastDataTime = HAL_GetTick();

    while ((HAL_GetTick() - lastDataTime) < 15000)
    {
        /* Dung direct register de loai bo overhead cua HAL_UART_Receive -> Khong bi rot byte */
        if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
        {
            ch = (uint8_t)(huart1.Instance->DR & 0x00FF);
            lastDataTime = HAL_GetTick();

        if (!headerSkipped)
        {
            if (hdrPos < sizeof(headerBuf) - 1)
            {
                headerBuf[hdrPos++] = ch;
                headerBuf[hdrPos] = '\0';
            }

            endHdr = strstr(headerBuf, "\r\n\r\n");
            if (endHdr)
            {
                headerSkipped = 1;
                
                if (!strstr(headerBuf, "200 OK"))
                {
                    goto cleanup;
                }

                p = strstr(headerBuf, "Content-Length: ");
                if (p)
                {
                    contentLength = atoi(p + 16);
                }
                
                if (contentLength == 0 || contentLength > APP_MAX_SIZE)
                {
                    goto cleanup;
                }

                /* Copy phan du lieu thuc bi dính sau header (neu co) */
                dataStart = endHdr + 4;
                remaining = hdrPos - (uint16_t)(dataStart - headerBuf);
                
                for (i = 0; i < remaining; i++)
                {
                    if (received >= contentLength) break;

                    if (!haveLowByte)
                    {
                        lowByte = dataStart[i];
                        haveLowByte = 1;
                    }
                    else
                    {
                        halfWord = lowByte | (dataStart[i] << 8);
                        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, writeAddr, halfWord);
                        writeAddr += 2;
                        haveLowByte = 0;
                    }
                    received++;
                }
                
                if (received >= contentLength) break;
            }
        }
        else
        {
            /* 7. Doc tung byte data va ghi truc tiep vao Flash */
            if (received >= contentLength) break;

            if (!haveLowByte)
            {
                lowByte = ch;
                haveLowByte = 1;
            }
            else
            {
                halfWord = lowByte | (ch << 8);
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, writeAddr, halfWord);
                writeAddr += 2;
                haveLowByte = 0;
            }
            received++;

            /* Nhay LED de bao tien trinh */
            if (received % 1024 == 0)
            {
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7);
            }
            
            if (received >= contentLength) break;
        }
        }
    }

    if (haveLowByte && received == contentLength)
    {
        halfWord = lowByte | (0xFF << 8);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, writeAddr, halfWord);
    }

cleanup:
    HAL_FLASH_Lock();
    
    /* Thoat che do trong suot */
    HAL_Delay(1000);
    HAL_UART_Transmit(&huart1, (uint8_t *)"+++", 3, 1000);
    HAL_Delay(1000);

    /* Dong ket noi */
    ESP_SendCmd("AT+CIPMODE=0\r\n", "OK", 2000);
    HAL_Delay(100);
    ESP_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);

    if (received > 0 && received == contentLength)
    {
        uint32_t appStack = *(volatile uint32_t *)APP_ADDRESS;
        if (appStack != 0xFFFFFFFF && (appStack & 0x20000000) == 0x20000000)
        {
            return 1;
        }
    }
    
    return 0;
}

/* ======================= JUMP TO APP ======================= */
static void JumpToApp(void)
{
    /* Kiem tra App co hop le khong (Stack Pointer phai nam trong SRAM) */
    uint32_t appStack = *(volatile uint32_t *)APP_ADDRESS;
    /* Kiem tra App co hop le khong (Stack Pointer khong the la 0xFFFFFFFF va phai thuoc SRAM 0x20000000) */
    if (appStack == 0xFFFFFFFF || (appStack & 0x20000000) != 0x20000000)
    {
        /* App khong hop le, o lai bootloader */
        return;
    }

    /* Disable tat ca interrupt */
    __disable_irq();

    /* Reset SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* Disable tat ca NVIC interrupt */
    for (uint8_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* De-init HAL */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* Set Vector Table offset cho App */
    SCB->VTOR = APP_ADDRESS;

    /* Set Main Stack Pointer */
    __set_MSP(appStack);

    /* Re-enable interrupt truoc khi nhay */
    __enable_irq();

    /* Nhay vao Reset Handler cua App */
    uint32_t appEntry = *(volatile uint32_t *)(APP_ADDRESS + 4);
    void (*app_reset_handler)(void) = (void (*)(void))appEntry;
    app_reset_handler();
}

/* ======================= MAIN ======================= */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    /* MCU Configuration */
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    /* Doc co OTA tu SRAM */
    uint32_t otaFlag = *((volatile uint32_t *)OTA_FLAG_ADDRESS);

    /* Kiem tra xem App co bi hong hoac bi xoa (vi du do OTA lan truoc that bai) hay khong */
    uint32_t appStack = *(volatile uint32_t *)APP_ADDRESS;
    if (appStack == 0xFFFFFFFF || (appStack & 0x20000000) != 0x20000000)
    {
        /* App khong hop le -> BAT BUOC phai vao che do OTA de cuu mach */
        otaFlag = OTA_FLAG_MAGIC;
    }

    if (otaFlag == OTA_FLAG_MAGIC)
    {
        /* Co OTA duoc set! Xoa co truoc (tranh loop vo han) */
        *((volatile uint32_t *)OTA_FLAG_ADDRESS) = 0x00000000;

        /* Bao hieu dang OTA: Nhay LED nhanh 5 lan */
        for (int i = 0; i < 5; i++)
        {
            LED_AllOn();
            HAL_Delay(100);
            LED_AllOff();
            HAL_Delay(100);
        }

        /* Init UART cho giao tiep ESP-01 */
        MX_USART1_UART_Init();

        /* Init ESP va ket noi WiFi + TCP */
        if (ESP_Init_Boot())
        {
            /* Tai firmware */
            if (OTA_Download())
            {
                /* Thanh cong! Nhay LED cham 3 lan */
                for (int i = 0; i < 3; i++)
                {
                    LED_AllOn();
                    HAL_Delay(500);
                    LED_AllOff();
                    HAL_Delay(500);
                }
            }
            else
            {
                /* That bai download: Nhay LED nhanh 10 lan */
                for (int i = 0; i < 10; i++)
                {
                    LED_AllOn();
                    HAL_Delay(50);
                    LED_AllOff();
                    HAL_Delay(50);
                }
            }
        }
        else
        {
            /* That bai ESP Init: Nhay LED nhanh 20 lan */
            for (int i = 0; i < 20; i++)
            {
                LED_AllOn();
                HAL_Delay(50);
                LED_AllOff();
                HAL_Delay(50);
            }
        }

        /* Reset de nhay vao App (du thanh cong hay that bai) */
        NVIC_SystemReset();
    }
    else
    {
        /* Khong co yeu cau OTA - Nhay thang vao App */
        JumpToApp();
    }

    /* Fallback: Neu JumpToApp that bai (app khong hop le) */
    while (1)
    {
        LED_AllOn();
        HAL_Delay(1000);
        LED_AllOff();
        HAL_Delay(1000);
    }
}

/* ======================= CLOCK CONFIG ======================= */
/**
  * @brief System Clock Configuration - HSI 8MHz (khong PLL)
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* HSI + PLL = 64MHz */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ======================= GPIO INIT ======================= */
/**
  * @brief GPIO Init: PA4, PA5, PA7 = LED Output
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA4, PA5, PA7 = LED output (Push-Pull) */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ======================= USART1 INIT ======================= */
/**
  * @brief USART1 Init: 115200 baud, 8N1 (giao tiep ESP-01)
  */
static void MX_USART1_UART_Init(void)
{
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
}

/* ======================= ERROR HANDLER ======================= */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
