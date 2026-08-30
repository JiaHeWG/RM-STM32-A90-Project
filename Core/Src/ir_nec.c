/**
******************************************************************************
  * @file           : ir_nec.c
  * @brief          : Support of NEC-based IR Remote Control on STM32
  * @Author         : JiaHeWG
  * @Date           : Aug 29, 2026
  ******************************************************************************
  */

#include "ir_nec.h"
#include "tim.h"

typedef enum
{
    IR_STATE_IDLE = 0,
    IR_STATE_LEADER,
    IR_STATE_DATA
} IR_State_t;

// Static variables
static IR_State_t ir_state = IR_STATE_IDLE;
static uint16_t ir_last_time = 0;
static GPIO_PinState ir_last_pin = GPIO_PIN_SET;

static uint32_t ir_data = 0;
static uint8_t ir_bit_count = 0;
static uint32_t ir_last_valid_raw = 0;
static uint8_t ir_has_last_key = 0;
static uint8_t ir_result_ready = 0;

/**
 * @brief 获取TIM2当前计数值
 */
static uint16_t IR_GetTick(void)
{
    return (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
}

/**
 * @brief 重置当前计数状态
 */
static void IR_ResetReceiver(void)
{
    ir_state = IR_STATE_IDLE;
    ir_bit_count = 0;
    ir_data = 0;
}

/**
 * @brief 初始化，包含TIM2初始化
 */
void IR_Init(void)
{
    HAL_TIM_Base_Start(&htim2);

    ir_last_time = IR_GetTick();
    ir_last_pin = HAL_GPIO_ReadPin(IR_PORT, IR_PIN);

    IR_ResetReceiver();

    ir_last_valid_raw = 0;
    ir_has_last_key = 0;
    ir_result_ready = 0;
}

/**
 * @brief 通过EXTI获取边际数据，Rising/Falling切换，详见红外遥控原理
 */
void IR_HandleEXTI(void)
{
    uint16_t now;
    uint16_t duration;
    GPIO_PinState pin;

    now = IR_GetTick();
    pin = HAL_GPIO_ReadPin(IR_PORT, IR_PIN);

    duration = (uint16_t)(now - ir_last_time);
    ir_last_time = now;

    // Rising Edge
    if (ir_last_pin == GPIO_PIN_RESET &&
        pin == GPIO_PIN_SET)
    {
        switch (ir_state)
        {
        case IR_STATE_IDLE:
            // NEC Leader Low
            if (duration >= 8000 &&
                duration <= 10000)
            {
                ir_state = IR_STATE_LEADER;
                ir_bit_count = 0;
                ir_data = 0;
            }

            break;


        case IR_STATE_DATA:
            /* NEC Data Low
             * Giving up when timing abnormal */
            if (duration < 300 ||
                duration > 800)
            {
                IR_ResetReceiver();
            }

            break;


        default:
            break;
        }
    }

    // Falling Edge
    else if (ir_last_pin == GPIO_PIN_SET &&
        pin == GPIO_PIN_RESET)
    {
        switch (ir_state)
        {
        case IR_STATE_LEADER:

            // NEC Data Frame
            if (duration >= 4000 &&
                duration <= 5000)
            {
                ir_state = IR_STATE_DATA;
                ir_bit_count = 0;
                ir_data = 0;
            }

            // NEC Repeat
            else if (duration >= 1800 &&
                duration <= 2800)
            {
                if (ir_has_last_key)
                {
                    ir_result_ready = 1;
                }
                IR_ResetReceiver();
            }

            else
            {
                // Abnormal data (possibly not NEC, throw up
                IR_ResetReceiver();
            }

            break;

        // Data Location
        case IR_STATE_DATA:
            {
                uint8_t bit;
                // "0" in BIN
                if (duration >= 400 &&
                    duration <= 800)
                {
                    bit = 0;
                }
                // "1" in BIN
                else if (duration >= 1000 &&
                    duration <= 1800)
                {
                    bit = 1;
                }

                // Abnormal HIGH Signal
                else
                {
                    IR_ResetReceiver();
                    break;
                }
                ir_data = (ir_data << 1) | bit;
                ir_bit_count++;

                /* Finish receiving data
                 * spitting for debug */
                if (ir_bit_count == 32)
                {
                    uint8_t address;
                    uint8_t address_inv;
                    uint8_t command;
                    uint8_t command_inv;


                    address =
                        (uint8_t)((ir_data >> 24) & 0xFF);

                    address_inv =
                        (uint8_t)((ir_data >> 16) & 0xFF);

                    command =
                        (uint8_t)((ir_data >> 8) & 0xFF);

                    command_inv =
                        (uint8_t)(ir_data & 0xFF);


                    // One’s Complement Sum Verification
                    if (((uint8_t)~address_inv == address) &&
                        ((uint8_t)~command_inv == command))
                    {
                        // Valid, new key output
                        ir_last_valid_raw = ir_data;
                        ir_has_last_key = 1;
                        ir_result_ready = 1;
                    }
                    // Force close the frame, avoid stucking
                    IR_ResetReceiver();
                }

                break;
            }


        default:
            break;
        }
    }
    ir_last_pin = pin;
}

/* Repeat Key: Continuing send original data but not "repeat" raw message
 * Simplify the IR usage in factoring
 * Note: The repeat message should be the raw message from remote */
/**
 * @brief 获取原始键码接口，已简化
 */
uint32_t IR_GetRawData(void)
{
    uint32_t raw = 0;

    if (ir_result_ready)
    {
        raw = ir_last_valid_raw;
        ir_result_ready = 0;
    }

    return raw;
}

/**
 * @brief 清除暂存位结果
 */
void IR_ClearResult(void)
{
    ir_result_ready = 0;
}
