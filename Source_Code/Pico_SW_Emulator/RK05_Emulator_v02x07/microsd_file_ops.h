// *********************************************************************************
// microsd_file_ops.h
//   header for file operations to read and write headers and RK05 disk image data
// *********************************************************************************
// 
//#include "disk_state_definitions.h"

int file_open_read_disk_image();
int file_open_write_disk_image();
int file_close_disk_image();
int read_image_file_header(Disk_State* dstate);
int write_image_file_header(Disk_State* dstate);
int read_disk_image_data(Disk_State* dstate);
int write_disk_image_data(Disk_State* datate);
int file_init_and_mount();

#define FILE_OPS_OKAY 0
