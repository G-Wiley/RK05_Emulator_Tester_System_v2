// *********************************************************************************
// emulator_command.h
//  definitions and functions related to parsing and execution of emulator 
//  Interface Test Mode commands
// 
// *********************************************************************************
// 

void command_parse_and_dispatch (Disk_State* dstate);
int extract_command_fields(char* line_ptr);
void emulator_command_mode(Disk_State* dstate);
