/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "adc.h"
#include "usart.h"
#include "stdio.h"
#include "tim.h"
#include "gpio.h"
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
/* USER CODE BEGIN Variables */
volatile uint32_t adc_value = 0;
volatile uint8_t prev_but1=1,prev_but2=1,prev_but3=1;
uint32_t cpr=1562;
uint16_t prev_cnt=0;
typedef enum mode_motor{
	FORWARD,
	REVERSE,
	STOP
} mode_motor;
typedef struct infor{
	mode_motor mode;
	uint32_t adc_value;
	float pwm;
	float positionDeg; 
	float rpm;
} infor;
infor inf;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for InputTask */
osThreadId_t InputTaskHandle;
const osThreadAttr_t InputTask_attributes = {
  .name = "InputTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for MotorTask */
osThreadId_t MotorTaskHandle;
const osThreadAttr_t MotorTask_attributes = {
  .name = "MotorTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for EncoderTask */
osThreadId_t EncoderTaskHandle;
const osThreadAttr_t EncoderTask_attributes = {
  .name = "EncoderTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for ESPTask */
osThreadId_t ESPTaskHandle;
const osThreadAttr_t ESPTask_attributes = {
  .name = "ESPTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for OTATask */
osThreadId_t OTATaskHandle;
const osThreadAttr_t OTATask_attributes = {
  .name = "OTATask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for motorCommandQueue */
osMessageQueueId_t motorCommandQueueHandle;
const osMessageQueueAttr_t motorCommandQueue_attributes = {
  .name = "motorCommandQueue"
};
/* Definitions for systemStateMutex */
osMutexId_t systemStateMutexHandle;
const osMutexAttr_t systemStateMutex_attributes = {
  .name = "systemStateMutex"
};
/* Definitions for myBinarySem01 */
osSemaphoreId_t myBinarySem01Handle;
const osSemaphoreAttr_t myBinarySem01_attributes = {
  .name = "myBinarySem01"
};
/* Definitions for myEvent01 */
osEventFlagsId_t myEvent01Handle;
const osEventFlagsAttr_t myEvent01_attributes = {
  .name = "myEvent01"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void OTA_TriggerUpdate(void);
void Off_Led(void);
uint32_t Read_ADC();
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void InputTask_function(void *argument);
void MotorTask_function(void *argument);
void EncoderTask_function(void *argument);
void ESPTask_function(void *argument);
void OTATask_function(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of systemStateMutex */
  systemStateMutexHandle = osMutexNew(&systemStateMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of myBinarySem01 */
  myBinarySem01Handle = osSemaphoreNew(1, 1, &myBinarySem01_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of motorCommandQueue */
  motorCommandQueueHandle = osMessageQueueNew (16, sizeof(mode_motor), &motorCommandQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of InputTask */
  InputTaskHandle = osThreadNew(InputTask_function, NULL, &InputTask_attributes);

  /* creation of MotorTask */
  MotorTaskHandle = osThreadNew(MotorTask_function, NULL, &MotorTask_attributes);

  /* creation of EncoderTask */
  EncoderTaskHandle = osThreadNew(EncoderTask_function, NULL, &EncoderTask_attributes);

  /* creation of ESPTask */
  ESPTaskHandle = osThreadNew(ESPTask_function, NULL, &ESPTask_attributes);

  /* creation of OTATask */
  OTATaskHandle = osThreadNew(OTATask_function, NULL, &OTATask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* creation of myEvent01 */
  myEvent01Handle = osEventFlagsNew(&myEvent01_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_InputTask_function */
/**
* @brief Function implementing the InputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_InputTask_function */
void InputTask_function(void *argument)
{
  /* USER CODE BEGIN InputTask_function */
		mode_motor cmd;
    for (;;)
    {
				uint8_t cur_but1=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_0);
				uint8_t cur_but2=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_10);
				uint8_t cur_but3=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_11);
				uint32_t adc_raw = Read_ADC();
				float duty = ((float)adc_raw * 100.0f) / 4095.0f;

				osMutexAcquire(systemStateMutexHandle, osWaitForever);
				inf.adc_value = adc_raw;
				inf.pwm = duty;
				if(cur_but1==0&&prev_but1==1){
					cmd = FORWARD;
					inf.mode = FORWARD;
					osMessageQueuePut(motorCommandQueueHandle,&cmd,NULL,0);
				}
				if(cur_but2==0&&prev_but2==1){
					cmd = REVERSE;
					inf.mode = REVERSE;
					osMessageQueuePut(motorCommandQueueHandle,&cmd,NULL,0);
				}
				if(cur_but3==0&&prev_but3==1){
					cmd = STOP;
					inf.mode = STOP;
					osMessageQueuePut(motorCommandQueueHandle,&cmd,NULL,0);
				}
				osMutexRelease(systemStateMutexHandle);

				prev_but1=cur_but1;
				prev_but2=cur_but2;
				prev_but3=cur_but3;
        osDelay(10);
    }
  /* USER CODE END InputTask_function */
}

/* USER CODE BEGIN Header_MotorTask_function */
/**
* @brief Function implementing the MotorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_MotorTask_function */
void MotorTask_function(void *argument)
{
  /* USER CODE BEGIN MotorTask_function */
  /* Infinite loop */
	mode_motor cmd;
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
	mode_motor cur_mode=STOP;
	float velo=0.0f;
	uint16_t timeout=0;
	uint32_t ccr=0;
	uint32_t arr=0;
  for(;;)
  {
    if (osMessageQueueGet(motorCommandQueueHandle,&cmd,NULL,10) == osOK){
			switch (cmd)
				{
					case FORWARD:
						if(cur_mode!=FORWARD){
							__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,0);
							timeout=0;
						while(1){
							osMutexAcquire(systemStateMutexHandle, osWaitForever);
							velo=inf.rpm;
							osMutexRelease(systemStateMutexHandle);
							if (velo<10 && velo >-10){
								break;
							}
							osDelay(10);
							timeout++;
              if (timeout >= 100)
                 {
                    break;
                  }
						}
					}
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_SET);
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);
						Off_Led();
						HAL_GPIO_WritePin(GPIOA,GPIO_PIN_7,GPIO_PIN_SET);
						cur_mode=cmd;
            break;

          case REVERSE:
						if(cur_mode!=REVERSE){
							__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,0);
							timeout=0;
						while(1){
							osMutexAcquire(systemStateMutexHandle, osWaitForever);
							velo=inf.rpm;
							osMutexRelease(systemStateMutexHandle);
							if (velo<10 && velo >-10){
								break;
							}
							osDelay(10);
							timeout++;
              if (timeout >= 100)
                 {
                    break;
                  }
						}
					}
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_SET);
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_SET);
						Off_Led();
						HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,GPIO_PIN_SET);
						cur_mode=cmd;
            break;

          case STOP:
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB,GPIO_PIN_4,GPIO_PIN_RESET);
						HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);		
						Off_Led();
						HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);
						cur_mode=cmd;
            break;
          }	
			}
			osMutexAcquire(systemStateMutexHandle, osWaitForever);
			float duty=inf.pwm;
			mode_motor mode=cur_mode;
			osMutexRelease(systemStateMutexHandle);
			if(mode==STOP){
				__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,0);}
			else{
			arr =__HAL_TIM_GET_AUTORELOAD(&htim1);
			ccr =(uint32_t)((duty* arr) / 100.0f);
			__HAL_TIM_SET_COMPARE(&htim1,TIM_CHANNEL_1,ccr);
			}
  }
  /* USER CODE END MotorTask_function */
}

