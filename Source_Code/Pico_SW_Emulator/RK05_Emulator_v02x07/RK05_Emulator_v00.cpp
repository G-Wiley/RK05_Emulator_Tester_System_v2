// *********************************************************************************
// RK05_Emulator_v0.cpp
//   Top Level main() function of the RK05 disk emulator
// *********************************************************************************
//
//===============================================================================================//
//                                                                                               //
// This software and related modules included by the top level module and files used to build    //
// the software are provided on an as-is basis. No warrantees or guarantees are provided or      //
// implied. Users of the RK05 Emulator or RK05 Tester shall not hold the developers of this      //
// software, firmware, hardware, or related documentation liable for any damages caused by       //
// any type of malfunction of the product including malfunctions caused by defects in the design //
// or operation of the software, firmware, hardware or use of related documentation or any       //
// combination thereof.                                                                          //
//                                                                                               //
//===============================================================================================//
//
#define SOFTWARE_MINOR_VERSION 7
#define SOFTWARE_VERSION 2

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
//#include "hardware/adc.h"
//#include "sd_card.h"
//#include "ff.h"

//#include "include_libs/stdlib.h"
//#include "include_libs/adc.h"
//#include "include_libs/sd_card.h"
//#include "include_libs/ff.h"

//#include <stdio.h>
#include <string.h>
//#include "pico/stdlib.h"
//#include "hardware/uart.h"
//#include "pico/binary_info.h"
//#include "hardware/spi.h"

//#include "emulator_hardware.h"
//#include "emulator_state.h"
//#include "display_big_images.h"
#include "disk_state_definitions.h"
#include "display_functions.h"
//#include "microsd_file_ops.h"

#include "emulator_state_definitions.h"
#include "emulator_state.h"
#include "emulator_hardware.h"
#include "display_functions.h"
#include "display_timers.h"
#include "emulator_command.h"

// GLOBAL VARIABLES
struct Disk_State edisk;

#define INPUT_LINE_LENGTH 200
char inputdata[INPUT_LINE_LENGTH];
char *extract_argv[INPUT_LINE_LENGTH];
int extract_argc;

// callback code
int char_from_callback;

int debug_mode;

// Function Prototype
//void update_drive_address(Disk_State* ddisk);


// console input callback code
void callback(void *ptr){
    int *i = (int*) ptr;  // cast void pointer back to int pointer
    // read the character which caused to callback (and in the future read the whole string)
    *i = getchar_timeout_us(100); // length of timeout does not affect results
}

void gpio_callback(uint gpio, uint32_t events) {
    int readval = read_int_inputs();
    int operation_id = (readval >> 10) & 0x3;
    if((gpio == 4) && ((events & 0x8) == 0x8)){
        switch(operation_id){
            case 0:
                if((readval & 0x400000) == 0)
                    printf("*SEEK RESTORE\r\n");
                else
                    printf("*SEEK %d\r\n", readval & 0xff);
                break;
            case 1:
                printf("*READ c=%d h=%d s=%d\r\n", readval & 0xff, (readval >> 8) & 1, (readval >> 12) & 0xf);
                break;
            case 2:
                printf("*WRITE c=%d h=%d s=%d\r\n", readval & 0xff, (readval >> 8) & 1, (readval >> 12) & 0xf);
                break;
            default:
                printf("*ERROR, operation_id=%d\r\n", operation_id);
                break;
        }
    }
}

void read_switches_and_set_drive_address(){
    int switch_read_value = read_drive_address_switches();
    edisk.Drive_Address = switch_read_value & DRIVE_ADDRESS_BITS_I2C;
    edisk.mode_RK05f = ((switch_read_value & DRIVE_FIXED_MODE_BIT_I2C) == 0) ? false : true;
    load_drive_address(edisk.Drive_Address);
}

void initialize_states(){
    edisk.Drive_Address = 0;
    edisk.mode_RK05f = false;
    edisk.File_Ready = false;
    edisk.Fault_Latch = false;
    edisk.dc_low = false;
    edisk.FPGA_version = 0;
    edisk.FPGA_minorversion = 0;
    edisk.Board_version = read_board_version();

    edisk.run_load_state = RLST0;
    edisk.rl_switch = false;
    edisk.p_wp_switch = edisk.wp_switch = false;

    edisk.door_is_open = true;
    edisk.door_count = 0;

    //display_disable_message_timer();
    //display_restart_invert_timer();

    // initialize parameters to PDP-8 RK8-E values
    strcpy(edisk.controller, "RK8-E");
    edisk.bitRate = 1440000;
    //edisk.preamble1Length = 120;
    //edisk.preamble2Length = 82;
    //edisk.dataLength = 3104;
    //edisk.postambleLength = 36;
    edisk.numberOfCylinders = 203;
    edisk.numberOfSectorsPerTrack = 16;
    edisk.numberOfHeads = 2;
    edisk.microsecondsPerSector = 2500;
}

#define UART_ID uart0
//#define BAUD_RATE 115200
#define BAUD_RATE 460800
#define UART_TX_PIN 0
#define UART_RX_PIN 1

