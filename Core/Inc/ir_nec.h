/**
******************************************************************************
  * @file           : ir_nec.h
  * @brief          : Library for ir_nec.c
  * @Auther         : JiaHeWG
  * @Date           : Aug 29, 2026
  ******************************************************************************
  */

#ifndef __IR_NEC_H
#define __IR_NEC_H

#include "main.h"
#include <stdint.h>

/* IR Remote Receiver Definition */
#define IR_PIN   GPIO_PIN_8
#define IR_PORT  GPIOA

void IR_Init(void);
void IR_HandleEXTI(void);

uint32_t IR_GetRawData(void);
void IR_ClearResult(void);

#endif /* __IR_NEC_H */