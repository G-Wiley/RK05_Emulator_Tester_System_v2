// *********************************************************************************
// emulator_hardware.h
//  definitions and functions related to the emulator hardware
//  both GPIO and FPGA
// *********************************************************************************
// 
#define DOOROPEN 0
#define DOORCLOSED 1
#define DOORMOVING 2

#define DRIVE_ADDRESS_BITS_I2C 0x7
#define DRIVE_FIXED_MODE_BIT_I2C 0x8

void initialize_uart();
void initialize_gpio();
void initialize_fpga(Disk_State* ddisk);
void read_rocker_switches(Disk_State* ddisk);
bool read_load_switch();
bool read_wp_switch();
bool is_testmode_selected();
void check_dc_low(Disk_State* ddisk);
//void boot_open_the_door();
bool is_card_present();
void load_drive_address(int dr_addr);
void initialize_spi();

void load_ram_address(int ramaddress);
void storebyte(int bytevalue);
int readbyte();
bool is_it_a_tester();
int read_board_version();

void close_drive_door();
void open_drive_door();
int drive_door_status();

void set_cpu_ready_indicator();
void clear_cpu_ready_indicator();
void set_cpu_fault_indicator();
void clear_cpu_fault_indicator();
void set_cpu_load_indicator();
void clear_cpu_load_indicator();
uint8_t read_reg00();
void led_from_bits(int walking_bit);
void assert_fpga_reset();
void deassert_fpga_reset();
void microSD_LED_on();
void microSD_LED_off();

uint8_t read_write_spi_register(uint8_t reg, uint8_t data);
void toggle_wp();
void set_file_ready();
void clear_file_ready();
void set_fault_latch();
void clear_fault_latch();
void set_dc_low();
void clear_dc_low();
void enable_interface_test_mode();
void assert_outputs(int step_count);
int read_test_inputs();
int read_int_inputs();
void update_fpga_disk_state(Disk_State* ddisk);
