// *********************************************************************************
// disk_state_definitions.h
//  definitions of the disk drive state
// 
// *********************************************************************************
// 
#define ImageNameLength 11
#define ImageDescriptionLength 200
#define ImageDateLength 20
#define ControllerNameLength 100

struct Disk_State
{
    char imageName[ImageNameLength];
    char imageDescription[ImageDescriptionLength];
    char imageDate[ImageDateLength];
    char controller[ControllerNameLength];

    int bitRate;
    int preamble1Length;
    int preamble2Length;
    // bit_times_sector_to_start = preamble1Length + preamble2Length;
    int bit_times_data_bits_after_start;
    int postambleLength;

    int numberOfCylinders;
    int numberOfSectorsPerTrack;
    int numberOfHeads;
    int microsecondsPerSector;

    int Drive_Address;
    bool Tester_Ready;
    int FPGA_version;
    int FPGA_minorversion;
    int tester_board_version;
    int drive_board_version;
    bool wtprot;
    bool rk11d;   // determines the state of disk bus signal BUS_RK11D_L and enables encoded drive select signals

    //int tester_state;
    bool rl_switch;
    bool p_wp_switch, wp_switch;
};
