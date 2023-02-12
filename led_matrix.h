#ifndef __LED_MATRIX_H
#define __LED_MATRIX_H

#include <stdint.h>
#include <stdbool.h>

typedef void* led_matrix_handle_t;

enum
{
    LED_MATRIX_FRAME_BUFFER_A,
    LED_MATRIX_FRAME_BUFFER_B,
    LED_MATRIX_FRAME_BUFFER_NUM
};

typedef struct
{
    uint32_t* data;
    int height;
    int width;
    uint32_t color;
} led_matrix_frame_buffer_t;

typedef struct
{
    int width;
    int height;
    struct
    {
        uint32_t CLK;
        uint32_t NOE;
        uint32_t LATCH;
        uint32_t R1;
        uint32_t R2;
        uint32_t G1;
        uint32_t G2;
        uint32_t B1;
        uint32_t B2;
        uint32_t addr[5];
    } pins;
} led_matrix_config_t;

int led_matrix_init(led_matrix_config_t* config);
void led_matrix_deinit();

/**
 * @brief Check if the next free fb is ready.
 */
bool led_matrix_has_free_fb();

/**
 * @brief Return NULL if the next free fb is not ready yet.
 */
led_matrix_frame_buffer_t* led_matrix_get_free_fb();

/**
 * @brief Overlay bitmap onto frame buffer
 * 
 * @param fb
 * @param x x offset, starts from left
 * @param y y offset, starts from right
 * @param width bitmap width, in bits, <= 32
 * @param height bitmap height, int bits, <= 32
 * @param bitmap bitmap data, uint32_t array with length = height
 */
void led_matrix_frame_buffer_draw(led_matrix_frame_buffer_t* fb, int x, int y, int width, int height, const uint32_t* bitmap);
void led_matrix_frame_buffer_clear(led_matrix_frame_buffer_t* fb);

/**
 * @brief After this call, the user should call get_free_fb again for the next frame.
 */
void led_matrix_draw_free_fb();


#endif

