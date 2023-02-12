#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "led_matrix.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"

/** DO NOT MODIFY */
#define COLOR_DEPTH 10
static const uint16_t gamma_table[256] = {
/**0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F*/
   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   2,   2,  /** 00 */                                   
   2,   3,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   9,   9,  10,  /** 10 */  
  11,  11,  12,  13,  14,  15,  15,  16,  17,  18,  19,  20,  21,  22,  23,  25,  /** 20 */   
  26,  27,  28,  29,  31,  32,  33,  35,  36,  38,  39,  41,  42,  44,  45,  47,  /** 30 */   
  49,  50,  52,  54,  55,  57,  59,  61,  63,  65,  67,  69,  71,  73,  75,  77,  /** 40 */   
  79,  81,  84,  86,  88,  91,  93,  95,  98, 100, 103, 105, 108, 110, 113, 116,  /** 50 */   
 118, 121, 124, 127, 129, 132, 135, 138, 141, 144, 147, 150, 153, 156, 160, 163,  /** 60 */   
 166, 169, 173, 176, 179, 183, 186, 190, 193, 197, 201, 204, 208, 212, 215, 219,  /** 70 */   
 223, 227, 231, 235, 238, 242, 246, 251, 255, 259, 263, 267, 271, 276, 280, 284,  /** 80 */   
 289, 293, 298, 302, 307, 311, 316, 321, 325, 330, 335, 340, 344, 349, 354, 359,  /** 90 */   
 364, 369, 374, 379, 384, 390, 395, 400, 405, 411, 416, 421, 427, 432, 438, 443,  /** A0 */   
 449, 455, 460, 466, 472, 478, 483, 489, 495, 501, 507, 513, 519, 525, 531, 538,  /** B0 */   
 544, 550, 556, 563, 569, 575, 582, 588, 595, 601, 608, 615, 621, 628, 635, 642,  /** C0 */   
 649, 655, 662, 669, 676, 683, 690, 697, 705, 712, 719, 726, 734, 741, 748, 756,  /** D0 */   
 763, 771, 778, 786, 794, 801, 809, 817, 825, 832, 840, 848, 856, 864, 872, 880,  /** E0 */   
 888, 897, 905, 913, 921, 930, 938, 946, 955, 963, 972, 981, 989, 998,1006,1015   /** F0 */  
};

/** pixles are not ordered */
static const uint8_t matrix_bit_map[32] = {
    24,25,26,27,28,29,30,31,
    23,22,21,20,19,18,17,16,
     8, 9,10,11,12,13,14,15,
     7, 6, 5, 4, 3, 2, 1, 0
};




typedef struct
{
    led_matrix_config_t config;
    bool pin_inited;
    led_matrix_frame_buffer_t frame_buffer[LED_MATRIX_FRAME_BUFFER_NUM];
    int active_fb_index;
    led_matrix_frame_buffer_t* active_fb;
    int scan_lines;
    bool core1_started;
    struct
    {
        uint32_t CLK;
        uint32_t NOE;
        uint32_t LATCH;
        uint32_t color;
        uint32_t addr;
    } pin_masks;
    struct
    {
        uint32_t high;
        uint32_t low;
    } color_data[COLOR_DEPTH];
    uint32_t* addr_data;
} led_matrix_t;

static void core1_loop_start();
static void send_stop_message();
static void wait_stop_message();

static led_matrix_t* this = NULL;

