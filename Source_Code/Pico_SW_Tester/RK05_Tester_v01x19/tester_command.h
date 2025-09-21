// *********************************************************************************
// tester_command.h
//  definitions and functions related to parsing and execution of tester commands
// 
// *********************************************************************************
// 

void command_parse_and_dispatch (Disk_State* dstate);
int extract_command_fields(char* line_ptr);
