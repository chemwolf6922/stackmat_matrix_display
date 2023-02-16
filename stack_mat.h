#ifndef __STACK_MAT_H
#define __STACK_MAT_H

#include "stdint.h"
#include "hardware/uart.h"
#include "tev_irq_injector.h"

#define STACK_MAT_STATUS_IDLE 'I'
#define STACK_MAT_STATUS_LEFT 'L'
#define STACK_MAT_STATUS_RIGHT 'R'
#define STACK_MAT_STATUS_BOTH 'C'
#define STACK_MAT_STATUS_READY 'A'
#define STACK_MAT_STATUS_RUNNING ' '
#define STACK_MAT_STATUS_STOP 'S'

typedef struct
{
    char status;
    uint8_t digits[6]; 
} stack_mat_data_t;

typedef void(*stack_mat_on_data_t)(const stack_mat_data_t* data, void* ctx);

int stack_mat_init(
    uint32_t rx, uart_inst_t* uart,
    tev_irq_injector_handle_t injector,
    stack_mat_on_data_t on_data, void* ctx);
void stack_mat_deinit();

#endif