/* USER CODE BEGIN Header_EncoderTask_function */
/**
* @brief Function implementing the EncoderTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_EncoderTask_function */
void EncoderTask_function(void *argument)
{
  /* USER CODE BEGIN EncoderTask_function */
  /* Infinite loop */
	HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  for(;;)
  {
		osMutexAcquire(systemStateMutexHandle, osWaitForever);
		uint16_t total_cnt=__HAL_TIM_GET_COUNTER(&htim2);
		inf.positionDeg=((float)total_cnt * 360.0f)/cpr;
		inf.rpm =((float)((int16_t)(total_cnt-prev_cnt)) * 6000.0f) / cpr;
		prev_cnt=total_cnt;
		osMutexRelease(systemStateMutexHandle);
		osDelay(10);
  }
  /* USER CODE END EncoderTask_function */
}

/* USER CODE BEGIN Header_ESPTask_function */
/**
* @brief Function implementing the ESPTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_ESPTask_function */
void ESPTask_function(void *argument)
{
  /* USER CODE BEGIN ESPTask_function */
  /* Infinite loop */
	char txbuf[128];
  uint8_t esp_connected = 0;

  /* Cho ESP-01 boot va auto-connect WiFi */
  osDelay(2000);

  extern uint8_t esp_step;
  
  uint8_t fail_count = 0;
  uint8_t connection_lost = 0;

  /* Thu ket noi lien tuc cho den khi duoc */
  while(!esp_connected){
    if(ESP_Init("172.20.10.7", 8000)){
      esp_connected = 1;
    } else {
      /* Nhay LED de bao loi o buoc thu esp_step */
      for(int i = 0; i < esp_step; i++){
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_SET);
          osDelay(200);
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);
          osDelay(200);
      }
      osDelay(3000);
    }
  }

  /* Doi TCP connection on dinh truoc khi gui */
  osDelay(500);

  for(;;)
  {
    /* ====== XU LY RECONNECT NEU CONNECTION MAT ====== */
    if(!esp_connected || connection_lost){
      connection_lost = 0;
      esp_connected = 0;
      ESP_SendCommand("AT+CIPCLOSE\r\n", "OK", 1000);
      osDelay(500);
      if(ESP_Init("172.20.10.7", 8000)){
        esp_connected = 1;
        fail_count = 0;
        osDelay(300);
      } else {
        osDelay(2000);
        continue;
      }
    }

    /* ====== DOC DATA VA GUI ====== */
    osMutexAcquire(systemStateMutexHandle, osWaitForever);
    int mode_val    = (int)inf.mode;
    uint32_t adc_val = inf.adc_value;
    
    int duty_int    = (int)inf.pwm;
    int duty_dec    = (int)((inf.pwm - duty_int) * 10);
    if(duty_dec < 0) duty_dec = -duty_dec;
    
    const char* pos_sign = (inf.positionDeg < 0) ? "-" : "";
    float abs_pos   = (inf.positionDeg < 0) ? -inf.positionDeg : inf.positionDeg;
    int pos_int     = (int)abs_pos;
    int pos_dec     = (int)((abs_pos - pos_int) * 100);
    
    const char* rpm_sign = (inf.rpm < 0) ? "-" : "";
    float abs_rpm   = (inf.rpm < 0) ? -inf.rpm : inf.rpm;
    int rpm_int     = (int)abs_rpm;
    int rpm_dec     = (int)((abs_rpm - rpm_int) * 100);
    osMutexRelease(systemStateMutexHandle);

    snprintf(txbuf, sizeof(txbuf),
      "Boot : Mode:%d,ADC:%u,Duty:%d.%d,Pos:%s%d.%02d,RPM:%s%d.%02d\n",
      mode_val, (unsigned int)adc_val,
      duty_int, duty_dec,
      pos_sign, pos_int, pos_dec,
      rpm_sign, rpm_int, rpm_dec);
    
    uint8_t result = ESP_SendData(txbuf);
    
    if(result == 1){
      /* Gui thanh cong */
      fail_count = 0;
    } 
    else if(result == 2){
      /* Connection CLOSED - reconnect ngay */
      connection_lost = 1;
      fail_count = 0;
    }
    else {
      /* Send fail - thu lai */
      fail_count++;
      if(fail_count >= 5){
        /* 5 lan fail -> reconnect */
        connection_lost = 1;
        fail_count = 0;
      }
      osDelay(50);
    }
    
    osDelay(80);
  }
  /* USER CODE END ESPTask_function */
}

