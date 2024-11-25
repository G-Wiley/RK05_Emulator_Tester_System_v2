// *********************************************************************************
// emulator_global.h
//   header for emulator global variables for command mode
// *********************************************************************************
// 

#define INPUT_LINE_LENGTH 200
extern char inputdata[INPUT_LINE_LENGTH];
extern char *extract_argv[INPUT_LINE_LENGTH];
extern int extract_argc;

#define LIST_LENGTH 50
extern int list_cylinder[LIST_LENGTH];
extern int list_head[LIST_LENGTH];
extern int list_sector[LIST_LENGTH];
extern int list_count;

// callback code
extern int char_from_callback;

// debug mode
extern int debug_mode;