/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : RoboMaster User Code for Trial Run
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ir_nec.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// IR Remote State Definition
typedef enum
{
    MODE_LINE = 1,
    MODE_REMOTE,
    MODE_STOP
} ControlMode_t;

// Default for emergency_stop, stop when powerup
static ControlMode_t mode = MODE_STOP;
static uint8_t emergency_stop = 1;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// IR Remote Code Definition
// Model: CMCC Smart Controller (universal)
#define IR_KEY_POWER   0x44BB3BC4UL
#define IR_KEY_MENU    0x44BB41BEUL
#define IR_KEY_LEFT    0x44BB9966UL
#define IR_KEY_UP      0x44BB53ACUL
#define IR_KEY_RIGHT   0x44BB837CUL
#define IR_KEY_ENTER   0x44BB738CUL
#define IR_KEY_DOWN    0x44BB4BB4UL
#define IR_KEY_1       0x44BB49B6UL
#define IR_KEY_2       0x44BBC936UL
#define IR_KEY_3       0x44BB33CCUL
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 共阳数码管段码
 * Note: 这里起初按照项目要求设置了0~9，且奇数数字有右下角亮点，后续为调试增加至F */
static const uint8_t seg_table[16] =
{
    0xC0, // 0
    0xF9, // 1
    0xA4, // 2
    0xB0, // 3
    0x99, // 4
    0x92, // 5
    0x82, // 6
    0xF8, // 7
    0x80, // 8
    0x90, // 9
    0x88, // A
    0x83, // b
    0xC6, // C
    0xA1, // d
    0x86, // E
    0x8E, // F
};

/* 遥控安全超时：超过此时间没有收到红外信号就停车，单位毫秒 */
#define REMOTE_TIMEOUT_MS 200

static uint32_t remote_last_time = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Motor_SetSpeed(int16_t right, int16_t left);
static void Line_Follow(void);
static void SEG_Display(uint8_t num);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief 设置数码管显示
 */
static void SEG_Display(uint8_t num)
{
    if (num > 15)
        return;

    /* 共阳数码管：先全部熄灭 */
    HAL_GPIO_WritePin(
        GPIOA,
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
        GPIO_PIN_SET
    );

    /* 点亮对应段 */
    uint8_t data = seg_table[num];

    for (uint8_t i = 0; i < 8; i++)
    {
        if (!(data & (1U << i)))
        {
            HAL_GPIO_WritePin(
                GPIOA,
                (GPIO_PIN_0 << i),
                GPIO_PIN_RESET
            );
        }
    }
}

/**
 * @brief 蜂鸣器开
 */
static void BUZZER_ON(void)
{
    HAL_GPIO_WritePin(
        Onboard_Buzzer_GPIO_Port,
        Onboard_Buzzer_Pin,
        GPIO_PIN_SET
    );
}

/**
 * @brief 蜂鸣器关
 */
static void BUZZER_OFF(void)
{
    HAL_GPIO_WritePin(
        Onboard_Buzzer_GPIO_Port,
        Onboard_Buzzer_Pin,
        GPIO_PIN_RESET
    );
}

/**
 * @brief 设置左右电机速度
 *
 * @param right 右轮速度，范围 -99 ~ 99
 * @param left  左轮速度，范围 -99 ~ 99
 *
 * 正值：正转
 * 负值：反转
 * 0：停止
 */
static void Motor_SetSpeed(int16_t right, int16_t left)
{
    /* 限制速度范围 */
    // Add by ChatGPT
    if (right > 99) right = 99;
    if (right < -99) right = -99;
    if (left > 99) left = 99;
    if (left < -99) left = -99;

    /* 右轮：TIM4 CH1 / CH2 */
    if (right >= 0)
    {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, right);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0);
    }
    else
    {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, -right);
    }

    /* 左轮：TIM4 CH3 / CH4 */
    if (left >= 0)
    {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, left);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
    }
    else
    {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, -left);
    }
}

/**
 * @brief 巡线控制
 *
 * 传感器逻辑：
 *   0 = 白
 *   1 = 黑
 *
 *       左 右
 * 白白  → 直行
 * 黑白  → 左转
 * 白黑  → 右转
 * 黑黑  → 保持当前状态
 * Note: 其实这里应该继续微调，但是现在先这样吧，14500坏了导致输出电压不稳，电机速度接着不稳，此时调整没意义
 */
static void Line_Follow(void)
{
    uint8_t left_black =
        (HAL_GPIO_ReadPin(IR_Left_GPIO_Port, IR_Left_Pin) == GPIO_PIN_SET);

    uint8_t right_black =
        (HAL_GPIO_ReadPin(IR_Right_GPIO_Port, IR_Right_Pin) == GPIO_PIN_SET);

    if (!left_black && !right_black)
    {
        /* 白白：直行 */
        BUZZER_OFF();
        SEG_Display(10); // A
        Motor_SetSpeed(60, 60);
    }
    else if (left_black && !right_black)
    {
        /* 黑白：向左修正 */
        BUZZER_ON();
        SEG_Display(11); // b
        Motor_SetSpeed(50, -20);
    }
    else if (!left_black && right_black)
    {
        /* 白黑：向右修正 */
        BUZZER_ON();
        SEG_Display(12); // C
        Motor_SetSpeed(-20, 50);
    }
    else
    {
        /* 黑黑：可能脱圈，尝试旋转寻回*测试* */
        BUZZER_OFF();
        SEG_Display(15); // F
        Motor_SetSpeed(50, -20);
    }
}

