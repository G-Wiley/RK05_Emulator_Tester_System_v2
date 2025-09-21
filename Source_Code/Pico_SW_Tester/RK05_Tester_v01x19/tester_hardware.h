// *********************************************************************************
// tester_hardware.h
//  definitions and functions related to the tester hardware
//  both CPU GPIO and FPGA
// *********************************************************************************
// 
#define DRIVE_ADDRESS_BITS_I2C 0x7
#define DRIVE_FIXED_MODE_BIT_I2C 0x8

void initialize_uart();
void initialize_gpio();
void initialize_fpga(Disk_State* ddisk);
void read_switches(Disk_State* ddisk);
bool read_load_switch();
bool read_wp_switch();
//void check_dc_low(Disk_State* ddisk);
bool is_card_present();
int read_drive_status1();
int read_drive_status2();
bool is_read_in_progress();
bool is_write_in_progress();
bool is_rws_ready();
bool is_addr_accepted_ready();
void initialize_spi();

void load_ram_address(int ramaddress);
void storebyte(int bytevalue);
int readbyte();
bool is_it_a_tester();
int read_board_version();

void set_cpu_ready_indicator();
void clear_cpu_ready_indicator();
void set_cpu_fault_indicator();
void clear_cpu_fault_indicator();
void set_cpu_load_indicator();
void clear_cpu_load_indicator();
void assert_fpga_reset();
void deassert_fpga_reset();
void assert_bus_restore();
void deassert_bus_restore();
void assert_GP6();
void deassert_GP6();

uint8_t read_write_spi_register(uint8_t reg, uint8_t data);
void load_sector_address(int sect_addr);
void load_drive_address(int dr_addr);
void set_rk11de_mode();
void clear_rk11de_mode();
void set_wtprot_mode();
void clear_wtprot_mode();
void set_tester_ready();
void clear_tester_ready();
void set_on_cyl_indicator();
void clear_on_cyl_indicator();
void set_5bit_support();
void clear_5bit_support();
void microSD_LED_on();
void microSD_LED_off();

void seek_to_cylinder(int cylinder, bool restore);
void select_head(int head);
void write_sector();
void read_sector();
void command_clear();
void enable_interface_test_mode();
void assert_outputs(int step_count, int hw_version);
int read_test_inputs();
void led_from_bits(int walking_bit);
void update_fpga_disk_state(Disk_State* ddisk);
int compute_ram_address(int number_of_sectors, int cylinder, int head, int sector);