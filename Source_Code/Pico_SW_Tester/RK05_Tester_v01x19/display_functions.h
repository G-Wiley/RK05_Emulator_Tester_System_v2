// *********************************************************************************
// display_functions.h
//   header for display operations
// *********************************************************************************
// 

// FUNCTION PROTOTYPES
void setup_display();
void display_error(char* row1, char* row2);
void display_status(char* row1, char* row2);
void display_rk05_tester();
void display_shutdown();
void display_drive_address(int drv_addr, bool fixed, char *image_name);
void manage_display_timers();
void display_restart_invert_timer();
void display_disable_message_timer();
void initialize_pca9557();
int read_drive_address_switches();
void print_display_state(); //for debugging