int led_matrix_init(led_matrix_config_t* config)
{
    if(this)    /** reinit */
        return -1;

    /** create led matrix */
    this = malloc(sizeof(led_matrix_t));
    if(!this)
        goto error;
    memset(this,0,sizeof(led_matrix_t));
    
    /** copy over config */
    memcpy(&this->config,config,sizeof(led_matrix_config_t));
    this->scan_lines = this->config.height/2;   /** 2 lines at a time */

    /** init addr data */
    this->addr_data = malloc(sizeof(uint32_t)*this->scan_lines);   
    if(!this->addr_data)
        goto error;
    memset(this->addr_data,0,sizeof(uint32_t)*this->scan_lines);

    /** init frame buffer */
    for(int i=0;i<LED_MATRIX_FRAME_BUFFER_NUM;i++)
    {
        led_matrix_frame_buffer_t* fb = &this->frame_buffer[i];
        fb->height = config->height;
        fb->width = config->width/32;
        fb->data = malloc(sizeof(uint32_t)*fb->height*fb->width);
        if(!fb->data)
            goto error;
        memset(fb->data,0,sizeof(uint32_t)*fb->height*fb->width);
    }
    this->active_fb_index = LED_MATRIX_FRAME_BUFFER_A;
    this->active_fb = &this->frame_buffer[this->active_fb_index];

    /** init pins */

#define util_gpio_init_output(pin,value) \
    gpio_init(pin);\
    gpio_set_slew_rate(pin,GPIO_SLEW_RATE_FAST);\
    gpio_set_dir(pin,true);\
    gpio_put(pin,value)

    /** CLK defaults to 0 */
    util_gpio_init_output(this->config.pins.CLK,false);
    /** LATCH defaults to 0 */
    util_gpio_init_output(this->config.pins.LATCH,false);
    /** NOE defaults to 1 */
    util_gpio_init_output(this->config.pins.NOE,true);
    /** set address lines to output, default 0 */
    int active_address_lines = (int)(ceilf(log2f(this->config.height/2))+0.5f); /** 2 scan lines */
    for(int i=0;i<active_address_lines;i++)
    {
        util_gpio_init_output(this->config.pins.addr[i],false);
    }
    /** set color pins to 0 */
    util_gpio_init_output(this->config.pins.R1,false);
    util_gpio_init_output(this->config.pins.R2,false);
    util_gpio_init_output(this->config.pins.G1,false);
    util_gpio_init_output(this->config.pins.G2,false);
    util_gpio_init_output(this->config.pins.B1,false);
    util_gpio_init_output(this->config.pins.B2,false);
#undef util_gpio_init_output
    this->pin_inited = true;
    /** init CLK LATCH NOE mask */
    this->pin_masks.CLK = 1u<<this->config.pins.CLK;
    this->pin_masks.LATCH = 1u<<this->config.pins.LATCH;
    this->pin_masks.NOE = 1u<<this->config.pins.NOE;
    /** init color mask */
    this->pin_masks.color |= 1u<<this->config.pins.R1;
    this->pin_masks.color |= 1u<<this->config.pins.G1;
    this->pin_masks.color |= 1u<<this->config.pins.B1;
    this->pin_masks.color |= 1u<<this->config.pins.R2;
    this->pin_masks.color |= 1u<<this->config.pins.G2;
    this->pin_masks.color |= 1u<<this->config.pins.B2;
    /** init addr mask & addr data */
    for(int i=0;i<active_address_lines;i++)
        this->pin_masks.addr |= 1u<<this->config.pins.addr[i];
    for(int i=0;i<this->config.height/2;i++)
    {
        for(int j=0;j<active_address_lines;j++)
        {
            if((1u<<j)&i)
                this->addr_data[i] |= 1u<<this->config.pins.addr[j];
        }
    }

    /** start core 1 task */
    core1_loop_start();

    return 0;
error:
    led_matrix_deinit();
    return -1;
}

void led_matrix_deinit()
{
    if(this)
    {
        if(this->core1_started)
        {
            /** stop task on core 1 */
            send_stop_message();
            wait_stop_message();
            multicore_reset_core1();
        }
        if(this->pin_inited)
        {
            gpio_deinit(this->config.pins.CLK);
            gpio_deinit(this->config.pins.LATCH);
            gpio_deinit(this->config.pins.NOE);
            gpio_deinit(this->config.pins.R1);
            gpio_deinit(this->config.pins.R2);
            gpio_deinit(this->config.pins.G1);
            gpio_deinit(this->config.pins.G2);
            gpio_deinit(this->config.pins.B1);
            gpio_deinit(this->config.pins.B2);
            int active_address_lines = (int)ceilf(log2f(this->config.height/2));
            for(int i=0;i<active_address_lines;i++)
                gpio_deinit(this->config.pins.addr[i]);
        }
        if(this->addr_data)
            free(this->addr_data);
        for(int i=0;i<LED_MATRIX_FRAME_BUFFER_NUM;i++)
        {
            if(this->frame_buffer[i].data)
                free(this->frame_buffer[i].data);
        }
        free(this);
        this = NULL;
    }
}

