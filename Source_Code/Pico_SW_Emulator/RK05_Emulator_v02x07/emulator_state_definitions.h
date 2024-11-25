// *********************************************************************************
// emulator_state_definitions.h
//  definitions and functions related to the RUN/LOAD emulator state
// 
// *********************************************************************************
// 

// RUN/LOAD state, state definitions
#define RLST0  0x0  // Unloaded, waiting in the unloaded state for the RUN/LOAD switch to be toggled to the “RUN” positioned
#define RLST1  0x1  // The RUN/LOAD switch has been toggled to the “RUN” position. Check to see that the microSD has been inserted. 
                    // If not, then go to load error state with code 1.
#define RLST2  0x2  // Check to see if the file system can be started. If not, then go to load error state with code 2.
//#define RLST3  0x3  // Check to see if the disk image file is present . If not, then go to load error state with code 3.
#define RLST4  0x4  // Check to see if the disk image file can be opened. If not, then go to load error state with code 4.
#define RLST5  0x5  // Read the format identifier in the header of the disk image file. If there's an error, then go to load error state with code 5.
                    // If the header is good then start moving the actuator to close the drive door
#define RLST6  0x6  // Wait for the actuator to finish closing the drive door.
#define RLST7  0x7  // Read the disk image file and write it to the DRAM. If a read error occurs then go to load error state with code 7.
#define RLST8  0x8  // Close the disk image file and set the File_Ready bit in the FPGA mode register and illuminate RDY on the front panel.
#define RLST10 0x10 // Loaded and running state. Waiting for the RUN/LOAD switch to be toggled to the “LOAD” position.
#define RLST11 0x11 // The RUN/LOAD switch has been toggled to the “LOAD” position. Read the contents of the DRAM and write it to the disk image file. 
                    // If a write error occurs then go to the unload error state with code 21.
#define RLST12 0x12 // Write the header of the image file.
#define RLST13 0x13 // Write the disk image data.
#define RLST14 0x14 // Close the disk image file. If an error occurs then go to the unload error state with code 22.
                    // Start moving the actuator to close the drive door
#define RLST15 0x15 // Wait for the actuator to finish opening the drive door.
//#define RLST34 0x34 // Check to see if the config file can be opened.
#define RLST18 0x18 // Loading error state, initialize internal error states for loading error.
#define RLST19 0x19 // Loading error state, indicator on. Flash the Fault light indefinitely.
#define RLST1a 0x1a // Loading error state, indicator off. Flash the Fault light indefinitely.
#define RLST1b 0x1b // wait for door to open after loading error state or loaded/ready RLST10 state.
#define RLST1c 0x1c // Unloading error state, initialize internal error states for unloading error.
#define RLST1d 0x1d // Unloading error state, indicator on. Flash the Fault light indefinitely.
#define RLST1e 0x1e // Unloading error state, indicator off. Flash the Fault light indefinitely.
#define RLST1f 0x1f // wait for door to close after Unloading error state or unloaded RLST0 state.
