/**
  ******************************************************************************
  * @file    bootloader_main.c
  * @brief   OTA Bootloader cho STM32F103C8T6 + ESP-01
  *          ROM: 0x08000000 - 0x080027FF (10KB)
  *          Kiem tra co OTA trong Backup Register DR1.
  *          Neu co = 0xDEAD: Tai firmware moi tu HTTP Server qua ESP-01
  *          Neu khong: Nhay thang vao App tai 0x08002800
  ******************************************************************************
  */

#include "stm32f10x.h"
#include <string.h>

/* ======================= DEFINES ======================= */
#define APP_ADDRESS         0x08002800U
#define OTA_FLAG_MAGIC      0xDEADU
#define FLASH_PAGE_SIZE     0x400U      /* 1KB per page */
#define FLASH_APP_END       0x08010000U /* 64KB total */

/* WiFi & Server config */
#define WIFI_SSID           "tuikycuc"
#define WIFI_PASS           "18032005"
#define HTTP_SERVER_IP      "172.20.10.7"
#define HTTP_SERVER_PORT    "8080"
#define FIRMWARE_PATH       "/update.bin"

/* ======================= GLOBALS ======================= */
static char rxBuf[512];
static uint16_t rxIdx;

/* ======================= CLOCK CONFIG ======================= */
static void SystemClock_Config(void)
{
    /* Dung HSI 8MHz (giong App) */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));
    RCC->CFGR = 0; /* HSI as SYSCLK, no prescalers */

    /* Flash latency 0 cho 8MHz */
    FLASH->ACR &= ~FLASH_ACR_LATENCY;

    /* Enable clocks cho GPIOA, GPIOB, USART1, AFIO, PWR, BKP */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_USART1EN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
}

/* ======================= GPIO ======================= */
static void GPIO_Init(void)
{
    /* PA9 = USART1_TX (AF Push-Pull, 50MHz) */
    GPIOA->CRH &= ~(0xF << 4);
    GPIOA->CRH |=  (0xB << 4); /* CNF=10 (AF PP), MODE=11 (50MHz) */

    /* PA10 = USART1_RX (Input Floating) */
    GPIOA->CRH &= ~(0xF << 8);
    GPIOA->CRH |=  (0x4 << 8); /* CNF=01 (floating), MODE=00 (input) */

    /* PA4, PA5, PA7 = LED outputs (Push-Pull) */
    GPIOA->CRL &= ~((0xF << 16) | (0xF << 20) | (0xF << 28));
    GPIOA->CRL |=  ((0x3 << 16) | (0x3 << 20) | (0x3 << 28));
}

/* ======================= LED ======================= */
static void LED_AllOn(void)
{
    GPIOA->BSRR = GPIO_BSRR_BS4 | GPIO_BSRR_BS5 | GPIO_BSRR_BS7;
}

static void LED_AllOff(void)
{
    GPIOA->BSRR = GPIO_BSRR_BR4 | GPIO_BSRR_BR5 | GPIO_BSRR_BR7;
}

/* ======================= DELAY ======================= */
static volatile uint32_t sysTick_ms = 0;

void SysTick_Handler(void)
{
    sysTick_ms++;
}

static void Delay_ms(uint32_t ms)
{
    uint32_t start = sysTick_ms;
    while ((sysTick_ms - start) < ms);
}