/**
 * @brief update color from the active fb 
 */
static void led_matrix_update_color_data()
{
    uint32_t color = this->active_fb->color;
    uint32_t red = gamma_table[(color >> 16) & 0xFF];
    uint32_t green = gamma_table[(color >> 8) & 0xFF];
    uint32_t blue = gamma_table[color & 0xFF];
    for(int i=0;i<COLOR_DEPTH;i++)
    {
        this->color_data[i].high = 0;
        this->color_data[i].low = 0;
        /** starts from the LSB */
        uint32_t color_bit_mask = 1u << i;
        if(red & color_bit_mask)
        {
            this->color_data[i].high |= 1u << this->config.pins.R1;
            this->color_data[i].low |= 1u << this->config.pins.R2;
        }
        if(green & color_bit_mask)
        {
            this->color_data[i].high |= 1u << this->config.pins.G1;
            this->color_data[i].low |= 1u << this->config.pins.G2;
        }
        if(blue & color_bit_mask)
        {
            this->color_data[i].high |= 1u << this->config.pins.B1;
            this->color_data[i].low |= 1u << this->config.pins.B2;
        }
    }
}

#define sleep_pin() __asm volatile ("nop\n\t")
/** ~50ns */
#define sleep_color_lsb0() __asm volatile (\
"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t")
/** ~100ns */
#define sleep_color_lsb1() __asm volatile (\
"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"\
"nop\n\tnop\n\tnop")
/** ~200ns */
#define sleep_color_lsb2() __asm volatile (\
"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"\
"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"\
"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t")

static void __not_in_flash_func(led_matrix_display)()
{
    /** clear shift registers */
    gpio_clr_mask(this->pin_masks.color);
    sleep_pin();
    for(int x=0;x<this->config.width;x++)
    {
        gpio_set_mask(this->pin_masks.CLK);
        sleep_pin();
        gpio_clr_mask(this->pin_masks.CLK);
    }
    gpio_set_mask(this->pin_masks.LATCH);
    sleep_pin();
    gpio_clr_mask(this->pin_masks.LATCH);
    /** scan */
    for(int c=0;c<COLOR_DEPTH;c++)
    {
        int fbh_i = 0;    /** frame buffer index high half */
        int fbl_i = this->active_fb->width*this->scan_lines;    /** low half */
        for(int y=0;y<this->config.height/2;y++)
        {
            /** line scan starts */
            int x = 0;
            int fb_offset = 0;
            uint32_t fb_mask = 1u<<matrix_bit_map[0];
            for(;;)
            {
                /** set color */
                uint32_t color_data = 0;
                if(this->active_fb->data[fbh_i+fb_offset] & fb_mask)
                    color_data |= this->color_data[c].high;
                if(this->active_fb->data[fbl_i+fb_offset] & fb_mask)
                    color_data |= this->color_data[c].low;
                /** put here to use calculation time as signal stable time */
                gpio_clr_mask(this->pin_masks.CLK);
                gpio_put_masked(this->pin_masks.color,color_data);
                /** calculate for next pixel and wait for this one to be stable */
                x++;
                if(x==this->config.width)
                {
                    /** shift the last bit in */
                    sleep_pin();
                    gpio_set_mask(this->pin_masks.CLK);
                    sleep_pin();
                    gpio_clr_mask(this->pin_masks.CLK);
                    break;
                }
                fb_offset = x>>5;
                fb_mask = 1u<<matrix_bit_map[x&0x1F];
                /** shift bit in */
                gpio_set_mask(this->pin_masks.CLK);
            }

            /** line latch starts */
            /** change row address */
            gpio_put_masked(this->pin_masks.addr,this->addr_data[y]);
            /** latch data */
            gpio_set_mask(this->pin_masks.LATCH);
            /** increment frame buffer index, also wait for latch stable */
            fbh_i+=this->active_fb->width;
            fbl_i+=this->active_fb->width;
            gpio_clr_mask(this->pin_masks.LATCH);

            /** enable output */
            gpio_clr_mask(this->pin_masks.NOE);
            switch (c)
            {
            case 0:{
                sleep_color_lsb0(); /** 50ns */
            } break;
            case 1:{
                sleep_color_lsb1(); /** 100ns */
            } break;
            case 2:{
                sleep_color_lsb2(); /** 200ns */
            } break;
            case 3:{
                sleep_color_lsb2(); /** 400ns */
                sleep_color_lsb2();
            } break;
            case 4:{
                sleep_color_lsb2(); /** 800ns */
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
            } break;
            case 5:{
                sleep_color_lsb2(); /** 1600ns */
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
                sleep_color_lsb2();
            } break;
            case 6:{
                for(int i=0;i<2;i++)  /** 3200ns */
                {
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                }
            } break;
            case 7:{
                for(int i=0;i<4;i++)    /** 6400ns */
                {
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                }
            } break;
            case 8:{
                for(int i=0;i<8;i++)    /** 12800ns */
                {
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                }
            } break;
            case 9:{
                for(int i=0;i<16;i++)   /** 25600ns */
                {
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                    sleep_color_lsb2();
                }
            } break;
            default:
                break;
            }
            gpio_set_mask(this->pin_masks.NOE);
        }
    }
}