void initialize_system() {
    debug_mode = 0;
    stdio_init_all();
    sleep_ms(50);
    //initialize_uart();
    // Set up our UART with the required speed.
    uart_init(UART_ID, BAUD_RATE);
    sleep_ms(500);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    // We are using GP0 and GP1 for the UART, package pins 1 & 2
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    sleep_ms(50);

    // callback code
    char_from_callback = 0;
    uint32_t old_irq_status = save_and_disable_interrupts();
    stdio_set_chars_available_callback(callback, (void*)  &char_from_callback); //register callback
    restore_interrupts(old_irq_status);

    //printf("************* UART is initialized *****************\r\n");

    // Wait for user to press 'enter' to continue
    //char buf[100];
    //printf("\r\nPress 'enter' to start.\r\n");
    //while (true) {
        //buf[0] = getchar();
        //if ((buf[0] == '\r') || (buf[0] == '\n')) {
            //break;
        //}
    //}

    printf("\r\n************* RK05 Emulator STARTUP *************\n");
    initialize_gpio();
    setup_display();
    printf(" *gpio initialized\n");
    assert_fpga_reset();
    sleep_ms(10);
    deassert_fpga_reset();
    initialize_spi();
    printf(" *spi initialized\r\n");
    initialize_states();
    printf(" *software internal states initialized\n");
    initialize_fpga(&edisk);
    printf(" *fpga registers initialized\n");

    printf(" *Emulator software version %d.%d\r\n", SOFTWARE_VERSION, SOFTWARE_MINOR_VERSION);
    printf(" *FPGA version %d.%d\r\n", edisk.FPGA_version, edisk.FPGA_minorversion);
    printf(" *Board version %d\r\n", edisk.Board_version);

    // At boot time: if the RUN/LOAD switch is in the LOAD position then open the door
    read_rocker_switches(&edisk);
    //if(edisk.rl_switch == 0)
      //boot_open_the_door();
    // else if the switch is in the RUN position then the RUN/LOAD state machine will "automatically" close the door and load the DRAM
    clear_dc_low();
}

int main() {
    uint32_t ticker;
    initialize_system();
    read_switches_and_set_drive_address();

    ticker = 0;
    int reg00_val;
    //stdio_init_all();
    // set the baud rate of stdio here
    printf("RK05 Emulator STARTING\n");
    display_splash_screen();
    if(is_it_a_tester()) // check the hardware ID bit in the FPGA to confirm that it's loaded with emulator FPGA code
        printf("  ######## ERROR, hardware is a tester, should be emulator hardware ########\r\n");

    if(is_testmode_selected()){
        sleep_ms(1000); // wait a second before turning off the display
        display_shutdown(); // turn off the display during Interface Test Mode
        close_drive_door(); // make the servo motor relax
	    sleep_ms(100);
        drive_door_status();
        enable_interface_test_mode(); // enable interface test mode in the FPGA
        while(true){
            emulator_command_mode(&edisk);
        }
    }
    else{
        while (true) {
            if((ticker % 50) == 0){
                reg00_val = read_reg00();
                printf("main loop %d, Drive_Address = %d, RLST%x, vsense = %d, reg00 = %x\r\n", ticker, edisk.Drive_Address, edisk.run_load_state, 
                edisk.debug_vsense, reg00_val);
            }
            //if((ticker % 10) == 0) // for debugging the display_state functions
                //print_display_state();
            read_rocker_switches(&edisk);
            // if the WTPROT button is pressed then toggle the WP status in the FPGA, also toggles the indicator
            if(edisk.wp_switch && !edisk.p_wp_switch){
                toggle_wp();
                printf("toggle WTPROT\r\n");
            }

            check_dc_low(&edisk);
            //clear_dc_low();

            process_run_load_state(&edisk);
            if((edisk.run_load_state == RLST0) || (edisk.run_load_state == RLST19) || (edisk.run_load_state == RLST1d)){
                int previous_drive_address = edisk.Drive_Address;
                bool previous_mode_RK05f = edisk.mode_RK05f;
                read_switches_and_set_drive_address();
                //int switch_read_value = read_drive_address_switches();
                //edisk.Drive_Address = switch_read_value & DRIVE_ADDRESS_BITS_I2C;
                //edisk.mode_RK05f = ((switch_read_value & DRIVE_FIXED_MODE_BIT_I2C) == 0) ? false : true;
                //load_drive_address(edisk.Drive_Address);
                if((previous_drive_address != edisk.Drive_Address) || (previous_mode_RK05f != edisk.mode_RK05f)){
                    printf("Drive_Address changed to %d.\r\n", edisk.Drive_Address);
                    display_disable_message_timer();
                    display_restart_invert_timer();
                    display_drive_address(edisk.Drive_Address, edisk.mode_RK05f, edisk.File_Ready ? edisk.imageName : (char *)"");
                }
            }
            manage_display_timers(&edisk);
            if(char_from_callback != 0){
                // if the key was L or l then begin logging events
                if((char_from_callback == 'L') || (char_from_callback == 'l')){
                    printf("  Begin logging events\r\n");
                    gpio_set_irq_enabled_with_callback(4, GPIO_IRQ_EDGE_RISE, true, &gpio_callback); // gpio callback
                }
                else if((char_from_callback == 'S') || (char_from_callback == 's')){
                    printf("  Stop logging events\r\n");
                    gpio_set_irq_enabled_with_callback(4, GPIO_IRQ_EDGE_RISE, false, &gpio_callback); // gpio callback
                }
                char_from_callback = 0; //reset the value
            }

            sleep_ms(100);
            ticker++;
        }
    }
    return 0;
}