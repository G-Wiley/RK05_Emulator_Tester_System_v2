// *********************************************************************************
// tester_hardware.cpp
//  definitions and functions related to the emulator hardware
//  both GPIO and FPGA
// *********************************************************************************
// 
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
//#include "include_libs/adc.h"

#include "hardware/uart.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"

#include "disk_state_definitions.h"
#include "display_functions.h"
//#include "tester_state_definitions.h" //commented-out 2/5/2025

//#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "time.h"

#define UART_ID uart0
#define BAUD_RATE 115200

//#define PICO_DEFAULT_SPI_CSN_PIN 1 // commented out because compiler said it was re-defined

// GPIO PIN DEFINITIONS
#define GPIO_ON 1
#define GPIO_OFF 0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define BUS_RESTORE 2
#define FPGA_HW_RESET_N 3
#define SPARE_FP_GP22 22
#define SPARE_SPIRegSelect0 4
#define SPARE_SPIRegSelect1 5
#define SPARE_GP6 6
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
#define ADC3 29

//FPGA VERSIONS
#define FPGA_MIN_VERSION 0
#define FPGA_MAX_VERSION 255

//FPGA CPU REGISTERS, WRITE
#define SPI_DRIVE_ADDRESS_0 0
#define SPI_CYLINDER_ADDRESS_1 1
#define SPI_SECTOR_ADDRESS_2 2
#define SPI_CONTROL_3 3
#define SPI_COMMAND_4 4
#define SPI_DRAM_ADDR_5 5
#define SPI_DRAM_DATA_6 6
#define SPI_PREAMBLE1_7 7
#define SPI_PREAMBLE2_8 8
#define SPI_DATA_LENH_9 9
#define SPI_DATA_LENL_A 0xa
#define SPI_POSTAMBLE_B 0xb
#define SPI_SECTPERTRK_C 0xc
#define SPI_BITCLKDIV_CP_D 0xd
#define SPI_BITCLKDIV_DP_E 0xe
#define SPI_BITPLSWIDTH_F 0xf
#define SPI_USECPERSECTH_10 0x10
#define SPI_USECPERSECTL_11 0x11
#define SPI_INTERFACE_TEST_MODE_20 0x20

//FPGA CPU REGISTERS, READ
#define SPI_DRIVESTATUS1_80 0x80
#define SPI_DRIVESTATUS2_81 0x81
#define SPI_DRAMREAD_88 0x88
#define SPI_FUNCT_ID_89 0x89
#define SPI_FPGACODE_VER_90 0x90
#define SPI_FPGACODE_MINORVER_91 0x91
#define SPI_TEST_MODE_GRP1_94 0x94
#define SPI_TEST_MODE_GRP2_95 0x95
#define SPI_TEST_MODE_GRP3_96 0x96
#define SPI_READBACK_00_A0 0xa0
#define SPI_READBACK_00_A1 0xa1
#define SPI_READBACK_00_A2 0xa2
#define SPI_READBACK_00_A3 0xa3
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
#define RK11DE_MODE_BIT 0x8
#define TESTER_READY_BIT 0x10
#define ON_CYL_INDICATOR_BIT 0x20
#define SUPPORT_5BIT_BIT 0x40
#define RESTORE_BIT 0x01
#define HEAD_SELECT_BIT 0x02
#define WRITE_PROTECT_BIT 0x04
#define SEEK_COMMAND_BIT 0x10
#define WRITE_SECTOR_COMMAND_BIT 0x02
#define READ_SECTOR_COMMAND_BIT 0x01
#define COMMAND_CLEAR_BIT 0x80

#define BUF_LEN 2
//#define PICO_DEFAULT_SPI_CSN_PIN 17

//Function Prototypes
//void update_drive_address(Disk_State* ddisk);

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

int read_drive_status1(){
    int retval;
    retval = read_write_spi_register(SPI_DRIVESTATUS1_80, 0);
    return(retval);
}

int read_drive_status2(){
    int retval;
    retval = read_write_spi_register(SPI_DRIVESTATUS2_81, 0);
    return(retval);
}

bool is_read_in_progress(){
    bool retval;
    retval = (read_drive_status2() & 0x40) == 0 ? false : true;
    return(retval);
}

bool is_write_in_progress(){
    bool retval;
    retval = (read_drive_status2() & 0x80) == 0 ? false : true;
    return(retval);
}

bool is_rws_ready(){
    bool retval;
    retval = (read_drive_status2() & 0x01) == 0 ? false : true;
    return(retval);
}

bool is_addr_accepted_ready(){
    bool retval;
    retval = (read_drive_status1() & 0x40) == 0 ? false : true;
    return(retval);
}

void load_sector_address(int sect_addr)
{
    //printf(" load_sector_address=%d\r\n", sect_addr);
    write_spi_register(SPI_SECTOR_ADDRESS_2, sect_addr & 0xff);
}

