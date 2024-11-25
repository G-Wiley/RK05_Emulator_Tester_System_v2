// *********************************************************************************
// emulator_hardware.cpp
//  definitions and functions related to the emulator hardware
//  both GPIO and FPGA
// *********************************************************************************
// 
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "hardware/uart.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"

#include "disk_state_definitions.h"
#include "display_functions.h"
#include "emulator_state_definitions.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "time.h"

#define UART_ID uart0
#define BAUD_RATE 115200

// MOTORMIN and MOTORMAX are virtual PWM values that range from 0 to 20. With a 100 msec update it takes 2 seconds to open or close the door.
// The actual PWM values that get written to the hardware are mapped to hardware-specific values based on the state of servodutyfactor.
#define MOTORMIN 0  // was 4 for GPIO PWM, original was 3, sets the bottom of the inner arm, this is the Door Open position
#define MOTORMAX 20 // was 7 for GPIO PWM, original was 13, sets the top of the inner arm, this is the Door Closed position
#define MOTORDELTA 1
//#define SERVOPULSEGPIO 10

//#define PICO_DEFAULT_SPI_CSN_PIN 1 // commented out because compiler said it was re-defined

// GPIO PIN DEFINITIONS
#define GPIO_ON 1
#define GPIO_OFF 0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define DC_LOW_CPU_N 2
//#define SPARE_GP2 2
#define FPGA_HW_RESET_N 3
// CMD_INTERRUPT is GPIO4
#define SPARE_FP_GP22 22
#define SPARE_SPIRegSelect1 5
#define TESTMODE_SELECT 6
#define FP_CPU_RDY_ind 7
#define FP_CPU_FAULT_ind 8
#define FP_CPU_LOAD_ind 9
#define SP_SDLED_N 10
#define microSD_CD 11
#define microSD_MISO 12
#define microSD_CS 13
#define microSD_SCLK 14
#define microSD_MOSI 15
#define FPGA_SPI_MISO 16
#define FPGA_SPI_CS_n 17
#define FPGA_SPI_CLK 18
#define FPGA_SPI_MOSI  19
#define FP_switch_RUN_LOAD 20
#define FP_switch_WT_PROT 21
#define BOARD_VERSION_0 22
#define LED_PIN 25
#define ADC2 28

//FPGA VERSIONS
#define FPGA_MIN_VERSION 0
#define FPGA_MAX_VERSION 255

//FPGA CPU REGISTERS, WRITE
#define SPI_CONTROL_0 0
#define SPI_COMMAND_4 4
#define SPI_DRAM_ADDR_5 5
#define SPI_DRAM_DATA_6 6
#define SPI_TEST_REG1_7 7
#define SPI_TEST_REG2_8 8
#define SPI_TEST_REG3H_9 9
#define SPI_TEST_REG3L_A 0xa
#define SPI_POSTAMBLE_B 0xb
#define SPI_SECTPERTRK_C 0xc
#define SPI_BITCLKDIV_CP_D 0xd
#define SPI_BITCLKDIV_DP_E 0xe
#define SPI_BITPLSWIDTH_F 0xf
#define SPI_USECPERSECTH_10 0x10
#define SPI_USECPERSECTL_11 0x11
#define SPI_SERVO_PW_12 0x12
#define SPI_INTERFACE_TEST_MODE_20 0x20

//FPGA CPU REGISTERS, READ
#define SPI_STATUS_80 0x80
#define SPI_CYLADDR_81 0x81
#define SPI_DRVSTATUS_82 0x82
#define SPI_DRAMREAD_88 0x88
#define SPI_FUNCT_ID_89 0x89
#define SPI_FPGACODE_VER_90 0x90
#define SPI_FPGACODE_MINORVER_91 0x91
#define SPI_TEST_MODE_GRP1_94 0x94
#define SPI_TEST_MODE_GRP2_95 0x95
#define SPI_TEST_MODE_GRP3_96 0x96
#define SPI_READBACK_00_A0 0xa0
#define SPI_READBACK_00_A7 0xa7
#define SPI_READBACK_00_A8 0xa8
#define SPI_READBACK_00_A9 0xa9
#define SPI_READBACK_00_AA 0xaa
#define SPI_READBACK_00_AB 0xab
#define SPI_READBACK_00_AC 0xac
#define SPI_READBACK_00_AD 0xad
#define SPI_READBACK_00_AE 0xae
#define SPI_READBACK_00_AF 0xaf
#define SPI_READBACK_00_B0 0xb0
#define SPI_READBACK_00_B1 0xb1