/**
 * @brief 红外中断响应回传
 *
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == IR_PIN)
    {
        IR_HandleEXTI();
    }
}

/**
 * @brief 处理遥控器运动按键
 *
 * 每次收到方向键时刷新 remote_last_time。
 * 如果遥控器停止发送，主循环中的超时检测会自动停车。
 */
static void Remote_Move(int16_t right, int16_t left, uint8_t display)
{
    Motor_SetSpeed(right, left);
    SEG_Display(display);

    /* 收到有效运动指令，刷新遥控安全计时器 */
    remote_last_time = HAL_GetTick();
}

/** * @brief 红外接收帧处理 */
static void IR_ProcessKey(uint32_t key)
{
    static uint32_t last_power_time = 0;
    uint32_t now = HAL_GetTick();
    /* ========================= * 电源键：全局急停 * ========================= */
    if (key == IR_KEY_POWER)
    {
        // Take a static variable to handle special power button hit
        if (now - last_power_time < 250)
        {
            return; // long-press, ignore
        }
        last_power_time = now; // update tick
        // 切换状态值
        emergency_stop = !emergency_stop;
        if (emergency_stop)
        {
            /* 进入急停 */
            BUZZER_OFF();
            Motor_SetSpeed(0, 0);
            SEG_Display(13);
        }
        else
        {
            /* 解除急停，但不自动恢复运动 */
            Motor_SetSpeed(0, 0);
            remote_last_time = HAL_GetTick();
        }
        return;
    } /* 急停时屏蔽其他按键 */
    if (emergency_stop) return;
    /* ========================= * 模式选择 * ========================= */
    switch (key)
    {
    case IR_KEY_1: mode = MODE_LINE;
        Motor_SetSpeed(0, 0);
        SEG_Display(1);
        break;
    case IR_KEY_2: mode = MODE_REMOTE;
        BUZZER_OFF();
        Motor_SetSpeed(0, 0); /* 切换到遥控模式后，从停车状态开始计时 */
        remote_last_time = HAL_GetTick();
        SEG_Display(2);
        break;
    case IR_KEY_3: mode = MODE_STOP;
        BUZZER_OFF();
        Motor_SetSpeed(0, 0);
        SEG_Display(0);
        break;
    /* ========================= * 遥控运动 * =========================
     * 通过封装Remote_Move简化代码结构，参数分别为(左轮功率，右轮功率，段码显示 */
    case IR_KEY_UP: if (mode == MODE_REMOTE) { Remote_Move(80, 80, 8); }
        break;
    case IR_KEY_DOWN: if (mode == MODE_REMOTE) { Remote_Move(-80, -80, 6); }
        break;
    case IR_KEY_LEFT: if (mode == MODE_REMOTE) { Remote_Move(80, -80, 7); }
        break;
    case IR_KEY_RIGHT: if (mode == MODE_REMOTE) { Remote_Move(-80, 80, 9); }
        break;
    case IR_KEY_ENTER: if (mode == MODE_REMOTE) { Remote_Move(0, 0, 14); }
        break;
    /* ========================= * 测试功能 * ========================= */
    case IR_KEY_MENU: BUZZER_ON();
        HAL_Delay(50);
        BUZZER_OFF();
        break;
    default: break;
    }
}

//Status 0 For Display Self Test
static void SEG_SelfTest(void)
{
    static uint32_t last_tick = 0;
    static uint8_t digit = 0;

    if (HAL_GetTick() - last_tick >= 200)
    {
        last_tick = HAL_GetTick();

        SEG_Display(digit);

        digit++;

        if (digit > 15)
            digit = 0;
    }
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

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_TIM4_Init();
    MX_TIM2_Init();
    MX_USART1_UART_Init();
    /* USER CODE BEGIN 2 */
    IR_Init();
    // Already init TIM2 in IR_Init(), ignore.
    //HAL_TIM_Base_Start(&htim2);

    /* 启动 L298N PWM */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

    /* 初始停止 */
    Motor_SetSpeed(0, 0);
    SEG_Display(13); // d
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        uint32_t key = IR_GetRawData();

        if (key != 0)
        {
            IR_ProcessKey(key);
            IR_ClearResult();
        }

        if (!emergency_stop)
        {
            switch (mode)
            {
            case MODE_LINE:
                Line_Follow();
                break;

            case MODE_REMOTE:
                /*
                 * 遥控安全超时
                 *
                 * 如果超过 REMOTE_TIMEOUT_MS 没有收到新的
                 * 遥控运动指令，则自动停车。
                 */
                if (HAL_GetTick() - remote_last_time >= REMOTE_TIMEOUT_MS)
                {
                    SEG_Display(14); // Show E as stop sign, different from that in other modes.
                    Motor_SetSpeed(0, 0);
                }
                break;

            case MODE_STOP:
                Motor_SetSpeed(0, 0);
                SEG_SelfTest();
                break;

            default:
                mode = MODE_STOP;
                Motor_SetSpeed(0, 0);
                break;
            }
        }
        else
        {
            /* 急停状态：无条件关闭电机 */
            Motor_SetSpeed(0, 0);
        }

        HAL_Delay(1);
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
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

/* USER CODE BEGIN 4 */
#include <stdio.h>
#ifdef __GNUC__
int _write(int file, char* ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}
#else
int fputc(int ch, FILE* f)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif
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
void assert_failed(uint8_t* file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
