// *********************************************************************************
// disk_state_definitions.h
//  definitions of the disk drive state
// 
// *********************************************************************************
// 
struct Disk_State
{
    char imageName[11];
    char imageDescription[200];
    char imageDate[20];

    char controller[100];
    int bitRate;
    //int preamble1Length;
    //int preamble2Length;
    //int dataLength;
    //int postambleLength;
    int numberOfCylinders;
    int numberOfSectorsPerTrack;
    int numberOfHeads;
    int microsecondsPerSector;

    int Drive_Address;
    bool mode_RK05f;
    bool File_Ready;
    bool Fault_Latch;
    bool dc_low;
    int FPGA_version;
    int FPGA_minorversion;
    int Board_version;

    int run_load_state;
    bool rl_switch;
    bool p_wp_switch, wp_switch;
    bool door_is_open;
    int door_count;

    int debug_vsense;

    //int display_message_timer;
    //int display_invert_timer;
};