#define DRIVE_ADDRESS_BITS 0x7
#define FILE_READY_BIT 0x10
#define FAULT_LATCH_BIT 0x20
#define DC_LOW_BIT 0x80
#define TOGGLE_WP_BIT 0x1

#define TEST_REG_3_LO_BIT_7 0X80

#define dc_lower_threshold 2850 // 3850 // equivalent of ~4.70 V
#define dc_upper_threshold 2940 // 3972 // equivalent of ~4.85 V

// drive door states
#define DOOROPEN 0
#define DOORCLOSED 1
#define DOORMOVING 2

#define DOOR_IS_CLOSING 1
#define DOOR_IS_OPENING 0

#define BUF_LEN 2
//#define PICO_DEFAULT_SPI_CSN_PIN 17

//static int debugdrivedoorstatus;
static int servodutyfactor;
static int servomovedirection;
static uint servo_slice_num;
static uint servo_chan;

static uint8_t dutyfactortable_fpga[21] = {47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 88};

#ifdef PICO_DEFAULT_SPI_CSN_PIN
static inline void cs_select()
{
    asm volatile("nop \n nop \n nop");
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect()
{
    asm volatile("nop \n nop \n nop");
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);
    asm volatile("nop \n nop \n nop");
}
#endif

// *************** FPGA SPI Registers ***************
//
uint8_t read_write_spi_register(uint8_t reg, uint8_t data)
{
    uint8_t out_buf [BUF_LEN], in_buf [BUF_LEN];
    uint8_t buf[2];
    out_buf[0] = reg;
    out_buf[1] = data;
    cs_select();
    spi_write_read_blocking (spi_default, out_buf, in_buf, 2);
    cs_deselect();
    return(in_buf[1]);
}

void write_spi_register(uint8_t reg, uint8_t data)
{
    uint8_t out_buf [BUF_LEN], in_buf [BUF_LEN];
    uint8_t buf[2];
    out_buf[0] = reg;
    out_buf[1] = data;
    cs_select();
    spi_write_read_blocking (spi_default, out_buf, in_buf, 2);
    cs_deselect();
}

void toggle_wp()
{
    write_spi_register(SPI_COMMAND_4, TOGGLE_WP_BIT);
}