/* USER CODE BEGIN Header_OTATask_function */
/**
* @brief Function implementing the OTATask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_OTATask_function */
void OTATask_function(void *argument)
{
  /* USER CODE BEGIN OTATask_function */
  /* Nhan giu PB12 trong 3 giay de kich hoat OTA */
  /* PB12 pull-down, noi VCC -> nhan = HIGH (GPIO_PIN_SET) */
  uint8_t was_pressed = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET);
  for(;;)
  {
    uint8_t is_pressed = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET);

    /* Chi kich hoat khi co su thay doi tu NHA sang NHAN (Edge Detection) */
    if (is_pressed && !was_pressed)
    {
      uint32_t press_start = HAL_GetTick();
      uint8_t held = 1;

      while (held && (HAL_GetTick() - press_start) < 3000)
      {
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) != GPIO_PIN_SET)
        {
          held = 0;
        }
        osDelay(50);
      }

      if (held)
      {
        /* Nhay LED xac nhan OTA truoc khi reset */
        for (int i = 0; i < 10; i++)
        {
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_SET);
          osDelay(100);
          HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);
          osDelay(100);
        }
        
        /* Ghi co OTA va Reset */
        OTA_TriggerUpdate();
        
        /* Neu vi ly do nao do chua reset duoc thi doi nha nut ra */
        while(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET)
        {
             osDelay(100);
        }
      }	
    }
    
    was_pressed = is_pressed;
    osDelay(100);
  }
  /* USER CODE END OTATask_function */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