void load_drive_address(int d_addr)
{
    //printf(" load_drive_address=%d\r\n", d_addr);
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = (tempctrlreg & ~DRIVE_ADDRESS_BITS) | d_addr;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void set_rk11de_mode()
{
    printf("  set_rk11de_mode\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | RK11DE_MODE_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void clear_rk11de_mode()
{
    printf("  clear_rk11de_mode\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg & ~RK11DE_MODE_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void set_wtprot_mode()
{
    printf("  set_wtprot_mode\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A3, 0);
    tempctrlreg = tempctrlreg | WRITE_PROTECT_BIT;
    write_spi_register(SPI_CONTROL_3, tempctrlreg & 0xff);
}

void clear_wtprot_mode()
{
    printf("  clear_wtprot_mode\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A3, 0);
    tempctrlreg = tempctrlreg & ~WRITE_PROTECT_BIT;
    write_spi_register(SPI_CONTROL_3, tempctrlreg & 0xff);
}

void set_tester_ready()
{
    printf("  set_tester_ready\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | TESTER_READY_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void clear_tester_ready()
{
    printf("  clear_tester_ready\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg & ~TESTER_READY_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void set_on_cyl_indicator()
{
    printf("  set_on_cyl_indicator\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | ON_CYL_INDICATOR_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void clear_on_cyl_indicator()
{
    printf("  clear_on_cyl_indicator\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg & ~ON_CYL_INDICATOR_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void set_5bit_support()
{
    printf("  set_5bit_support\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg | SUPPORT_5BIT_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void clear_5bit_support()
{
    printf("  clear_5bit_support\r\n");
    int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A0, 0);
    tempctrlreg = tempctrlreg & ~SUPPORT_5BIT_BIT;
    write_spi_register(SPI_DRIVE_ADDRESS_0, tempctrlreg & 0xff);
}

void led_from_bits(int walking_bit){
    write_spi_register(SPI_DATA_LENL_A, walking_bit & 0xff);
}

void enable_interface_test_mode(){
    write_spi_register(SPI_INTERFACE_TEST_MODE_20, 0x55);
}

void assert_outputs(int step_count, int hw_version){
    int clock_bit;
    if(hw_version == 0)
        clock_bit = 1;
    else
        clock_bit = 8;
    int walking_one = 1 << (step_count & 0x7);
    if((step_count >= 0) && (step_count <= 7)){
        write_spi_register(SPI_PREAMBLE1_7, walking_one);
        write_spi_register(SPI_PREAMBLE2_8, 0);
        if((step_count & 1) == 1)
            write_spi_register(SPI_DATA_LENH_9, clock_bit);
        else
            write_spi_register(SPI_DATA_LENH_9, 0);
    }
    else if((step_count >= 8) && (step_count <= 15)){
        write_spi_register(SPI_PREAMBLE1_7, 0);
        write_spi_register(SPI_PREAMBLE2_8, walking_one);
        if((step_count & 1) == 1)
            write_spi_register(SPI_DATA_LENH_9, clock_bit);
        else
            write_spi_register(SPI_DATA_LENH_9, 0);
    }
    else if((step_count >= 16) && (step_count <= 21)){
        write_spi_register(SPI_PREAMBLE1_7, 0);
        write_spi_register(SPI_PREAMBLE2_8, 0);
        if((step_count & 1) == 1)
            write_spi_register(SPI_DATA_LENH_9, walking_one | clock_bit);
        else
            write_spi_register(SPI_DATA_LENH_9, walking_one);
    }
}

int read_test_inputs(){
    int retval = read_write_spi_register(SPI_TEST_MODE_GRP1_94, 0);
    retval |= (read_write_spi_register(SPI_TEST_MODE_GRP2_95, 0) & 0xff) << 8;
    retval |= (read_write_spi_register(SPI_TEST_MODE_GRP3_96, 0) & 0xff) << 16;
    return(retval);
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

void assert_bus_restore(){
    gpio_put(BUS_RESTORE, GPIO_OFF);
}

void deassert_bus_restore(){
    gpio_put(BUS_RESTORE, GPIO_ON);
}

void seek_to_cylinder(int cylinder, bool restore){
    write_spi_register(SPI_CYLINDER_ADDRESS_1, cylinder & 0xff);
    //int tempctrlreg = read_write_spi_register(SPI_READBACK_00_A3, 0);
    if(restore) // if restore command in seek list then set the Restore bit
        //tempctrlreg = tempctrlreg | RESTORE_BIT; 
        assert_bus_restore();
    else // if restore command in seek list is not set then clear the Restore bit
       //tempctrlreg = tempctrlreg & ~RESTORE_BIT; 
       deassert_bus_restore();
    
    //write_spi_register(SPI_CONTROL_3, tempctrlreg & 0xff); // write back the Restore bit
    write_spi_register(SPI_COMMAND_4, SEEK_COMMAND_BIT); // issue the Seek command
}

void select_head(int head){
    //printf(" select_head%d\r\n", head);
    int tempreg = read_write_spi_register(SPI_READBACK_00_A3, 0);
    tempreg = tempreg & ~HEAD_SELECT_BIT; // clear the head bit in tempreg
    if(head != 0) // if the head bit parameter is non-zero then set the head bit in tempreg
        tempreg = tempreg | HEAD_SELECT_BIT;
    write_spi_register(SPI_CONTROL_3, tempreg & 0xff); //write-back the register value with the head bit 
}

void write_sector(){
    // prior to this, must perform the following:
    //   1. seek_to_cylinder(int cylinder, bool restore)
    //   2. select_head(int head)
    //   3. load_sector_address(int sect_addr)
    //   4. RWS must be ready in the drive
    //printf(" write sector command\r\n");
    write_spi_register(SPI_COMMAND_4, WRITE_SECTOR_COMMAND_BIT);
}

void read_sector(){
    // prior to this, must perform the following:
    //   1. seek_to_cylinder(int cylinder, bool restore)
    //   2. select_head(int head)
    //   3. load_sector_address(int sect_addr)
    //   4. RWS must be ready in the drive
    //printf(" read sector command\r\n");
    write_spi_register(SPI_COMMAND_4, READ_SECTOR_COMMAND_BIT);
}

void command_clear()
{
    write_spi_register(SPI_COMMAND_4, COMMAND_CLEAR_BIT);
}

// update the FPGA registers from the disk drive parameters read from the JSON header in the RK05 image file
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
        write_spi_register(SPI_BITPLSWIDTH_F, 5);
    }
    else if(ddisk->bitRate == 1600000){
        write_spi_register(SPI_BITCLKDIV_CP_D, 12);
        write_spi_register(SPI_BITCLKDIV_DP_E, 13);
        write_spi_register(SPI_BITPLSWIDTH_F, 5);
    }
    else
        printf("###ERROR, unknown disk bitRate %d\r\n", ddisk->bitRate);
    write_spi_register(SPI_PREAMBLE1_7, ddisk->preamble1Length);
    write_spi_register(SPI_PREAMBLE2_8, ddisk->preamble2Length);
    write_spi_register(SPI_DATA_LENH_9, (ddisk->bit_times_data_bits_after_start >> 8) & 0xff);
    write_spi_register(SPI_DATA_LENL_A, ddisk->bit_times_data_bits_after_start & 0xff);
    write_spi_register(SPI_POSTAMBLE_B, ddisk->postambleLength);
    //write_spi_register(<NO_REGISTER_FOR_THIS_YET>, ddisk->numberOfCylinders); // FPGA is coded with a constant == 203
    write_spi_register(SPI_SECTPERTRK_C, ddisk->numberOfSectorsPerTrack);
    //write_spi_register(<NO_REGISTER_FOR_THIS_YET>, ddisk->numberOfHeads); // FPGA is coded with a constant of 2 heads
    write_spi_register(SPI_USECPERSECTH_10, ddisk->microsecondsPerSector >> 8);
    write_spi_register(SPI_USECPERSECTL_11, ddisk->microsecondsPerSector & 0xff);
    if(ddisk->rk11d)
        set_rk11de_mode();
    else
        clear_rk11de_mode();
}

// *************** CPU GPIO Signals ***************
//
void read_switches(Disk_State* ddisk)
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

void assert_fpga_reset(){
    gpio_put(FPGA_HW_RESET_N, GPIO_OFF);
}

void deassert_fpga_reset(){
    gpio_put(FPGA_HW_RESET_N, GPIO_ON);
}

void assert_GP6(){
    gpio_put(SPARE_GP6, GPIO_ON);
}

void deassert_GP6(){
    gpio_put(SPARE_GP6, GPIO_OFF);
}

// indicators are active low, so GPIO_ON turns the LED off and GPIO_OFF turns the LED on
void set_cpu_ready_indicator()
{
    gpio_put(FP_CPU_RDY_ind, GPIO_OFF);
    //File_Ready = 1;
}

void clear_cpu_ready_indicator()
{
    gpio_put(FP_CPU_RDY_ind, GPIO_ON);
    //File_Ready = 0;
}

void set_cpu_fault_indicator()
{
    gpio_put(FP_CPU_FAULT_ind, GPIO_OFF);
    //Fault_Latch = 1;
}

void clear_cpu_fault_indicator()
{
    gpio_put(FP_CPU_FAULT_ind, GPIO_ON);
    //Fault_Latch = 0;
}

void set_cpu_load_indicator()
{
    gpio_put(FP_CPU_LOAD_ind, GPIO_OFF);
}

void clear_cpu_load_indicator()
{
    gpio_put(FP_CPU_LOAD_ind, GPIO_ON);
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
// ********************************* end of indicators *********************************

/*
void check_dc_low(Disk_State* ddisk)
{
    //uint16_t result = adc_read();
    uint16_t result = Value_for_debug_5V;

    if(ddisk->dc_low == false)
        ddisk->dc_low = result < dc_lower_threshold ? 1 : 0;
    else
        ddisk->dc_low = result > dc_upper_threshold ? 0 : 1;

    if(ddisk->dc_low){ 
        set_dc_low();
        display_error((char*) "DC Low", (char*) "detected");
    }
    else
        clear_dc_low();
}
*/

bool is_card_present()
{
    // socket switch is closed when card is present, so low is card present, high is card removed
    bool card_present = (gpio_get(microSD_CD) == GPIO_ON) ? false : true;
    return(card_present);
}

void initialize_gpio()
{
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    //gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
    //gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);

    // set up SPARE_GP6 so software can toggle it for real-time debugging
    gpio_init(SPARE_GP6);
    gpio_set_dir(SPARE_GP6, GPIO_OUT);
    gpio_put(SPARE_GP6, GPIO_OFF);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, GPIO_OFF);

    // RK05 Bus Restore signal, active low, is DC Low on the Emulator
    gpio_init(BUS_RESTORE);
    gpio_set_dir(BUS_RESTORE, GPIO_OUT);
    gpio_put(BUS_RESTORE, GPIO_ON);

    //FPGA Hardware Reset, active low
    gpio_init(FPGA_HW_RESET_N);
    gpio_set_dir(FPGA_HW_RESET_N, GPIO_OUT);
    gpio_put(FPGA_HW_RESET_N, GPIO_ON);

    //x Front Panel RDY indicator, driven by the CPU
    gpio_init(FP_CPU_RDY_ind);
    gpio_set_dir(FP_CPU_RDY_ind, GPIO_OUT);
    gpio_put(FP_CPU_RDY_ind, GPIO_ON);

    //x Front Panel FAULT indicator, driven by the CPU
    gpio_init(FP_CPU_FAULT_ind);
    gpio_set_dir(FP_CPU_FAULT_ind, GPIO_OUT);
    gpio_put(FP_CPU_FAULT_ind, GPIO_ON);

    //x Front Panel LOAD indicator, driven by the CPU
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

    // analog input to measure +3.3V / 2
    // probably won't use this, use ADC3 input instead, which is VSYS / 3
    // However, ADC3 measures VSYS which is a diode drop below +5V so there's some uncertainty
    //adc_init();
    //adc_gpio_init(ADC2);
    //adc_select_input(2);
    //adc_gpio_init(ADC3);
    //adc_select_input(3);

    initialize_pca9557(); // IC on the front panel PCB used to read the state of the device select switches

    clear_cpu_ready_indicator();
    clear_cpu_fault_indicator();
    set_cpu_load_indicator();
}

void initialize_spi()
{
    // initialize SPI to run at 25 MHz
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

    printf(" UART is initialized\r\n");
}

void initialize_fpga(Disk_State* ddisk)
{
    //fpga_ctrl_reg_image = 0;
    //update_drive_address(ddisk);
    load_drive_address(0); // initialize drive address to zero, initially
    set_tester_ready();
    ddisk->Tester_Ready = true;

    clear_rk11de_mode();
    clear_wtprot_mode();

    // read the FPGA version and later confirm whether it's compatible with the software version
    ddisk->FPGA_version = read_fpga_version();
    ddisk->FPGA_minorversion = read_fpga_minorversion();
}

int compute_ram_address(int number_of_sectors, int cylinder, int head, int sector)
{
    int ram_addr;
    //return((cylinder << 14) | (head << 13) | (sector << 9)); // for debugging, ram address calculation like the original ====================
    if(number_of_sectors > 16)
        ram_addr = ((cylinder << 14) | (head << 13) | ((sector & 0x1f) << 8)); //17 to 32 sectors
    else if(number_of_sectors > 8)
        ram_addr = ((cylinder << 14) | (head << 13) | ((sector & 0x0f) << 9)); // 9 to 16 sectors
    else if(number_of_sectors > 4)
        ram_addr = ((cylinder << 14) | (head << 13) | ((sector & 0x07) << 10)); // 5 to 8 sectors
    else if(number_of_sectors > 2)
        ram_addr = ((cylinder << 14) | (head << 13) | ((sector & 0x03) << 11)); // 3 to 4 sectors
    else
        ram_addr = ((cylinder << 14) | (head << 13) | ((sector & 0x01) << 12)); // 2 sectors
    //printf("addr = %7x, number of sectors %d, CHS = %3x %x %2x\r\n", ram_addr, number_of_sectors, cylinder, head, sector);
    return(ram_addr);
}