void set_file_ready()
{
    printf("set_file_ready\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | FILE_READY_BIT;
    write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

void clear_file_ready()
{
    //int tempctrlreg;
    printf("clear_file_ready\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    //tempctrlreg = 0;
    tempctrlreg = tempctrlreg & ~FILE_READY_BIT;
    write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

void set_fault_latch()
{
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | FAULT_LATCH_BIT;
    write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

void clear_fault_latch()
{
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg & ~FAULT_LATCH_BIT;
    write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

void set_dc_low()
{
    gpio_put(DC_LOW_CPU_N, GPIO_OFF);
    //int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    //tempctrlreg = tempctrlreg | DC_LOW_BIT;
    //write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

void clear_dc_low()
{
    gpio_put(DC_LOW_CPU_N, GPIO_ON);
    //int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    //tempctrlreg = tempctrlreg & ~DC_LOW_BIT;
    //write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

uint8_t read_reg00()
{
    uint8_t tempreg00 = read_write_spi_register(SPI_READBACK_00_A0, 0);
    return(tempreg00);
}

void led_from_bits(int walking_bit){
    write_spi_register(SPI_TEST_REG3L_A, walking_bit & 0xff);
}

void enable_interface_test_mode(){
    write_spi_register(SPI_INTERFACE_TEST_MODE_20, 0x55);
}

void assert_outputs(int step_count){
    int walking_one = 1 << (step_count & 0x7);
    if((step_count >= 0) && (step_count <= 7)){
        write_spi_register(SPI_TEST_REG1_7, walking_one);
        write_spi_register(SPI_TEST_REG2_8, 0);
        write_spi_register(SPI_TEST_REG3L_A, 0);
        if(step_count == 6)
            set_dc_low();
        else
            clear_dc_low();
        if((step_count & 1) == 1)
            write_spi_register(SPI_TEST_REG3H_9, 8);
        else
            write_spi_register(SPI_TEST_REG3H_9, 0);
    }
    else if((step_count >= 8) && (step_count <= 15)){
        write_spi_register(SPI_TEST_REG1_7, 0);
        write_spi_register(SPI_TEST_REG2_8, walking_one);
        write_spi_register(SPI_TEST_REG3L_A, 0);
        clear_dc_low();
        if((step_count & 1) == 1)
            write_spi_register(SPI_TEST_REG3H_9, 8);
        else
            write_spi_register(SPI_TEST_REG3H_9, 0);
    }
    else if((step_count >= 16) && (step_count <= 19)){ // activates the 3 test outputs added to v1 hardware
        write_spi_register(SPI_TEST_REG1_7, 0);
        write_spi_register(SPI_TEST_REG2_8, 0);
        write_spi_register(SPI_TEST_REG3L_A, 0);
        clear_dc_low();
        if((step_count & 1) == 1)
            write_spi_register(SPI_TEST_REG3H_9, walking_one | 8);
        else
            write_spi_register(SPI_TEST_REG3H_9, walking_one);
    }
    else if((step_count >= 20) && (step_count <= 21)){ // activates the 5th sector bit added to v2 hardware
        write_spi_register(SPI_TEST_REG1_7, 0);
        write_spi_register(SPI_TEST_REG2_8, 0);
        clear_dc_low();
        if(step_count == 20)
            write_spi_register(SPI_TEST_REG3L_A, TEST_REG_3_LO_BIT_7);
        else
            write_spi_register(SPI_TEST_REG3L_A, 0);
        if((step_count & 1) == 1)
            write_spi_register(SPI_TEST_REG3H_9, 8);
        else
            write_spi_register(SPI_TEST_REG3H_9, 0);
    }
}

int read_test_inputs(){
    int retval = read_write_spi_register(SPI_TEST_MODE_GRP1_94, 0);
    retval |= (read_write_spi_register(SPI_TEST_MODE_GRP2_95, 0) & 0xff) << 8;
    retval |= (read_write_spi_register(SPI_TEST_MODE_GRP3_96, 0) & 0xff) << 16;
    return(retval);
}

int read_int_inputs(){
    int retval = read_write_spi_register(SPI_CYLADDR_81, 0);
    retval |= (read_write_spi_register(SPI_DRVSTATUS_82, 0) & 0xff) << 8;
    retval |= (read_write_spi_register(SPI_TEST_MODE_GRP2_95, 0) & 0xff) << 16;
    return(retval);
}

void load_drive_address(int d_addr)
{
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = (tempctrlreg & ~DRIVE_ADDRESS_BITS) | d_addr;
    write_spi_register(SPI_CONTROL_0, tempctrlreg & 0xff);
}

int read_fpga_version()
{
    int readversion = read_write_spi_register(SPI_FPGACODE_VER_90, 0);
    return(readversion);
}

int read_fpga_minorversion()
{
    int readminorversion = read_write_spi_register(SPI_FPGACODE_MINORVER_91, 0);
    return(readminorversion);
}

bool is_it_a_tester()
{
    int readback = read_write_spi_register(SPI_FUNCT_ID_89, 0);
    bool istester = ((readback & 0x80) == 0x80) ? true : false;
    return(istester);
}

int read_board_version()
{
    // read the pin value to determine the board version
    int retval = (gpio_get(BOARD_VERSION_0) == 0) ? 2 : 1;
    //retval = 1; // for debug only, makes v2 board look like v1 ============================================

    // then initialize the GPIO as an output that can be used for software scope timing and debugging
    gpio_init(BOARD_VERSION_0);
    gpio_set_dir(BOARD_VERSION_0, GPIO_OUT);
    return(retval);
}

void load_ram_address(int ramaddress)
{
    write_spi_register(SPI_DRAM_ADDR_5, (ramaddress >> 16) & 0xff);
    write_spi_register(SPI_DRAM_ADDR_5, (ramaddress >> 8)  & 0xff);
    write_spi_register(SPI_DRAM_ADDR_5,  ramaddress        & 0xff);
}

void storebyte(int bytevalue)
{
    write_spi_register(SPI_DRAM_DATA_6, bytevalue & 0xff);
}

int readbyte()
{
    int readdata = read_write_spi_register(SPI_DRAMREAD_88, 0);
    return(readdata);

}

// update the FPGA registers from the disk drive parameters read from the header in the RK05 image file
//
void update_fpga_disk_state(Disk_State* ddisk){
    if(ddisk->bitRate == 1440000){
        write_spi_register(SPI_BITCLKDIV_CP_D, 14);
        write_spi_register(SPI_BITCLKDIV_DP_E, 14);
        write_spi_register(SPI_BITPLSWIDTH_F, 6);
    }
    else if(ddisk->bitRate == 1545000){
        write_spi_register(SPI_BITCLKDIV_CP_D, 13);
        write_spi_register(SPI_BITCLKDIV_DP_E, 13);
        write_spi_register(SPI_BITPLSWIDTH_F, 6);
    }
    else if(ddisk->bitRate == 1600000){
        write_spi_register(SPI_BITCLKDIV_CP_D, 12);
        write_spi_register(SPI_BITCLKDIV_DP_E, 13);
        write_spi_register(SPI_BITPLSWIDTH_F, 6);
    }
    else
        printf("###ERROR, unknown disk bitRate %d\r\n", ddisk->bitRate);
    //write_spi_register(SPI_TEST_REG1_7, ddisk->preamble1Length);
    //write_spi_register(SPI_TEST_REG2_8, ddisk->preamble2Length);
    //write_spi_register(SPI_TEST_REG3H_9, ddisk->dataLength >> 8);
    //write_spi_register(SPI_TEST_REG3L_A, ddisk->dataLength & 0xff);
    //write_spi_register(SPI_POSTAMBLE_B, ddisk->postambleLength);
    //write_spi_register(<NO_REGISTER_FOR_THIS_YET>, ddisk->numberOfCylinders); // FPGA is coded with a constant == 203
    write_spi_register(SPI_SECTPERTRK_C, ddisk->numberOfSectorsPerTrack);
    //write_spi_register(<NO_REGISTER_FOR_THIS_YET>, ddisk->numberOfHeads); // FPGA is coded with a constant of 2 heads
    write_spi_register(SPI_USECPERSECTH_10, ddisk->microsecondsPerSector >> 8);
    write_spi_register(SPI_USECPERSECTL_11, ddisk->microsecondsPerSector & 0xff);
}

// *************** CPU GPIO Signals ***************
//
void read_rocker_switches(struct Disk_State* ddisk)
{
    ddisk->rl_switch = (gpio_get(FP_switch_RUN_LOAD) == 0) ? true : false; // invert switch value because active switch is grounded
    // if the RUN/LOAD switch is in the RUN position and the drive is unloaded then begin the loading process
    //if((ddisk->rl_switch) && (ddisk->run_load_state == RLST0)) ddisk->run_load_state = RLST1;
    // if the RUN/LOAD switch is in the LOAD position and the drive is running then begin the unloading process
    //if((ddisk->run_load_state == RLST10) && (~ddisk->rl_switch)) ddisk->run_load_state = RLST11;

    // if the WT PROT switch is moved from the lower to the upper position then toggle the WT PROT bit in the FPGA Mode register
    ddisk->p_wp_switch = ddisk->wp_switch; // now the present switch state becomes the previous state
    ddisk->wp_switch = (gpio_get(FP_switch_WT_PROT) == 0) ? true : false; //invert WP switch value because active switch is grounded
    //if((ddisk->wp_switch == 1) && (ddisk->p_wp_switch == 0)) toggle_wp(); // if the switch changes from not pressed to press then toggle the FPGA bit.
}

bool read_load_switch()
{
    bool retval = (gpio_get(FP_switch_RUN_LOAD) == 0) ? true : false; // invert switch value because active switch is grounded
    return(retval);
}

bool read_wp_switch()
{
    bool retval = (gpio_get(FP_switch_WT_PROT) == 0) ? true : false; //invert WP switch value because active switch is grounded
    return(retval);
}

bool is_testmode_selected(){
    bool retval = gpio_get(TESTMODE_SELECT) == 0 ? true : false; // it's testmode if the testpoint is grounded
    return(retval);
}

void assert_fpga_reset(){
    gpio_put(FPGA_HW_RESET_N, GPIO_OFF);
}

void deassert_fpga_reset(){
    gpio_put(FPGA_HW_RESET_N, GPIO_ON);
}

// indicators are active low, so GPIO_ON turns the LED off and GPIO_OFF turns the LED on
void set_cpu_ready_indicator()
{
    gpio_put(FP_CPU_RDY_ind, GPIO_OFF);
    // sets FPGA File_Ready = 1;
}

void clear_cpu_ready_indicator()
{
    gpio_put(FP_CPU_RDY_ind, GPIO_ON);
    // sets FPGA File_Ready = 0;
}

// microSD LED is active low, so GPIO_ON turns the LED off and GPIO_OFF turns the LED on
void microSD_LED_on()
{
    gpio_put(SP_SDLED_N, GPIO_OFF);
}

void microSD_LED_off()
{
    gpio_put(SP_SDLED_N, GPIO_ON);
}

void set_cpu_fault_indicator()
{
    gpio_put(FP_CPU_FAULT_ind, GPIO_OFF);
    // sets FPGA Fault_Latch = 1;
}

void clear_cpu_fault_indicator()
{
    gpio_put(FP_CPU_FAULT_ind, GPIO_ON);
    // sets FPGA Fault_Latch = 0;
}

void set_cpu_load_indicator()
{
    gpio_put(FP_CPU_LOAD_ind, GPIO_OFF);
}

void clear_cpu_load_indicator()
{
    gpio_put(FP_CPU_LOAD_ind, GPIO_ON);
}
// ********************************* end of indicators *********************************

// check_dc_low reads the supply voltage and sets the DC Low bit in the FPGA
// There is hysteresis depending on the current state of dc_low
//
void check_dc_low(struct Disk_State* ddisk)
{
    adc_init();
    adc_gpio_init(ADC2);
    adc_select_input(2);
    sleep_ms(10);
    uint16_t adc_result = adc_read();
    ddisk->debug_vsense = adc_result;
    bool previous_dc_low = ddisk->dc_low;
    //printf("adc_read() = %d\r\n", adc_result);

    // hysteresis depending on the current state of dc_low
    if(ddisk->dc_low == false)
        ddisk->dc_low = (adc_result < dc_lower_threshold) ? true : false;
    else
        ddisk->dc_low = (adc_result > dc_upper_threshold) ? false : true;

    if(ddisk->dc_low){ 
        set_dc_low();
        display_error((char*) "DC Low", (char*) "detected");
        if(previous_dc_low != ddisk->dc_low)
            printf("###ERROR, DC Low detected.\r\n");
    }
    else{
        clear_dc_low();
        if(previous_dc_low != ddisk->dc_low)
            printf("DC Voltage restored.\r\n");
    }
}

// Is the microSD card present? returns true if present and false if removed
//
bool is_card_present()
{
    // socket switch is closed when card is present, so low is card present, high is card removed
    bool card_present = (gpio_get(microSD_CD) == GPIO_ON) ? false : true;
    return(card_present);
}

void close_drive_door()
{
    servodutyfactor = MOTORMIN;
    servomovedirection = DOOR_IS_CLOSING;
    //debugdrivedoorstatus = DOORCLOSED;
    //printf("close_drive_door [%d]\r\n", debugdrivedoorstatus);
}

void open_drive_door()
{
    servodutyfactor = MOTORMAX;
    servomovedirection = DOOR_IS_OPENING;
    //debugdrivedoorstatus = DOOROPEN;
    //printf("open_drive_door [%d]\r\n", debugdrivedoorstatus);
}

// update the servo PWM duty factor based on the door state and return the status
//
int drive_door_status()
{
    // perform this is the door is closing
    if(servomovedirection == DOOR_IS_CLOSING){
        servodutyfactor += MOTORDELTA;
        write_spi_register(SPI_SERVO_PW_12, dutyfactortable_fpga[servodutyfactor]);
        if(servodutyfactor >= MOTORMAX){
            return(DOORCLOSED);
            servodutyfactor = MOTORMAX;
        }
        else{
            return(DOORMOVING);
        }
    }
    //else perform this if the door is opening
    else{
        servodutyfactor -= MOTORDELTA;
        write_spi_register(SPI_SERVO_PW_12, dutyfactortable_fpga[servodutyfactor]);
        if(servodutyfactor <= MOTORMIN){
            return(DOOROPEN);
            servodutyfactor = MOTORMIN;
        }
        else{
            return(DOORMOVING);
        }
    }
}

void initialize_gpio()
{
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    //gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
    //gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, GPIO_OFF);

    // FPGA Hardware Reset, active low
    gpio_init(FPGA_HW_RESET_N);
    gpio_set_dir(FPGA_HW_RESET_N, GPIO_OUT);
    gpio_put(FPGA_HW_RESET_N, GPIO_ON);

    // DC Low driven by the CPU, active low
    gpio_init(DC_LOW_CPU_N);
    gpio_set_dir(DC_LOW_CPU_N, GPIO_OUT);
    gpio_put(DC_LOW_CPU_N, GPIO_ON);

    // Front Panel RDY indicator, driven by the CPU
    gpio_init(FP_CPU_RDY_ind);
    gpio_set_dir(FP_CPU_RDY_ind, GPIO_OUT);
    gpio_put(FP_CPU_RDY_ind, GPIO_ON);

    // Front Panel FAULT indicator, driven by the CPU
    gpio_init(FP_CPU_FAULT_ind);
    gpio_set_dir(FP_CPU_FAULT_ind, GPIO_OUT);
    gpio_put(FP_CPU_FAULT_ind, GPIO_ON);

    // Front Panel LOAD indicator, driven by the CPU
    gpio_init(FP_CPU_LOAD_ind);
    gpio_set_dir(FP_CPU_LOAD_ind, GPIO_OUT);
    gpio_put(FP_CPU_LOAD_ind, GPIO_ON);

    // microSD LED driven by the CPU, active low
    gpio_init(SP_SDLED_N);
    gpio_set_dir(SP_SDLED_N, GPIO_OUT);
    gpio_put(SP_SDLED_N, GPIO_OFF);


    // microSD Card Detect input
    gpio_init(microSD_CD);
    gpio_set_dir(microSD_CD, GPIO_IN);

    // Front Panel Run/Load switch input
    gpio_init(FP_switch_RUN_LOAD);
    gpio_set_dir(FP_switch_RUN_LOAD, GPIO_IN);

    // Front Panel WT PROT switch input
    gpio_init(FP_switch_WT_PROT);
    gpio_set_dir(FP_switch_WT_PROT, GPIO_IN);

    // GPIO22 for a hardware toggle for debugging
    gpio_init(BOARD_VERSION_0);
    gpio_set_dir(BOARD_VERSION_0, GPIO_IN);
    gpio_pull_up(BOARD_VERSION_0); //enable the pullup. hardware v2 and after has a 0-ohm pulldown resistor

    // GPIO6 pin to check for Interface Test Mode
    gpio_init(TESTMODE_SELECT);
    gpio_set_dir(TESTMODE_SELECT, GPIO_IN);
    gpio_pull_up(TESTMODE_SELECT); //enable the pullup. hardware v1 and after also has a pullup resistor

    // ADC2 input to measure +5V / 2
    //adc_init();
    //adc_gpio_init(ADC2);
    //adc_select_input(2);
    //uint16_t adcresult = adc_read();
    //printf("adc_read() = %d\r\n", adcresult);

    initialize_pca9557(); // IC on the front panel PCB used to read the state of the device select switches

    clear_cpu_ready_indicator();
    clear_cpu_fault_indicator();
    set_cpu_load_indicator();

    //initialize_servo();
}

void initialize_spi()
{
    // initialize SPI to run at 25 MHz
    // This is the CPU to FPGA SPI link.
    spi_init(spi_default, 25 * 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    // SPI configuration needs to be CPHA = 0, CPOL = 0
    // resting state of the CPI clock is low, data is captured in the RPi Pico and in the FPGA on rising edge
    // so data needs to change on the falling edge and be set up half a clock prior to the first clock rising edge
    //spi_set_format(spi_default, )

    // Make the SPI pins available to picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_SPI_TX_PIN, PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI));

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);

    // Make the CS pin available to picotool
    bi_decl(bi_1pin_with_name(PICO_DEFAULT_SPI_CSN_PIN, "SPI CS"));
}

void initialize_uart()
{
    // Set up our UART with the required speed.
    sleep_ms(500);
    uart_init(UART_ID, BAUD_RATE);
    sleep_ms(500);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    // We are using GP0 and GP1 for the UART, package pins 1 & 2
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    sleep_ms(500);

    printf("UART is initialized\r\n");
}

void initialize_fpga(struct Disk_State* ddisk)
{
    //fpga_ctrl_reg_image = 0;
    //update_drive_address(ddisk);
    clear_file_ready();
    ddisk->File_Ready = false;
    clear_fault_latch();
    ddisk->Fault_Latch = false;
    clear_dc_low();
    ddisk->dc_low = false;

    //check_dc_low(ddisk);

    // read the FPGA version and later confirm whether it's compatible with the software version
    ddisk->FPGA_version = read_fpga_version();
    ddisk->FPGA_minorversion = read_fpga_minorversion();
}

//void boot_open_the_door()
//{
    //
//}