/* ======================= UART ======================= */
static void UART_Init(void)
{
    /* 115200 baud @ 8MHz HSI */
    USART1->BRR = 0x45;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void UART_SendByte(uint8_t b)
{
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = b;
}

static void UART_SendString(const char *str)
{
    while (*str) UART_SendByte(*str++);
}

static int UART_ReadByte(uint32_t timeout_ms)
{
    uint32_t start = sysTick_ms;
    while ((sysTick_ms - start) < timeout_ms)
    {
        if (USART1->SR & USART_SR_ORE)
        {
            /* Clear overrun by reading SR then DR */
            (void)USART1->SR;
            (void)USART1->DR;
        }
        if (USART1->SR & USART_SR_RXNE)
        {
            return (uint8_t)(USART1->DR & 0xFF);
        }
    }
    return -1;
}

static void UART_Flush(void)
{
    while (UART_ReadByte(10) >= 0);
}

/* ======================= ESP AT COMMANDS ======================= */
static uint8_t ESP_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    rxIdx = 0;
    memset(rxBuf, 0, sizeof(rxBuf));

    UART_Flush();
    UART_SendString(cmd);

    uint32_t start = sysTick_ms;
    while ((sysTick_ms - start) < timeout_ms)
    {
        int c = UART_ReadByte(50);
        if (c >= 0)
        {
            if (rxIdx < sizeof(rxBuf) - 1)
            {
                rxBuf[rxIdx++] = (char)c;
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
    Delay_ms(2000); /* Cho ESP-01 boot */

    if (!ESP_SendCmd("AT\r\n", "OK", 2000)) return 0;
    Delay_ms(100);
    ESP_SendCmd("ATE0\r\n", "OK", 1000);
    Delay_ms(100);

    if (!ESP_SendCmd("AT+CWMODE=1\r\n", "OK", 2000)) return 0;
    Delay_ms(100);

    /* Ket noi WiFi */
    char cmd[100];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    if (!ESP_SendCmd(cmd, "OK", 20000))
    {
        if (!ESP_SendCmd("AT+CIFSR\r\n", "STAIP", 3000)) return 0;
    }
    Delay_ms(500);

    ESP_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);
    Delay_ms(200);
    ESP_SendCmd("AT+CIPMUX=0\r\n", "OK", 2000);
    Delay_ms(200);

    /* Ket noi TCP den HTTP server */
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%s\r\n",
             HTTP_SERVER_IP, HTTP_SERVER_PORT);
    if (!ESP_SendCmd(cmd, "OK", 10000)) return 0;
    Delay_ms(500);

    return 1;
}

/* ======================= FLASH ======================= */
static void Flash_Unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

static void Flash_Lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void Flash_ErasePage(uint32_t pageAddr)
{
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = pageAddr;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PER;
}

static void Flash_WriteHalfWord(uint32_t addr, uint16_t data)
{
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)addr = data;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PG;
}

/* ======================= OTA DOWNLOAD ======================= */
static uint8_t OTA_Download(void)
{
    /* Gui HTTP GET request */
    char httpReq[128];
    snprintf(httpReq, sizeof(httpReq),
             "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",
             FIRMWARE_PATH, HTTP_SERVER_IP);

    /* Bat che do Transparent Mode de nhan nguyen ban file (khong bi chen +IPD) */
    ESP_SendCmd("AT+CIPMODE=1\r\n", "OK", 2000);
    Delay_ms(100);

    /* Vao che do gui nhan trong suot */
    UART_Flush();
    UART_SendString("AT+CIPSEND\r\n");

    /* Doi dau '>' */
    uint32_t start = sysTick_ms;
    uint8_t got = 0;
    while ((sysTick_ms - start) < 3000)
    {
        int c = UART_ReadByte(100);
        if (c == '>') { got = 1; break; }
    }
    if (!got) return 0;

    Delay_ms(10);
    UART_SendString(httpReq);

    /* ===== Doc HTTP response ===== */
    Flash_Unlock();

    /* Xoa toan bo vung App truoc */
    for (uint32_t addr = APP_ADDRESS; addr < FLASH_APP_END; addr += FLASH_PAGE_SIZE)
    {
        Flash_ErasePage(addr);
        /* Nhay LED de bao tien trinh */
        if (((addr - APP_ADDRESS) / FLASH_PAGE_SIZE) % 4 == 0)
        {
            LED_AllOn();
            Delay_ms(30);
            LED_AllOff();
        }
    }

    uint32_t writeAddr = APP_ADDRESS;
    uint8_t headerSkipped = 0;
    uint8_t pageBuf[1024];
    uint16_t bufPos = 0;
    uint8_t headerBuf[512];
    uint16_t hdrPos = 0;
    uint32_t totalReceived = 0;
    uint32_t lastDataTime = sysTick_ms;

    /* Doc cho den khi het data (timeout 10 giay khong co data moi) */
    while ((sysTick_ms - lastDataTime) < 10000)
    {
        int c = UART_ReadByte(100);
        if (c < 0) continue;
        lastDataTime = sysTick_ms;

        if (!headerSkipped)
        {
            /* Gom data vao headerBuf de tim \\r\\n\\r\\n */
            if (hdrPos < sizeof(headerBuf) - 1)
            {
                headerBuf[hdrPos++] = (uint8_t)c;
                headerBuf[hdrPos] = '\0';
            }

            /* Tim ket thuc HTTP header */
            char *endHdr = strstr((char *)headerBuf, "\r\n\r\n");
            if (endHdr)
            {
                headerSkipped = 1;
                /* Copy phan du lieu sau header vao pageBuf */
                char *dataStart = endHdr + 4;
                uint16_t remaining = hdrPos - (uint16_t)(dataStart - (char *)headerBuf);
                if (remaining > 0)
                {
                    memcpy(pageBuf, dataStart, remaining);
                    bufPos = remaining;
                }
            }
        }
        else
        {
            /* Ghi data vao buffer */
            pageBuf[bufPos++] = (uint8_t)c;

            /* Khi du 1 page (1KB), ghi vao Flash */
            if (bufPos >= FLASH_PAGE_SIZE)
            {
                for (uint16_t i = 0; i < FLASH_PAGE_SIZE; i += 2)
                {
                    uint16_t hw = pageBuf[i] | ((uint16_t)pageBuf[i + 1] << 8);
                    Flash_WriteHalfWord(writeAddr, hw);
                    writeAddr += 2;
                }
                totalReceived += FLASH_PAGE_SIZE;
                bufPos = 0;

                /* Nhay LED bao tien trinh */
                LED_AllOn();
                Delay_ms(20);
                LED_AllOff();
            }
        }
    }

    /* Ghi phan du cuoi cung (khong du 1 page) */
    if (bufPos > 0 && headerSkipped)
    {
        /* Pad voi 0xFF */
        while (bufPos < FLASH_PAGE_SIZE)
            pageBuf[bufPos++] = 0xFF;

        for (uint16_t i = 0; i < FLASH_PAGE_SIZE; i += 2)
        {
            uint16_t hw = pageBuf[i] | ((uint16_t)pageBuf[i + 1] << 8);
            Flash_WriteHalfWord(writeAddr, hw);
            writeAddr += 2;
        }
        totalReceived += bufPos;
    }

    Flash_Lock();

    /* Dong ket noi */
    ESP_SendCmd("AT+CIPCLOSE\r\n", "OK", 2000);

    return (totalReceived > 0) ? 1 : 0;
}