/** core 1 task & inter core communication */

#define LED_MATRIX_CMD_OP_MASK 0xF0000000u
#define LED_MATRIX_CMD_DATA_MASK 0x0FFFFFFFu
#define LED_MATRIX_CMD_FB 0x00000000u
#define LED_MATRIX_CMD_STOP 0x10000000u

void led_matrix_draw_free_fb()
{
    sio_hw->fifo_wr = LED_MATRIX_CMD_FB;
}

bool led_matrix_has_free_fb()
{
    return (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)!=0;
}

led_matrix_frame_buffer_t* led_matrix_get_free_fb()
{
    if(!led_matrix_has_free_fb())
        return NULL;
    uint32_t core1_data = sio_hw->fifo_rd;
    int free_fb_index = core1_data & LED_MATRIX_CMD_DATA_MASK;
    return &this->frame_buffer[free_fb_index];
}

static void __not_in_flash_func(core1_loop_forever)()
{
    for(;;)
    {
        /** check in box */
        if(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
        {
            uint32_t core0_data = sio_hw->fifo_rd;
            uint32_t cmd = core0_data & LED_MATRIX_CMD_OP_MASK;
            if(cmd == LED_MATRIX_CMD_FB)
            {
                /** switch fb */
                int last_free_fb_index = this->active_fb_index==LED_MATRIX_FRAME_BUFFER_A?LED_MATRIX_FRAME_BUFFER_B:LED_MATRIX_FRAME_BUFFER_A;
                int next_free_fb_index = this->active_fb_index;
                this->active_fb_index = last_free_fb_index;
                this->active_fb = &this->frame_buffer[this->active_fb_index];
                /** update color data */
                led_matrix_update_color_data();
                /** send out free fb */
                sio_hw->fifo_wr = LED_MATRIX_CMD_FB | next_free_fb_index;
            }
            else if(cmd == LED_MATRIX_CMD_STOP)
            {
                send_stop_message();
                break;
            }
        }
        /** display frame */
        led_matrix_display();
    }
}

static void core1_loop_start()
{
    /** start task on core 1 */
    multicore_launch_core1(core1_loop_forever);
    /** send initial draw call */
    led_matrix_draw_free_fb();
    this->core1_started = true;
}

static void send_stop_message()
{
    sio_hw->fifo_wr = LED_MATRIX_CMD_STOP;
}

static void wait_stop_message()
{
    for(;;)
    {
        if(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
        {
            uint32_t core1_data = sio_hw->fifo_rd;
            if((core1_data & LED_MATRIX_CMD_OP_MASK) == LED_MATRIX_CMD_STOP)
                break;
        }
    }
}


