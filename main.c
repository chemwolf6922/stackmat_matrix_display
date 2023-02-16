#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "led_matrix.h"
#include "font.h"
#include "stack_mat.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tev.h"
#include "tev_irq_injector.h"

#define PERIODIC_TASK_INTERVAL 33

static void tev_task(void* ctx);
static void on_stack_mat_data(const stack_mat_data_t* data, void* ctx);

static tev_handle_t tev = NULL;
static tev_irq_injector_handle_t injector = NULL;
static tev_timeout_handle_t periodic = NULL;

static int x_offset_digit[6] = {0};
static int x_offset_colon = 0;
static int x_offset_period = 0;
static int y_offset = 0;


int main()
{

    // stdio_init_all();

    led_matrix_init(&(led_matrix_config_t){
        .height = 32,
        .width = 128,
        .pins = {
            .R1 = 0,
            .G1 = 1,
            .B1 = 2,
            .R2 = 3,
            .G2 = 4,
            .B2 = 5,
            .addr = {6,7,8,9},
            .CLK = 10,
            .LATCH = 11,
            .NOE = 12
        }
    });

    /** calculate display offsets */
    int total_width = 6*matrix_font_nums[0].witdh + matrix_font_colon.witdh + matrix_font_period.witdh;
    y_offset = (32-FONT_HEIGHT)/2;
    x_offset_digit[0] = (128 - total_width)/2;
    x_offset_colon = x_offset_digit[0]+matrix_font_nums[0].witdh;
    x_offset_digit[1] = x_offset_colon + matrix_font_colon.witdh;
    x_offset_digit[2] = x_offset_digit[1] + matrix_font_nums[0].witdh;
    x_offset_period = x_offset_digit[2] + matrix_font_nums[0].witdh;
    x_offset_digit[3] = x_offset_period + matrix_font_period.witdh;
    x_offset_digit[4] = x_offset_digit[3] + matrix_font_nums[0].witdh;
    x_offset_digit[5] = x_offset_digit[4] + matrix_font_nums[0].witdh;

    /** start FreeRTOS */
    xTaskCreate(tev_task,"tev",2048,NULL,1,NULL);

    /** FreeRTOS main loop */
    vTaskStartScheduler();

    led_matrix_deinit();

    for(;;)
        tight_loop_contents();

    return 0;
}

static void tev_task(void* ctx)
{
    tev = tev_create_ctx();
    injector = tev_irq_injector_new(tev,10);

    /** draw the default output */
    on_stack_mat_data(&(stack_mat_data_t){.digits={0,0,0,0,0,0},.status=STACK_MAT_STATUS_IDLE},NULL);

    /** init stacks mat connection */
    stack_mat_init(17,uart0,injector,on_stack_mat_data,NULL);

    /** tev main loop */
    tev_main_loop(tev);

    stack_mat_deinit();
    tev_irq_injector_free(injector);
    injector = NULL;
    tev_free_ctx(tev);
    tev = NULL;
}

static void on_stack_mat_data(const stack_mat_data_t* data, void* ctx)
{
    /** check data */
    for(int i=0;i<6;i++)
    {
        if(data->digits[i] < 0 || data->digits[i] > 9)
            return;
    }
    /** udpate display */
    led_matrix_frame_buffer_t* fb = led_matrix_get_free_fb();
    if(fb)
    {
        led_matrix_frame_buffer_clear(fb);
        const matrix_font_t* c = NULL;
        for(int i=5;i>=0;i--)
        {
            c = &matrix_font_nums[data->digits[i]];
            led_matrix_frame_buffer_draw(fb,x_offset_digit[i],y_offset,c->witdh,FONT_HEIGHT,c->data);
        }
        c = &matrix_font_colon;
        led_matrix_frame_buffer_draw(fb,x_offset_colon,y_offset,c->witdh,FONT_HEIGHT,c->data);
        c = &matrix_font_period;
        led_matrix_frame_buffer_draw(fb,x_offset_period,y_offset,c->witdh,FONT_HEIGHT,c->data);
        if(data->status == STACK_MAT_STATUS_BOTH)
            fb->color = 0xFF0000;
        else if(data->status == STACK_MAT_STATUS_READY)
            fb->color = 0x00FF00;
        else
            fb->color = 0xFFFFFF;
        led_matrix_draw_free_fb();
    }
}


