#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "led_matrix.h"
#include "font.h"

int main()
{

    stdio_init_all();

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
    int y_offset = (32-FONT_HEIGHT)/2;
    int x_offset_digit[6] = {0};
    x_offset_digit[0] = (128 - total_width)/2;
    int x_offset_colon = x_offset_digit[0]+matrix_font_nums[0].witdh;
    x_offset_digit[1] = x_offset_colon + matrix_font_colon.witdh;
    x_offset_digit[2] = x_offset_digit[1] + matrix_font_nums[0].witdh;
    int x_offset_period = x_offset_digit[2] + matrix_font_nums[0].witdh;
    x_offset_digit[3] = x_offset_period + matrix_font_period.witdh;
    x_offset_digit[4] = x_offset_digit[3] + matrix_font_nums[0].witdh;
    x_offset_digit[5] = x_offset_digit[4] + matrix_font_nums[0].witdh;

    int counter = 0;
    for(;;)
    {
        led_matrix_frame_buffer_t* fb = led_matrix_get_free_fb();
        if(fb)
        {
            led_matrix_frame_buffer_clear(fb);
            const matrix_font_t* c = NULL;
            int remain = counter;
            for(int i=5;i>=0;i--)
            {
                int n = remain % 10;
                remain = remain / 10;
                c = &matrix_font_nums[n];
                led_matrix_frame_buffer_draw(fb,x_offset_digit[i],y_offset,c->witdh,FONT_HEIGHT,c->data);
            }
            c = &matrix_font_colon;
            led_matrix_frame_buffer_draw(fb,x_offset_colon,y_offset,c->witdh,FONT_HEIGHT,c->data);
            c = &matrix_font_period;
            led_matrix_frame_buffer_draw(fb,x_offset_period,y_offset,c->witdh,FONT_HEIGHT,c->data);
            fb->color = 0xFFFFFF;
            led_matrix_draw_free_fb();
        }
        counter = (counter + 1) % 1000000;
        sleep_ms(30);
    }

    led_matrix_deinit();

    return 0;
}