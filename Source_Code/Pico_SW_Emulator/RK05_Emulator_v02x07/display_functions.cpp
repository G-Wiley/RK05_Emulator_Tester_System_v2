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
#include "emulator_hardware.h"
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
    int display_message_timer;
    int display_invert_timer;
    bool display_inverted;
} edisplay;

uint8_t buf[2];
uint8_t resultbuf;

ssd1306_t disp;
uint8_t display_buffer[(DISPLAY_WIDTH * DISPLAY_HEIGHT / 8) + 1];

uint8_t* big_digits[] = {digit_0m, digit_1m, digit_2m, digit_3m, digit_4m, digit_5m, digit_6m, digit_7m};
uint8_t* big_digit_pairs[] = {digits_01m, digits_23m, digits_45m, digits_67m};

void print_display_state(){ // for debugging only
    printf("Display_State.display_message_timer = %d, Display_State.display_invert_timer = %d\r\n", edisplay.display_message_timer, edisplay.display_invert_timer);
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

void display_error(char* row1, char* row2)
{
    ssd1306_clear(&disp);
    ssd1306_invert(&disp, 0);
    edisplay.display_inverted = false;
    ssd1306_draw_string(&disp, 0,  2, 2, (char*) "ERROR:");
    ssd1306_draw_string(&disp, 0, 22, 2, row1);
    ssd1306_draw_string(&disp, 0, 42, 2, row2);
    // Send buffer to the display
    ssd1306_show(&disp);

    printf("  ##DISPLAY ERROR: \"%s\", \"%s\"\r\n", row1, row2);
    edisplay.display_message_timer = ERROR_DISPLAY_TIME;
}

void display_status(char* row1, char* row2)
{
    ssd1306_clear(&disp);
    ssd1306_invert(&disp, 0);
    edisplay.display_inverted = false;
    ssd1306_draw_string(&disp, 5,  2, 2, (char*) "Status:");
    ssd1306_draw_string(&disp, 5, 22, 2, row1);
    ssd1306_draw_string(&disp, 5, 42, 2, row2);
    // Send buffer to the display
    ssd1306_show(&disp);

    printf("  DISPLAY STATUS: \"%s\", \"%s\"\r\n", row1, row2);
    edisplay.display_message_timer = STATUS_DISPLAY_TIME;
}

void display_splash_screen()
{
    ssd1306_clear(&disp);
    ssd1306_invert(&disp, 0);
    edisplay.display_inverted = false;
        //ssd1306_draw_string_with_font(&disp, 8, 24, 2, fonts[font_i], buf);
        //ssd1306_draw_string_with_font(&disp, 8, 24, 1, fonts[3], buf);
    ssd1306_draw_string(&disp, 20,  5, 3, (char*) "RK05");
    ssd1306_draw_string(&disp, 10, 35, 2, (char*) "emulator");
    ssd1306_show(&disp);
    sleep_ms(1000);

    edisplay.display_message_timer = STATUS_DISPLAY_TIME;
}

void display_invert()
{
    // invert the image presently in the display
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

void display_disable_message_timer()
{
    edisplay.display_message_timer = 0;
}

extern const uint8_t font_8x5[];

void display_drive_address(int drv_addr, bool fixed, char *image_name)
{
#if 1
    uint8_t *digit_pointer;

    if(fixed){
        // Fixed disk addresses
        digit_pointer = big_digit_pairs[(drv_addr >> 1) & 0x3];
    }
    else{
        //non-fixed (normal) disk addresses
        digit_pointer = big_digits[drv_addr & 0x7];
    }

    int x_coord, y_coord, bit_coord;
    uint8_t imagebyte;
    ssd1306_clear(&disp);
    //ssd1306_bmp_show_image(&disp, image_data, image_size);
    //ssd1306_bmp_show_image(&disp, digit_0m, image_size);
    for(y_coord = DRIVE_CHAR_YOFFSET; y_coord < (DRIVE_CHAR_YOFFSET + DRIVE_CHAR_HEIGHT); y_coord++){
        for(x_coord = (DRIVE_CHAR_XOFFSET / 8); x_coord < ((DRIVE_CHAR_XOFFSET + DRIVE_CHAR_WIDTH) / 8); x_coord++){
            imagebyte = *digit_pointer++;
            for(bit_coord = 0; bit_coord < 8; bit_coord++){
                if((imagebyte & 0x80) != 0)
                    ssd1306_draw_pixel(&disp, (x_coord * 8) + bit_coord, y_coord);
                imagebyte = imagebyte << 1;
            }
        }
    }
    // image name
    x_coord = DISPLAY_WIDTH / 2 - (strlen(image_name) * ssd1306_get_font_width(2)) / 2;
    ssd1306_draw_string(&disp, x_coord, 47, 2, image_name);
    ssd1306_show(&disp);
#else
    int x_coord;
    static char dbuf[10];
    static const char *fx_str[] = {
        "0/1",
        "2/3",
        "4/5",
        "6/7"
    };

    if (fixed) {
        x_coord = 32;
        strcpy(dbuf, fx_str[(drv_addr >> 1) & 3]);
    } else {
        x_coord = 52;
        snprintf(dbuf, sizeof(dbuf), "%d", drv_addr & 7);
    }
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, x_coord,  5, 4, dbuf);

    // Centre image name
    x_coord = DISPLAY_WIDTH / 2 - (strlen(image_name) * ssd1306_get_font_width(2)) / 2;
    ssd1306_draw_string(&disp, x_coord, 40, 2, image_name);
    ssd1306_show(&disp);
#endif
}

void manage_display_timers(Disk_State* ddstate)
{
    if(edisplay.display_message_timer > 0){
        edisplay.display_invert_timer = DISPLAY_INVERT_TIME;
        edisplay.display_message_timer--;
        if(edisplay.display_message_timer == 0){
            display_drive_address(ddstate->Drive_Address, ddstate->mode_RK05f, ddstate->File_Ready ? ddstate->imageName : (char *)"");
            edisplay.display_invert_timer = DISPLAY_INVERT_TIME;
        }
    }
    else{
        edisplay.display_invert_timer--;
        if(edisplay.display_invert_timer == 0){
            display_invert();
            edisplay.display_invert_timer = DISPLAY_INVERT_TIME;
        }
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

