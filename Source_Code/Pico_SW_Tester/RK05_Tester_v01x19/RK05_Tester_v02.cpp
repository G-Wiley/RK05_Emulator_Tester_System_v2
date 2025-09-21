// *********************************************************************************
// RK05_Tester_v02.cpp
//   Top Level main() function of the RK05 disk tester
// *********************************************************************************
// 
//===============================================================================================//
//                                                                                               //
// This software and related modules included by the top level module and files used to          //
// build the software are provided on an as-is basis. No warrantees or guarantees are provided   //
// or implied. Users of the RK05 Emulator or RK05 Tester shall not hold the developers of this   //
// software, firmware, hardware, or related documentation liable for any damages caused by       //
// any type of malfunction of the product including malfunctions caused by defects in the design //
// or operation of the software, firmware, hardware or use of related documentation or any       //
// combination thereof.                                                                          //
//                                                                                               //
//===============================================================================================//
//
#define SOFTWARE_MINOR_VERSION 19
#define SOFTWARE_VERSION 1

#include <stdio.h>
#include <pico/stdlib.h>
//#include <gpio.h>
#include <time.h> // Required for sleep_ms
#include <hardware/sync.h>
#include <string.h>

#include "disk_state_definitions.h"
#include "tester_hardware.h"
//#include "display_functions.h"
//#include "display_timers.h"
#include "tester_command.h"
//#include "display_big_images.h"
#include "display_functions.h"

// GLOBAL VARIABLES
Disk_State edisk;

#define INPUT_LINE_LENGTH 200
char inputdata[INPUT_LINE_LENGTH];
char *extract_argv[INPUT_LINE_LENGTH];
int extract_argc;

#define LIST_LENGTH 50
int list_cylinder[LIST_LENGTH];
int list_head[LIST_LENGTH];
int list_sector[LIST_LENGTH];
int list_count;

// callback code
int char_from_callback;

// Create a new display object at address 0xbc and size of 128x64

// Function Prototype
void update_drive_address(Disk_State* ddisk);

// main functions

// callback code
void callback(void *ptr){
    int *i = (int*) ptr;  // cast void pointer back to int pointer
    // read the character which caused to callback (and in the future read the whole string)
    *i = getchar_timeout_us(100); // length of timeout does not affect results
}

void initialize_states(){
    edisk.Drive_Address = 0;
    edisk.Tester_Ready = false;
    edisk.FPGA_version = 0;
    edisk.FPGA_minorversion = 0;
    edisk.tester_board_version = read_board_version();     // default setting is 0, which is not valid for this software version

    //edisk.tester_state = 0;
    edisk.rl_switch = false;
    edisk.p_wp_switch = edisk.wp_switch = false;

    //display_disable_message_timer();
    //display_restart_invert_timer();

    // initialize states to PDP-8 RK8-E values
    strcpy(edisk.controller, "RK8-E");
    strcpy(edisk.imageDescription, "image read from a disk connected to the Tester");
    strcpy(edisk.imageDate, "1/1/2025");
    edisk.bitRate = 1440000;
    edisk.preamble1Length = 120;
    edisk.preamble2Length = 82;
    // bit_times_sector_to_start = edisk.preamble1Length + edisk.preamble2Length;
    // was edisk.bit_times_sector_to_start = 210;
    edisk.bit_times_data_bits_after_start = 3104;
    edisk.postambleLength = 36;

    edisk.numberOfCylinders = 203;
    edisk.numberOfSectorsPerTrack = 16;
    edisk.numberOfHeads = 2;
    edisk.microsecondsPerSector = 2500;

    edisk.wtprot = false;
    edisk.rk11d = false;

    edisk.drive_board_version = 2; // default setting is latest board version
}

#define UART_ID uart0
#define BAUD_RATE 460800
#define UART_TX_PIN 0
#define UART_RX_PIN 1

void initialize_system() {
    stdio_init_all();
    sleep_ms(50);

    //initialize USB, commented out. Have not tested this
    // came from: https://stackoverflow.com/questions/76482416/c-c-pico-sdk-raspberry-pi-pico-read-strings-from-usb-cdc-serial-using-call
    //stdio_usb_init(); // init usb only.
    //while(!stdio_usb_connected()); // wait until USB connection
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

    printf("\r\n************* RK05 Tester STARTUP *************\n");
    //setup_display();
    initialize_gpio();
    printf(" *gpio initialized\n");
    microSD_LED_off();
    assert_fpga_reset();
    sleep_ms(10);
    deassert_fpga_reset();
    initialize_spi();
    printf(" *spi initialized\r\n");
    initialize_states();
    printf(" *software internal states initialized\n");
    initialize_fpga(&edisk);
    printf(" *fpga registers initialized\n");
    setup_display();

    printf(" *Tester software version %d.%d\r\n", SOFTWARE_VERSION, SOFTWARE_MINOR_VERSION);
    printf(" *FPGA version %d.%d\r\n", edisk.FPGA_version, edisk.FPGA_minorversion);
    printf(" *Tester Board version %d\r\n", edisk.tester_board_version);

    if(edisk.tester_board_version > 1)
        set_5bit_support();
    else
        clear_5bit_support();

    read_switches(&edisk);
    
    command_clear(); // clear all commands in case something is unintentionally asserted
}

int main() {
    int drive_address;
    int readback;
    int i;

    initialize_system();

    //stdio_init_all();
    // set the baud rate of stdio here
    printf(" RK05 Tester STARTING\n");
    display_rk05_tester();
    sleep_ms(3000);
    display_shutdown();
    
    if(is_it_a_tester() == false)
        printf("  ######## ERROR, hardware is an emulator, should be tester hardware ########\r\n");

    while (true) {
        read_switches(&edisk);
        if(edisk.Drive_Address < 0)
            printf("tester-Interface-Test-Mode>");
        else
            printf("tester-%x>", edisk.Drive_Address);
        i = 0;
        //display_rk05_tester();
        do {
            inputdata[i] = getchar() & 0x7f;
            //printf("{%x %d}",inputdata[i], i);
            switch(inputdata[i]){
                case '\b': //backspace
                case 0x7f: //delete
                   if(i > 0) printf("%c", inputdata[i--]);
                   break;
                case '\r': //return/enter
                   inputdata[i] = '\0';
                   break;
                default:
                   printf("%c", inputdata[i++]);
                   inputdata[i] = 0x7f;
                   break;
            }
            //manage_display_timers();
            //sleep_ms(100);
        } while ((inputdata[i] != '\0') && (i < INPUT_LINE_LENGTH));
        //display_shutdown();
        printf("\r\n");
        //printf("\r\n  %d chr, [%s], ", i, inputdata);
        extract_command_fields(inputdata);
        //printf("  extract_argc = %d\r\n", extract_argc); // debug/test code to view the extracted input line
        //for(i=0; i<25; i++){
            //if(inputdata[i] >= ' ')
                //printf("    %2d - %2x \'%c\'\r\n", i, inputdata[i], inputdata[i]);
            //else
                //printf("    %2d - %2x\r\n", i, inputdata[i]);
        //} // end of debug/test code
        command_parse_and_dispatch (&edisk);
        sleep_ms(100); // might remove this later or insert it in the command entry loop
    }
    return 0;
}