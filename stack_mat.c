#include <stdlib.h>
#include <string.h>
#include "stack_mat.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define STACK_MAT_UART_BAUD 1200
#define STACK_MAT_MAX_DATA_INTERVAL_US 30000

typedef struct
{
    char status;
    uint8_t digits[6];
    uint8_t sum;
    uint8_t tail[2];
} __attribute__((packed)) stack_mat_packet_t;

typedef struct
{
    tev_irq_injector_handle_t injector;
    uint32_t rx;
    uart_inst_t* uart;
    tev_irq_handle_t irq_handle;
    bool uart_inited;
    uint8_t rx_buffer[sizeof(stack_mat_packet_t)];
    int rx_len;
    uint64_t last_byte_time;
    stack_mat_packet_t rx_data;
    stack_mat_on_data_t callback;
    void* callback_ctx;
} stack_mat_t;

static void stack_mat_irq_handler();
static void stack_mat_tev_irq_handler(void* ctx);

static stack_mat_t* this = NULL;

int stack_mat_init(
    uint32_t rx, uart_inst_t* uart,
    tev_irq_injector_handle_t injector,
    stack_mat_on_data_t on_data, void* ctx)
{
    if(this)
        return -1;
    this = malloc(sizeof(stack_mat_t));
    if(!this)
        goto error;
    memset(this,0,sizeof(stack_mat_t));
    this->injector = injector;
    this->rx = rx;
    this->uart = uart;
    this->irq_handle = NULL;
    this->callback = on_data;
    this->callback_ctx = ctx;
    /** @todo test */    
    this->irq_handle = tev_irq_injector_set_irq_handler(this->injector,stack_mat_tev_irq_handler,NULL);
    if(!this->irq_handle)
        goto error;
    uart_init(this->uart,STACK_MAT_UART_BAUD);
    uart_set_hw_flow(this->uart,false,false);
    uart_set_format(this->uart,8,1,UART_PARITY_NONE);
    uart_set_fifo_enabled(this->uart,false);
    /** only uses rx */
    gpio_set_function(this->rx, GPIO_FUNC_UART);
    int UART_IRQ = this->uart==uart0?UART0_IRQ:UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ,stack_mat_irq_handler);
    irq_set_enabled(UART_IRQ,true);
    uart_set_irq_enables(this->uart,true,false);
    this->uart_inited = true;

    return 0;
error:
    stack_mat_deinit();
    return -1;
}

void stack_mat_deinit()
{
    if(this)
    {
        if(this->uart_inited)
        {
            int UART_IRQ = this->uart==uart0?UART0_IRQ:UART1_IRQ;
            irq_set_enabled(UART_IRQ,false);
            uart_deinit(this->uart);
        }
        if(this->irq_handle)
            tev_irq_injector_clear_irq_handler(this->injector,this->irq_handle);
        free(this);
        this = NULL;
    }
}

static void stack_mat_irq_handler()
{
    if(!this)
        return;
    /** filter out mis-aligned data */
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    if((now_us - this->last_byte_time) > STACK_MAT_MAX_DATA_INTERVAL_US)
        this->rx_len = 0;
    this->last_byte_time = now_us;
    /** read out data */
    while(uart_is_readable(this->uart))
    {
        uint8_t ch = uart_getc(this->uart);
        this->rx_buffer[this->rx_len++] = ch;
        if(this->rx_len == sizeof(this->rx_buffer))
        {
            this->rx_len = 0;
            memcpy(&this->rx_data,this->rx_buffer,sizeof(this->rx_buffer));
            tev_irq_injector_inject(this->injector,this->irq_handle);
        }
    }
}

static void stack_mat_tev_irq_handler(void* ctx)
{
    if(!this)
        return;
    /** try fix data */
    stack_mat_data_t data = {0};
    data.status = this->rx_data.status;
    int sum = 64;
    for(int i=0;i<sizeof(this->rx_data.digits);i++)
    {
        data.digits[i] = this->rx_data.digits[i] & 0x0F;
        sum += data.digits[i];
    }
    /** check sum */
    if(sum == this->rx_data.sum)
    {
        if(this->callback)
            this->callback(&data, this->callback_ctx);
    }
}
