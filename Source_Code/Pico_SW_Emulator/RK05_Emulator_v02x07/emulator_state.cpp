// *********************************************************************************
// emulator_state.cpp
//  definitions and functions related to the RUN/LOAD emulator state
// 
// *********************************************************************************
// 
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>

#include "emulator_state_definitions.h"
#include "disk_state_definitions.h"
#include "display_functions.h"
//#include "display_timers.h"
#include "emulator_hardware.h"
#include "microsd_file_ops.h"

#define LOADINGERRORON 7
#define LOADINGERROROFF 7
#define UNLOADINGERRORON 4
#define UNLOADINGERROROFF 4

static int errorlightcount;

void process_run_load_state(Disk_State* dstate){
int intermediate_result;

    switch(dstate->run_load_state){
        case RLST0:
            // Unloaded, waiting in the unloaded state for the RUN/LOAD switch to be toggled to the “RUN” positiond
            microSD_LED_off();
            if(dstate->rl_switch){
                if(dstate->wp_switch){ //if WTPROT switch is simultaneously pressed then only move the microSD carriage
                    close_drive_door();
                    set_file_ready();
                    set_cpu_ready_indicator();
                    dstate->File_Ready = true;
                    clear_cpu_load_indicator();
                    dstate->run_load_state = RLST1f; // go to the 1f state to wait for the door to be closed
                }
                else{ //if WTPROT switch is not simultaneously pressed then begin the normal loading process
                    printf("Switch toggled from LOAD to RUN\r\n");
                    dstate->run_load_state = RLST1; // If the RUN/LOAD switch is toggled to RUN then advance to RLST1
                }
            }
            break;
        case RLST1:
            // The RUN/LOAD switch has been toggled to the “RUN” position. Check to see that the microSD has been inserted. 
            // If not, then go to load error state with code 1.
            microSD_LED_on();
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            if(is_card_present()){
                printf("Card present, microSD card detected\r\n");
                display_status((char *) "microSD", (char *) "detected");
                dstate->run_load_state = RLST2; // if the microSD card is inserted then advance to RLST2
            }
            else {
                //error_code = 1;
                printf("*** ERROR, microSD card is not inserted\r\n");
                display_error((char *) "no microSD", (char *) "inserted");
                dstate->run_load_state = RLST18;
            }
            break;
        case RLST2:
            // Check to see if the file system can be started. If not, then go to load error state with code 2.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            if(file_init_and_mount() != 0) {
                //error_code = 2;
                printf("*** ERROR, could not init and mount microSD filesystem\r\n");
                //display_error((char *) "cannot init", (char *) "microSD card");
                dstate->run_load_state = RLST18;
            }
            else{
                printf("filesystem started\r\n");
                display_status((char *) "filesystem", (char *) "started");
                dstate->run_load_state = RLST4;
            }
            break;
        //case RLST34: // start transitions will skip this state for the moment, maybe this state will be deleted later
            // Check to see if the config file can be opened. If not, then go to load error state with code 0x14.
            //else if((fr = f_open(&fil, configfilename, FA_READ))!= FR_OK){
                //run_load_state = RLST18;
                //error_code = 0x14;
                //printf("*** ERROR, could not open file (%d)\r\n", fr);
            //}
            //else{
                //printf("Config file is open\r\n");
                //display_status("config file", "is open");
                //run_load_state = RLST3;
            //}
            //break;
        //case RLST3:
            // Check to see if the disk image file is present in the config file. 
            // If not, then go to load error state with code 3.
            //break;
        case RLST4:
            // Check to see if the disk image file can be opened. If not, then go to load error state with code 4.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            if(file_open_read_disk_image() != 0){
                //error_code = 0x4;
                printf("*** ERROR, file_open_read_disk_image failed\r\n");
                display_error((char *) "cannot open", (char *) "disk image");
                dstate->run_load_state = RLST18;
            }
            else{
                printf("Disk image file is open\r\n");
                display_status((char *) "image file", (char *) "is open");
                dstate->run_load_state = RLST5;
            }
            break;
        case RLST5:
            // Read the format identifier in the header of the disk image file. If there's an error, then go to load error state with code 5.
            // If the header is good then start moving the actuator to close the drive door
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            printf("Reading image file header\r\n");
            intermediate_result = read_image_file_header(dstate);
            if(intermediate_result != 0){
                file_close_disk_image();
                switch(intermediate_result) {
                    default:
                        printf("*** ERROR, problem reading image file header\r\n");
                        display_error((char *) "cannot read", (char *) "image header");
                        break;

                    case 2:
                        printf("*** ERROR, invalid file type\r\n");
                        display_error((char *) "invalid", (char *) "file type");
                        break;

                    case 3:
                        printf("*** ERROR, invalid file version\r\n");
                        display_error((char *) "invalid", (char *) "file ver");
                        break;
                }
                dstate->run_load_state = RLST18;
            }
            else if((dstate->numberOfSectorsPerTrack > 16) && (dstate->Board_version < 2)){
                printf("*** ERROR, Board Version %d cannot support %d sectors.\r\n", dstate->Board_version, dstate->numberOfSectorsPerTrack);
                display_error((char *) "> max", (char *) "sectors");
                dstate->run_load_state = RLST18;
            }
            else{
                printf("Image file header read successfully\r\n");
                clear_cpu_load_indicator();
                close_drive_door();
                printf("Moving the actuator to close the door\r\n");
                display_status((char *) "Closing", (char *) "microSD door");
                dstate->run_load_state = RLST6;
            }
            break;
        case RLST6:
            // Wait for the actuator to finish closing the drive door.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = drive_door_status();
            if(intermediate_result == DOORCLOSED){
                printf("Door closed\r\nReading disk image data from file\r\n");
                display_status((char *) "Reading", (char *) "image data");
                dstate->run_load_state = RLST7;
            }
            break;
        case RLST7:
            // Read the disk image file and write it to the DRAM. If a read error occurs then go to load error state with code 7.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = read_disk_image_data(dstate);
            if(intermediate_result != 0){
                file_close_disk_image();
                printf("*** ERROR, problem reading disk image data\n");
                display_error((char *) "cannot read", (char *) "image data");
                dstate->run_load_state = RLST18;
            }
            else{
                printf("Disk image data read successfully\r\n");
                display_status((char *) "Image data", (char *) "read OK");
                dstate->run_load_state = RLST8;
            }
            break;
        case RLST8:
            // Close the disk image file and set the File_Ready bit in the FPGA mode register and illuminate RDY on the front panel.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = file_close_disk_image();
            if(intermediate_result != 0){
                printf("*** ERROR, problem closing disk image data file\n");
                display_error((char *) "cannot close", (char *) "image file");
                dstate->run_load_state = RLST18;
            }
            else{
                printf("Disk image data read, file closed successfully\r\n");
                set_cpu_ready_indicator();
                set_file_ready();
                dstate->File_Ready = true;
                dstate->run_load_state = RLST10;
            }
            break;
        case RLST10:
            // Loaded and running state. Waiting for the RUN/LOAD switch to be toggled to the “LOAD” position.
            microSD_LED_on();
            if(dstate->rl_switch == 0){ //if WTPROT switch is simultaneously pressed then only move the microSD carriage
                if(dstate->wp_switch){
                    open_drive_door();
                    clear_file_ready();
                    clear_cpu_ready_indicator();
                    dstate->File_Ready = false;
                    set_cpu_load_indicator();
                    dstate->run_load_state = RLST1b; // go to the 1b state to wait for the door to be open
                }
                else{ //if WTPROT switch is not simultaneously pressed then begin the normal unloading process
                    printf("Switch toggled from RUN to LOAD\r\n");
                    dstate->run_load_state = RLST11; // If the RUN/LOAD switch is toggled to LOAD then advance to RLST11
                }
            }
            break;
        case RLST11:
            // The RUN/LOAD switch has been toggled to the “LOAD” position. Read the contents of the DRAM and write it to the disk image file. 
            // If a write error occurs then go to the unload error state with code 21.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = file_open_write_disk_image();
            printf("finished file open for write, code %d\r\n", intermediate_result);
            if(intermediate_result != FILE_OPS_OKAY){
                //error_code = 0x21;
                printf("*** ERROR, file_open_write_disk_image failed\r\n");
                display_error((char *) "image file", (char *) "open failed");
                dstate->run_load_state = RLST1a;
            }
            else{
                printf("Disk image file is open\r\n");
                clear_cpu_ready_indicator();
                clear_file_ready();
                dstate->File_Ready = false;
                display_status((char *) "Image file", (char *) "open");
                dstate->run_load_state = RLST12;
            }
            break;
        case RLST12:
            // Write the header of the image file.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = write_image_file_header(dstate);
            if(intermediate_result != FILE_OPS_OKAY){
                file_close_disk_image();
                printf("*** ERROR, write_image_file_header failed\r\n");
                display_error((char *) "image header", (char *) "write fail");
                dstate->run_load_state = RLST1a;
            }
            else{
                printf("Disk image header written\r\n");
                display_status((char *) "Writing", (char *) "image data");
                dstate->run_load_state = RLST13;
            }
            break;
        case RLST13:
            // Write the disk image data.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = write_disk_image_data(dstate);
            if(intermediate_result != FILE_OPS_OKAY){
                file_close_disk_image();
                printf("*** ERROR, write_disk_image_data failed\r\n");
                display_error((char *) "image data", (char *) "write fail");
                dstate->run_load_state = RLST1a;
            }
            else{
                printf("Disk image data written\r\n");
                //display_status((char *) "image file", (char *) "is open");
                dstate->run_load_state = RLST14;
            }
            break;
        case RLST14:
            // Close the disk image file. If an error occurs then go to the unload error state with code 22.
            // Start moving the actuator to close the drive door
            // Close the disk image file and set the File_Ready bit in the FPGA mode register and illuminate RDY on the front panel.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = file_close_disk_image();
            if(intermediate_result != 0){
                printf("*** ERROR, problem closing disk image data file\n");
                display_error((char *) "image file", (char *) "close fail");
                dstate->run_load_state = RLST1a;
            }
            else{
                printf("Disk image data write, file closed successfully\r\n");
                display_status((char *) "Opening", (char *) "microSD door");
                open_drive_door();
                printf("Moving the actuator to open the door\r\n");
                dstate->run_load_state = RLST15;
            }
            break;
        case RLST15:
            // Wait for the actuator to finish opening the drive door.
            microSD_LED_off();
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            intermediate_result = drive_door_status();
            printf("state RLST15 drive door [%d]\r\n", intermediate_result);
            if(intermediate_result == DOOROPEN){
                printf("Door open\r\n");
                display_status((char *) "microSD", (char *) "door open");
                set_cpu_load_indicator();
                dstate->run_load_state = RLST0;
            }
            break;
        case RLST18:
            // Loading error state, initialize internal error states for loading error.
            microSD_LED_off();
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            set_cpu_fault_indicator();
            errorlightcount = LOADINGERRORON;
            dstate->run_load_state = RLST19;
            break;
        case RLST19:
            // Loading error state, indicator on. Flash the Fault light indefinitely.
            if(--errorlightcount == 0){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                errorlightcount = LOADINGERROROFF;
                dstate->run_load_state = RLST1a;
                clear_cpu_fault_indicator();
            }
            if(!dstate->rl_switch && dstate->wp_switch){ //if WTPROT switch is simultaneously pressed when toggling RUN/LOAD back to LOAD then only move the microSD carriage
                open_drive_door();
                clear_cpu_fault_indicator();
                dstate->run_load_state = RLST1b; // go to the 1b state to wait for the door to be opened
                clear_file_ready();
                clear_cpu_ready_indicator();
                set_cpu_load_indicator();
            }
            break;
        case RLST1a:
            // Loading error state, indicator off. Flash the Fault light indefinitely.
            if(--errorlightcount == 0){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                errorlightcount = LOADINGERRORON;
                dstate->run_load_state = RLST19;
                set_cpu_fault_indicator();
            }
            if(!dstate->rl_switch && dstate->wp_switch){ //if WTPROT switch is simultaneously pressed when toggling RUN/LOAD back to LOAD then only move the microSD carriage
                open_drive_door();
                clear_cpu_fault_indicator();
                clear_file_ready();
                clear_cpu_ready_indicator();
                set_cpu_load_indicator();
                dstate->run_load_state = RLST1b; // go to the 1b state to wait for the door to be opened
            }
            break;
        case RLST1b:
            // wait for door to open after loading error state or loaded/ready RLST10 state.
            if(drive_door_status() == DOOROPEN){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                dstate->run_load_state = RLST0;
            }
            break;
        case RLST1c:
            // Unloading error state, initialize internal error states for unloading error.
            printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
            set_cpu_fault_indicator();
            errorlightcount = LOADINGERRORON;
            dstate->run_load_state = RLST1d;
            break;
        case RLST1d:
            // Unloading error state, indicator on. Flash the Fault light indefinitely.
            if(--errorlightcount == 0){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                errorlightcount = UNLOADINGERROROFF;
                dstate->run_load_state = RLST1e;
                clear_cpu_fault_indicator();
            }
            if(dstate->rl_switch && dstate->wp_switch){ //if WTPROT switch is simultaneously pressed when toggling RUN/LOAD back to RUN then only move the microSD carriage
                close_drive_door();
                clear_cpu_fault_indicator();
                set_file_ready();
                set_cpu_ready_indicator();
                clear_cpu_load_indicator();
                dstate->run_load_state = RLST1f; // go to the 1f state to wait for the door to be closed
            }
            break;
        case RLST1e:
            // Unloading error state, indicator off. Flash the Fault light indefinitely.
            if(--errorlightcount == 0){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                errorlightcount = UNLOADINGERRORON;
                dstate->run_load_state = RLST1d;
                set_cpu_fault_indicator();
            }
            if(dstate->rl_switch && dstate->wp_switch){ //if WTPROT switch is simultaneously pressed when toggling RUN/LOAD back to RUN then only move the microSD carriage
                close_drive_door();
                clear_cpu_fault_indicator();
                set_file_ready();
                set_cpu_ready_indicator();
                clear_cpu_load_indicator();
                dstate->run_load_state = RLST1f; // go to the 1f state to wait for the door to be closed
            }
            break;
        case RLST1f:
            // wait for door to close after Unloading error state or unloaded RLST0 state.
            if(drive_door_status() == DOORCLOSED){
                printf("  Drive_Address = %d, RLST%x, %d, %d\r\n", dstate->Drive_Address, dstate->run_load_state, dstate->rl_switch, dstate->wp_switch);
                dstate->run_load_state = RLST10;
            }
            break;
        default:
            printf("*** ERROR, invalid run_load_state: %x\n", dstate->run_load_state);
    }
}
