// *********************************************************************************
// microsd_file_ops.h
//   header for file operations to read and write headers and RK05 disk image data
// *********************************************************************************
// 
//#include "disk_state_definitions.h"

#include "ff.h" /* Obtains integer types */
//#include "diskio.h" /* Declarations of disk functions */
//#include "sd_card.h"

void display_directory (void);
FRESULT open_and_read_drive_parameters(Disk_State* dstate, char* filename);
FRESULT file_open_read_disk_image_n(char *filename);
FRESULT file_open_write_disk_image_n(char *filename);
FRESULT file_close_disk_image();
int read_image_file_header(Disk_State* dstate);
int write_image_file_header(Disk_State* dstate);
FRESULT read_disk_image_data_tester(Disk_State* dstate);
FRESULT write_disk_image_data(Disk_State* datate);
bool file_init_and_mount();

//#define FILE_OPS_OKAY 0
