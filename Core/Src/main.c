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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE BEGIN PV */
//改表，根据提供硬件资料中，数码管共阳，共8个LED定义，根据图中定义转译出seg_table，dp小数点定义为隔位闪烁
const uint8_t seg_table[10] =
{
  0xC0, //0  dp off
  0x79, //1  dp on
  0xA4, //2  dp off
  0x30, //3  dp on
  0x99, //4  dp off
  0x12, //5  dp on
  0x82, //6  dp off
  0x78, //7  dp on
  0x80, //8  dp off
  0x10  //9  dp on
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void SEG_Display(uint8_t num)
{
  uint8_t data = seg_table[num];

  // 使用HAL轮询，方便调试，共阳->SET灭灯
  HAL_GPIO_WritePin(GPIOA,
                    GPIO_PIN_0 |
                    GPIO_PIN_1 |
                    GPIO_PIN_2 |
                    GPIO_PIN_3 |
                    GPIO_PIN_4 |
                    GPIO_PIN_5 |
                    GPIO_PIN_6 |
                    GPIO_PIN_7,
                    GPIO_PIN_SET);

  // 根据Table转译调整对应LED变亮(RESET)
  for(int i=0;i<8;i++)
  {
    if(!(data & (1<<i)))
    {
      HAL_GPIO_WritePin(GPIOA,
                        (GPIO_PIN_0<<i),
                        GPIO_PIN_RESET);
    }
  }
}

uint8_t IR_Right_IsBlack(void)
{
  return HAL_GPIO_ReadPin(IR_Right_GPIO_Port, IR_Right_Pin)
         == GPIO_PIN_SET;
}

uint8_t IR_Left_IsBlack(void)
{
  return HAL_GPIO_ReadPin(IR_Left_GPIO_Port, IR_Left_Pin)
         == GPIO_PIN_SET;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  // 蜂鸣器提示切换定义
  #define BUZZER_ON()   HAL_GPIO_WritePin(Onboard_Buzzer_GPIO_Port, Onboard_Buzzer_Pin, GPIO_PIN_SET)
  #define BUZZER_OFF()  HAL_GPIO_WritePin(Onboard_Buzzer_GPIO_Port, Onboard_Buzzer_Pin, GPIO_PIN_RESET)
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  // 归零num变量
  uint8_t num=0;
  //int16_t currentSpeed=80;

  // 启用L298N驱动的TIM4_PWM
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

  // L298N驱动电机实现，配置两个电机的SetSpeed可调速度，范围为-99~99
  // 电机A（右轮）：IN1接PB6（CH1），IN2接PB7（CH2）
  // 电机B（左轮）：IN3接PB8（CH3），IN4接PB9（CH4）
  // speed: 0~99，正值正转，负值反转
  void MotorA_SetSpeed(int16_t speed) {
    if (speed >= 0) {
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, speed);
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0);
    } else {
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, -speed);
    }
  }

  void MotorB_SetSpeed(int16_t speed) {
    if (speed >= 0) {
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, speed);
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
    } else {
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
      __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, -speed);
    }
  }

  void Line_Follow(void)
  {
    uint8_t leftBlack  = IR_Left_IsBlack();
    uint8_t rightBlack = IR_Right_IsBlack();

    if (!leftBlack && !rightBlack)
    {
      BUZZER_OFF();
      SEG_Display(0);
      // 白 白
      MotorA_SetSpeed(50);
      MotorB_SetSpeed(50);
    }
    else if (leftBlack && !rightBlack)
    {
      BUZZER_ON();
      SEG_Display(1);
      // 黑 白：向左修正
      MotorA_SetSpeed(50);
      MotorB_SetSpeed(-50);
    }
    else if (!leftBlack && rightBlack)
    {
      BUZZER_ON();
      SEG_Display(2);
      // 白 黑：向右修正
      MotorA_SetSpeed(-50);
      MotorB_SetSpeed(50);
    }
    else
    {
      BUZZER_OFF();
      SEG_Display(8);
      // 黑 黑
      //MotorA_SetSpeed(0);
      //MotorB_SetSpeed(0);
    }
  }

  // 初始电机状态（正转）
  //MotorA_SetSpeed(currentSpeed);
  //MotorB_SetSpeed(currentSpeed);
  //SEG_Display(5);                // 显示5代表正转
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    Line_Follow();
    HAL_Delay(5);
  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
