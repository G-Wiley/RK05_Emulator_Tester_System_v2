// *********************************************************************************
// display_functions.cpp
//   functions for display operations
// *********************************************************************************
// 

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306a.h"

#include "disk_state_definitions.h"
#include "display_timers.h"
#include "display_big_images.h"
#include "tester_hardware.h"
//#include "display_state_definition.h"

#define FP_I2C1_SDA 26
#define FP_I2C1_SCL 27

#define PCA9557_ADDR 0x1c
#define DISPLAY_I2C_ADDR 0x3C

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define DRIVE_CHAR_WIDTH 80
#define DRIVE_CHAR_HEIGHT 40
#define DRIVE_CHAR_XOFFSET 24
#define DRIVE_CHAR_YOFFSET 2

//static Display_State edisplay;

struct Display_State
{public:
    int display_invert_timer;
    bool display_inverted;
} edisplay;

uint8_t buf[2];
uint8_t resultbuf;

ssd1306_t disp;
uint8_t display_buffer[(DISPLAY_WIDTH * DISPLAY_HEIGHT / 8) + 1];

void print_display_state(){ // for debugging only
    printf("Display_State.display_invert_timer = %d\r\n", edisplay.display_invert_timer);
}

void setup_i2c()
{
    // Init i2c0 controller
    i2c_init(i2c1, 1000000);
    // Set up pins for SCL and SDA
    gpio_set_function(FP_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(FP_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(FP_I2C1_SDA);
    gpio_pull_up(FP_I2C1_SCL);
}

void setup_display()
{
    disp.external_vcc=false;
    disp.i2c_i = i2c1;
    disp.bufsize = (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8) + 1;
    disp.buffer = &display_buffer[0];
    ssd1306_init(&disp, (uint8_t) DISPLAY_WIDTH, (uint8_t) DISPLAY_HEIGHT, (uint8_t) DISPLAY_I2C_ADDR);
    ssd1306_clear(&disp);
    int i;

    // If you don't do anything before initializing a display pi pico is too fast and starts sending
    // commands before the screen controller had time to set itself up, so we add an artificial delay for
    // ssd1306 to set itself up
    sleep_ms(250);

}

void display_invert()
{
    // invert the image presently in the display
    printf("display_invert()\r\n");
    if(edisplay.display_inverted){
        ssd1306_invert(&disp, 0);
        edisplay.display_inverted = false;
    }
    else{
        ssd1306_invert(&disp, 1);
        edisplay.display_inverted = true;
    }
}

void display_shutdown(){
    // turn off the display during Interface Test Mode to prevent burn-in
    //ssd1306_poweroff(&disp);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);
}

void display_restart_invert_timer()
{
    edisplay.display_invert_timer = DISPLAY_INVERT_TIME;
}

extern const uint8_t font_8x5[];

void display_rk05_tester()
{
    ssd1306_clear(&disp);
    ssd1306_invert(&disp, 0);
    edisplay.display_inverted = false;
    display_restart_invert_timer();
    ssd1306_draw_string(&disp, 29,  5, 3, (char*) "RK05");
    ssd1306_draw_string(&disp, 10, 35, 3, (char*) "Tester");
    ssd1306_show(&disp);
}

void manage_display_timers()
{
    printf("invert timer = %d\r\n", edisplay.display_invert_timer);
    edisplay.display_invert_timer--;
    if(edisplay.display_invert_timer <= 0){
        display_invert();
        edisplay.display_invert_timer = DISPLAY_INVERT_TIME;
    }
}

int read_pca9557()
{
uint8_t buf[2];
uint8_t resultbuf;
    // set the register pointer to the read data register, which is reg 0 by writing to reg 0
    buf[0] = 0;
    buf[1] = 0xff;
    i2c_write_blocking(i2c1, PCA9557_ADDR, buf, 2, false);
    // now read register 0
    i2c_read_blocking (i2c1, PCA9557_ADDR, &resultbuf, 1, false);
    return(resultbuf);
}

void initialize_pca9557()
{
uint8_t buf[2];
    setup_i2c();
    sleep_ms(50);

    // initialize the mode register as [7:4] are outputs and [3:0] are inputs
    buf[0] = 3;
    buf[1] = 0x0f;
    i2c_write_blocking(i2c1, PCA9557_ADDR, buf, 2, false);
    // initialize the invert data register to not invert data for any I/O signals
    buf[0] = 2;
    buf[1] = 0;
    i2c_write_blocking(i2c1, PCA9557_ADDR, buf, 2, false);
}

int read_drive_address_switches()
{
int i2c_read_value;

    //read the I2C register here to get the drive address bits and the fixed or not fixed bit
    i2c_read_value = read_pca9557();
    return((i2c_read_value & (DRIVE_ADDRESS_BITS_I2C | DRIVE_FIXED_MODE_BIT_I2C)));
}