/* ======================= JUMP TO APP ======================= */
static void JumpToApp(void)
{
    /* Kiem tra App co hop le khong (Stack Pointer phai nam trong SRAM) */
    uint32_t appStack = *(volatile uint32_t *)APP_ADDRESS;
    if ((appStack & 0x20000000) != 0x20000000)
    {
        /* App khong hop le, o lai bootloader */
        return;
    }

    /* Disable tat ca interrupt */
    __disable_irq();

    /* Reset SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    /* Set Vector Table offset cho App */
    SCB->VTOR = APP_ADDRESS;

    /* Set Main Stack Pointer */
    __set_MSP(appStack);

    /* Nhay vao Reset Handler cua App */
    uint32_t appEntry = *(volatile uint32_t *)(APP_ADDRESS + 4);
    void (*app_reset_handler)(void) = (void (*)(void))appEntry;
    app_reset_handler();
}

/* ======================= MAIN ======================= */
int main(void)
{
    SystemClock_Config();
    GPIO_Init();

    /* SysTick 1ms */
    SysTick_Config(8000); /* 8MHz / 8000 = 1kHz = 1ms */

    /* Doc cờ OTA tu SRAM */
    uint32_t otaFlag = *((volatile uint32_t *)0x20004000);

    if (otaFlag == 0xDEADBEEF)
    {
        /* Co OTA duoc set! Xoa co truoc (tranh loop vo han) */
        *((volatile uint32_t *)0x20004000) = 0x00000000;

        /* Bao hieu dang OTA: Nhay LED nhanh */
        for (int i = 0; i < 5; i++)
        {
            LED_AllOn();
            Delay_ms(100);
            LED_AllOff();
            Delay_ms(100);
        }

        /* Init UART & ESP */
        UART_Init();

        if (ESP_Init_Boot())
        {
            /* Tai firmware */
            if (OTA_Download())
            {
                /* Thanh cong! Nhay LED cham 3 lan */
                for (int i = 0; i < 3; i++)
                {
                    LED_AllOn();
                    Delay_ms(500);
                    LED_AllOff();
                    Delay_ms(500);
                }
            }
            else
            {
                /* That bai download: Nhay LED nhanh 10 lan */
                for (int i = 0; i < 10; i++)
                {
                    LED_AllOn();
                    Delay_ms(50);
                    LED_AllOff();
                    Delay_ms(50);
                }
            }
        }
        else
        {
            /* That bai ESP Init: Nhay LED nhanh 20 lan */
            for (int i = 0; i < 20; i++)
            {
                LED_AllOn();
                Delay_ms(50);
                LED_AllOff();
                Delay_ms(50);
            }
        }

        /* Reset de nhay vao App (du thanh cong hay that bai) */
        NVIC_SystemReset();
    }
    else
    {
        /* Khong co yeu cau OTA - Nhay thang vao App */
        PWR->CR &= ~PWR_CR_DBP;
        JumpToApp();
    }

    /* Fallback: Neu JumpToApp that bai */
    while (1)
    {
        LED_AllOn();
        Delay_ms(1000);
        LED_AllOff();
        Delay_ms(1000);
    }
}
