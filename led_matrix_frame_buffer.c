#include <string.h>
#include "led_matrix.h"

#define MIN(a,b) ((a<b)?a:b)

void led_matrix_frame_buffer_draw(led_matrix_frame_buffer_t* fb, int x, int y, int width, int height, const uint32_t* bitmap)
{
    /** check params */
    if(x>=fb->width*32 || y>=fb->height)
        return;
    int x_max = MIN(x+width, fb->width*32);
    int y_max = MIN(y+height, fb->height);
    bool has_right_half = x_max/32 != x/32;
    /** left half */
    int left_word_offset = x/32;
    int left_bit_offset = x%32;
    int left_bit_length = MIN(width,32-left_bit_offset);
    uint32_t left_bit_mask = (((0xFFFFFFFFu << left_bit_offset) >> left_bit_offset) >> (32-left_bit_length-left_bit_offset)) << (32-left_bit_length-left_bit_offset);
    for(int i=y,p=0;i<y_max;i++,p++)
    {
        uint32_t data = ((bitmap[p] >> left_bit_offset) & left_bit_mask);
        fb->data[i*fb->width+left_word_offset] |= data;
    }
    /** right half */
    if(has_right_half)
    {
        int right_word_offset = x_max/32;
        /** right_bit_offset === 0 */
        int right_bit_length = x_max%32;
        uint32_t right_bit_mask = (0xFFFFFFFFu >> (32-right_bit_length)) << (32-right_bit_length);
        for(int i=y,p=0;i<y_max;i++,p++)
        {
            uint32_t data = ((bitmap[p] << left_bit_length)) & right_bit_mask;
            fb->data[i*fb->width+right_word_offset] |= data;
        }
    }
}

void led_matrix_frame_buffer_clear(led_matrix_frame_buffer_t* fb)
{
    memset(fb->data,0,sizeof(uint32_t)*fb->height*fb->width);
    fb->color = 0;
}