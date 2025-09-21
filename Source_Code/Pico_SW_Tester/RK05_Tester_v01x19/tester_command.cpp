// *********************************************************************************
// tester-command.cpp
//  definitions and functions related to the RUN/LOAD emulator state
// 
// *********************************************************************************
// 
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>

#include "disk_state_definitions.h"
#include "display_functions.h"
//#include "display_timers.h"
#include "tester_hardware.h"
#include "microsd_file_ops.h"

#include "tester_global.h"

#define SEEK_TIMEOUT 1000
#define ADDR_ACCEPTED_TIMEOUT 10
#define READWRITE_TIMEOUT 1000

#define SAVE_BUFFER_SIZE 4000 // size for 2-sector disks calculated by "variable sectors flexible addressing" spreadsheet
        // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 4 sector disks
        //  for 4 sectors, one revolution is 40 msec, 40 ms / 4 = 10 ms, 10 ms * 1.6 Mbps / 8 bits/byte = 2000 bytes

int min(int a, int b){
    if(a < b)
        return(a);
    else
        return(b);
}

// shift_prbs16 function shifts a 16-order PRBS register by one state
void shift_prbs16(int* prbs){
    int counter; // a counter is a fast and easy way to calculate the parity of the taps
    counter = 0;
    // x^16 + x^14 + x^13 + x^11 + 1
    if((*prbs & 0x1) != 0) counter++;
    if((*prbs & 0x4) != 0) counter++;
    if((*prbs & 0x8) != 0) counter++;
    if((*prbs & 0x20) != 0) counter++;
    *prbs = *prbs >> 1;
    // inverted feedback, if parity of the taps is even then shift a 1 into the MSB
    if((counter & 1) == 0) *prbs = *prbs | 0x8000;
}

// shift_prbs24 function shifts a 24-order PRBS register by one state
// not used, available for future expansion of tests
void shift_prbs24(int* prbs){
    int counter; // a counter is a fast and easy way to calculate the parity of the taps
    counter = 0;
    // x^24 + x^23 + x^22 + x^4 + 1
    if((*prbs & 0x1) != 0) counter++;
    if((*prbs & 0x10) != 0) counter++;
    if((*prbs & 0x040000) != 0) counter++;
    if((*prbs & 0x080000) != 0) counter++;
    *prbs = *prbs >> 1;
    // inverted feedback, if parity of the taps is even then shift a 1 into the MSB
    if((counter & 1) == 0) *prbs = *prbs | 0x800000;
}

// shift_prbs31 function shifts a 31-order PRBS register by one state
void shift_prbs31(int* prbs){
    int counter; // a counter is a fast and easy way to calculate the parity of the taps
    counter = 0;
    //Pederson book: 7 21042104211E
    // x^31 + x^27 + x^23 + x^19 + x^15 + x^11 + x^7 + x^3 + 1
    if((*prbs & 0x1) != 0) counter++;
    if((*prbs & 0x8) != 0) counter++;
    if((*prbs & 0x80) != 0) counter++;
    if((*prbs & 0x800) != 0) counter++;
    if((*prbs & 0x8000) != 0) counter++;
    if((*prbs & 0x80000) != 0) counter++;
    if((*prbs & 0x800000) != 0) counter++;
    if((*prbs & 0x8000000) != 0) counter++;
    *prbs = *prbs >> 1;
    // inverted feedback, if parity of the taps is even then shift a 1 into the MSB
    if((counter & 1) == 0) *prbs = *prbs | 0x40000000;
}

void char_to_uppercase(char* cpointer){
    if((*cpointer >= 'a') && (*cpointer <= 'z')) // convert to uppercase
        *cpointer = *cpointer - 'a' + 'A';
}

bool check_drive_ready(){
    bool retval = true;
    int tempval = read_drive_status1();
    if((tempval & 0x10) == 0){
        printf("    BUS_FILE_READY_L is not asserted\r\n");
        retval = false;
    }
    if((tempval & 0x08) == 0){
        printf("    BUS_RK05_HIGH_DENSITY_L is not asserted\r\n");
        retval = false;
    }
    if((tempval & 0x04) != 0){
        printf("    BUS_DC_LO_L is active\r\n");
        retval = false;
    }

    tempval = read_drive_status2();
    if((tempval & 0x01) == 0){
        printf("    BUS_RWS_RDY_L is not asserted\r\n");
        retval = false;
    }
    return(retval);
}

int extract_command_fields(char* line_ptr) {
    char *scan_ptr;
    int extract_state;
    bool prior_chars;

    scan_ptr = line_ptr;
    extract_argc = 0;
    extract_state = 0;
    prior_chars = false;
    while(true){
        switch(extract_state){
            case 0: // pass through white-space prior to field
                if((*scan_ptr == ' ') || (*scan_ptr == '\t')){
                    scan_ptr++;
                }
                else if((*scan_ptr == '\r') || (*scan_ptr == '\n') || (*scan_ptr == '\0')){
                    *scan_ptr = '\0';
                    return(extract_argc);
                }
                else{
                    extract_argv[extract_argc] = scan_ptr;
                    extract_argc++;
                    extract_state = 1;
                    char_to_uppercase(scan_ptr);
                    scan_ptr++;
                }
                break;
            case 1:
                if((*scan_ptr == ' ') || (*scan_ptr == '\t')){
                    *scan_ptr = '\0';
                    scan_ptr++;
                    extract_state = 0;
                }
                else if((*scan_ptr == '\r') || (*scan_ptr == '\n') || (*scan_ptr == '\0')){
                    *scan_ptr = '\0';
                    return(extract_argc);
                }
                else{
                    char_to_uppercase(scan_ptr);
                    scan_ptr++;
                }
                break;
            default:
                printf("### ERROR, invalid state in extract_command_fields()\r\n");
                return(extract_argc);
                break;
        }
        if(scan_ptr >= (line_ptr + INPUT_LINE_LENGTH)){ // for protection, should never be true
            printf("### ERROR, extract_command_fields() pointer past end of line buffer\r\n");
            return(extract_argc);
        }
    }
}


#define TOGGLE_TIMEOUT 20
#define CYCLE_TIMEOUT 320

void scan_inputs_test(Disk_State* dstate){
    int toggle_timer;
    int step_count = 0;
    int ipin, p_ipin;
    int ref;
    int error_count, sample_count;
    int readval;

    int  min_version = min(dstate->drive_board_version, dstate->FPGA_version);

    printf("  Scan Bus Inputs test. Hit any key to stop the test loop.\r\n");

    // BUS_FILE_READY_L is the clock to sample the other signals. Confirm that it's toggling
    ipin = ((read_test_inputs() & 0x80000) == 0) ? 0 : 1;
    p_ipin = ipin;
    printf("ipin=%d, p_ipin=%d\r\n", ipin, p_ipin);
    for(toggle_timer = 0; toggle_timer < TOGGLE_TIMEOUT; toggle_timer++){ // change in BUS_SEL_DR_3_L
        p_ipin = ipin;
        ipin = ((readval=(read_test_inputs()) & 0x80000) == 0) ? 0 : 1;
        //printf("L1, p_ipin = %d, ipin = %d, toggle_timer = %d, readval = %x\r\n", p_ipin, ipin, toggle_timer, readval); // for debugging
        if(ipin != p_ipin)
            break;
        sleep_ms(1); 
    }
    printf("ipin=%d, p_ipin=%d\r\n", ipin, p_ipin);
    printf("  toggle_timer = %d\r\n", toggle_timer);
    if(toggle_timer >= TOGGLE_TIMEOUT){ // if BUS_FILE_READY_L not toggling then print error and abort
        if(ipin == 1)
            printf("### ERROR, \"BUS_FILE_READY_L\" is stuck high\r\n");
        else
            printf("### ERROR, \"BUS_FILE_READY_L\" is stuck low\r\n");
        return;
    }
    printf("  signal \"BUS_FILE_READY_L\" is toggling\r\n");

    // BUS_SEC_CNTR_0_L resets the sequence to verify the other signals. Confirm that it's toggling
    ipin = ((read_test_inputs() & 0x00001) == 0) ? 0 : 1;
    p_ipin = ipin;
    for(toggle_timer = 0; toggle_timer < CYCLE_TIMEOUT; toggle_timer++){ // change in BUS_SEC_CNTR_0_L
        p_ipin = ipin;
        ipin = (((readval=read_test_inputs()) & 0x00001) == 0) ? 0 : 1;
        //printf("L2, p_ipin = %d, ipin = %d, toggle_timer = %d, readval = %x\r\n", p_ipin, ipin, toggle_timer, readval); // for debugging
        if((ipin == 0) && (p_ipin == 1)){
            step_count = 0;
            break;
        }
        sleep_ms(1); 
    }
    if(toggle_timer >= CYCLE_TIMEOUT){ // if BUS_SEC_CNTR_0_L not toggling then print error and abort
        if(ipin == 1)
            printf("### ERROR, \"BUS_SEC_CNTR_0_L\" is stuck high\r\n");
        else
            printf("### ERROR, \"BUS_SEC_CNTR_0_L\" is stuck low\r\n");
        return;
    }
    printf("  signal \"BUS_SEC_CNTR_0_L\" is toggling\r\n");

    // loop until a key is pressed
    error_count = sample_count = 0;
    ipin = 1;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  error_count = %d, sample_count = %d, step_count = %d\r\n", error_count, sample_count, step_count);
            // if the key was P or p then only print the current progress test result but don't quit
            if((char_from_callback != 'P') && (char_from_callback != 'p')){
                printf("  Ending the Scan Inputs Test\r\n");
                char_from_callback = 0; //reset the value
                return;
            }
            char_from_callback = 0; //reset the value
        }

        p_ipin = ipin;
        ipin = ((read_test_inputs() & 0x80000) == 0) ? 0 : 1;
        if(ipin != p_ipin){
            step_count++;
            sleep_ms(1);
            switch (min_version){
                case 0:
                    if(step_count >= 16)
                        step_count = 0;
                    ref = ((~(1 << step_count)) & 0x0ffff);
                    readval = read_test_inputs() & 0x0ffff;
                    break;
                case 1:
                    if(step_count >= 20)
                        step_count = 0;
                    ref = ((~(1 << step_count)) & 0x7ffff) | ((1 & ~step_count) << 19);
                    readval = read_test_inputs() & 0xfffff;
                    break;
                case 2:
                    if(step_count >= 22)
                        step_count = 0;
                    ref = ((~(1 << step_count)) & 0x17ffff) | ((1 & ~step_count) << 19);
                    readval = read_test_inputs() & 0x1fffff;
                    break;
                default:
                    printf("### ERROR, invalid dstate->drive_board_version, %d\r\n", dstate->drive_board_version);
                    return;
            }
            sample_count++;
            if(readval != ref){
                error_count++;
                printf("### ERROR, step = %d, ref = %x, read = %x,   sample_count = %d\r\n", step_count, ref, readval, sample_count);
            }
        }
    }
}

void scan_outputs_test(Disk_State* dstate){
    int  min_version = min(dstate->drive_board_version,dstate->FPGA_version);

    printf("  Scan Bus Outputs test. Hit any key to stop the test loop.\r\n");
    // loop until a key is pressed
    int step_count = 0;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  Ending the Scan Outputs Test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }
        if(min_version > 0){
            if(step_count == 14)
                assert_bus_restore();
            else
                deassert_bus_restore();
        }
        assert_outputs(step_count, min_version);
        step_count++;
        //if(step_count > 16)
        switch (min_version){
            case 0:
                if(step_count >= 17)
                    step_count = 0;
                break;
            case 1:
                if(step_count >= 20)
                    step_count = 0;
                break;
            case 2:
                if(step_count >= 22)
                    step_count = 0;
                break;
            default:
                printf("## ERROR, invalid dstate->drive_board_version, %d\r\n", dstate->drive_board_version);
                return;
        }
        sleep_ms(10);
    }
}

void addr_switch_test(){
    printf("  Address Switch Test. Hit any key to stop the test loop.\r\n");
    // loop until a key is pressed
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  Ending the Door Test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }
        int switches = read_drive_address_switches();
        printf("  switches = %x  %d %d %d %d\r\n", switches,
            (switches >> 3) & 1, (switches >> 2) & 1, (switches >> 1) & 1, switches & 1);
        sleep_ms(300);
    }
}

void rocker_switch_test(){
    printf("  Rocker Switch Test. Hit any key to stop the test loop.\r\n");
    // loop until a key is pressed
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  Ending the Door Test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }
        int runload = read_load_switch() ? 1 : 0;
        int wtprot = read_wp_switch() ? 1 : 0;
        printf("  RUN/LOAD = %d,   WTPROT = %d\r\n", runload, wtprot);
        sleep_ms(300);
    }
}

void led_test(){
    printf("  Front Panel LED Test. Hit any key to stop the test loop.\r\n");
    // loop until a key is pressed
    int walking_bit = 1;
    int led_count = 0;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  Ending the LED Test\r\n");
            char_from_callback = 0; //reset the value
            // all LEDs off
            led_from_bits(0);
            clear_cpu_load_indicator();
            clear_cpu_fault_indicator();
            clear_cpu_ready_indicator();
            microSD_LED_off();
            return;
        }
        //led_from_bits(walking_bit);
        led_from_bits(1 << led_count);
        if(led_count == 2)
            set_cpu_load_indicator();
        else if(led_count == 4)
            set_cpu_fault_indicator();
        else if(led_count == 6)
            set_cpu_ready_indicator();
        else if(led_count == 7){
            clear_cpu_ready_indicator();
            microSD_LED_on();
        }
        else{
            // none of the GPIO-controlled LEDs are consecutive, so clear them all here
            clear_cpu_load_indicator();
            clear_cpu_fault_indicator();
            clear_cpu_ready_indicator();
            microSD_LED_off();
        }
        led_count++;
        //walking_bit = walking_bit << 1;
        if(led_count > 7){
            led_count = 0;
            //walking_bit = 1;
        }
        sleep_ms(500);
    }
}

void address_loop_test(Disk_State* dstate){
    int drive_address;

    printf("  Address Loop test. Hit any key to stop the loop.\r\n");
    drive_address = 0;
    while(true){
        load_drive_address(drive_address++);
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("Ending the Address Loop test\r\n");
            char_from_callback = 0; //reset the value
            load_drive_address(dstate->Drive_Address); // restore the drive address to the original value when the loop started
            return;
        }
        if(dstate->rk11d){
            if(drive_address>=8)
                drive_address = 0;
        }
        else{
            if(drive_address>=4)
                drive_address = 0;
        }
        sleep_ms(10); // delay for 10 msec between each drive selection
    }
}

void makelist_cyl(){
    int i;
    int tempval;

    printf("  Make Cylinder list. Enter cylinder data\r\n");
    list_count = 0;
    while(true){
        printf("  cyl_list>");
        i = 0;
        do {
            inputdata[i] = getchar() & 0x7f;
            //printf("{%x %d}",inputdata[i], i);
            switch(inputdata[i]){
                case '\b': //backspace
                case 0x7f: //delete
                    if(i > 0) printf("%c", inputdata[i--]);
                    break;
                case '\r': //return/enter
                    inputdata[i] = '\0';
                    break;
                default:
                    printf("%c", inputdata[i++]);
                    inputdata[i] = 0x7f;
                    break;
            }
        } while ((inputdata[i] != '\0') && (i < INPUT_LINE_LENGTH));
        //printf("\r\n  %d chr, [%s], ", i, inputdata);
        printf("\r\n");
        extract_command_fields(inputdata);
        //printf("  extract_argc = %d\r\n", extract_argc); // debug/test code to view the extracted input line

        if(extract_argc == 1){
            if(strcmp((char *) "X", extract_argv[0])==0){
                printf("List End, %d items\r\n", list_count);
                return;
            }
            else if(strcmp((char *) "R", extract_argv[0])==0){
                list_cylinder[list_count++] = 256; // if R for restore then set bit 8 of the Cylinder value
            }
            else{
                sscanf(extract_argv[0], "%d", &tempval);
                list_cylinder[list_count] = tempval;
                list_head[list_count] = 0;
                list_sector[list_count++] = 0;
            }
        }
        if(list_count >= LIST_LENGTH){
            printf("### Maximum List size reached, %d\r\n", list_count);
            return;
        }
    }
}

void makelist_chs(){
    int i;
    int tempval;

    printf("  Make Cylinder Head Sector list. Enter Cylinder Head Sector parameters\r\n");
    list_count = 0;
    while(true){
        printf("  chs_list>");
        i = 0;
        do {
            inputdata[i] = getchar() & 0x7f;
            //printf("{%x %d}",inputdata[i], i);
            switch(inputdata[i]){
                case '\b': //backspace
                case 0x7f: //delete
                    if(i > 0) printf("%c", inputdata[i--]);
                    break;
                case '\r': //return/enter
                    inputdata[i] = '\0';
                    break;
                default:
                    printf("%c", inputdata[i++]);
                    inputdata[i] = 0x7f;
                    break;
            }
        } while ((inputdata[i] != '\0') && (i < INPUT_LINE_LENGTH));
        //printf("\r\n  %d chr, [%s], ", i, inputdata);
        printf("\r\n");
        extract_command_fields(inputdata);
        //printf("  extract_argc = %d\r\n", extract_argc); // debug/test code to view the extracted input line

        if(strcmp((char *) "X", extract_argv[0])==0){
            if(extract_argc == 1){
                printf("List End, %d items\r\n", list_count);
                return;
            }
            else
                printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        }
        else if(strcmp((char *) "R", extract_argv[0])==0){
            if(extract_argc == 1){
                list_cylinder[list_count++] = 256; // if R for restore then set bit 8 of the Cylinder value
            }
        }
        else{
            if(extract_argc == 3){
                sscanf(extract_argv[0], "%d", &tempval);
                list_cylinder[list_count] = tempval;
                sscanf(extract_argv[1], "%d", &tempval);
                list_head[list_count] = tempval;
                sscanf(extract_argv[2], "%d", &tempval);
                list_sector[list_count++] = tempval;
            }
        }
        if(list_count >= LIST_LENGTH){
            printf("### Maximum List size reached, %d\r\n", list_count);
            return;
        }
    }
}

void display_cyl(){
    int i;

    printf("  Cylinder List\r\n");
    printf("   #  cyl\r\n");
    for(i=0; i < list_count; i++){
        printf("  %2d, %3d\r\n", i, list_cylinder[i]);
    }
}

void display_chs(){
    int i;

    printf("  Cylinder Head Sector List\r\n");
    printf("   #  cyl  h  sect\r\n");
    for(i=0; i < list_count; i++){
        printf("  %2d, %3d, %1d, %2d\r\n", i, list_cylinder[i], list_head[i], list_sector[i]);
    }
}

void display_status(Disk_State* dstate){
    int tempval;
    printf("  Drive Address =   %d\r\n", dstate->Drive_Address);
    printf("  Addr Encoded (RK11D mode) = %d\r\n", dstate->rk11d);
    printf("  controller =      %s\r\n", dstate->controller);
    printf("  bitRate =         %d\r\n", dstate->bitRate);
    printf("  preamble1Length = %d\r\n", dstate->preamble1Length);
    printf("  preamble2Length = %d\r\n", dstate->preamble2Length);
    printf("     total bit_times_sector_to_start = %d\r\n", dstate->preamble1Length + dstate->preamble2Length);
    printf("  bit_times_data_bits_after_start =    %d\r\n", dstate->bit_times_data_bits_after_start);
    printf("  postambleLength = %d\r\n", dstate->postambleLength);
    printf("  numberOfCylinders =        %d\r\n", dstate->numberOfCylinders);
    printf("  numberOfSectorsPerTrack =  %d\r\n", dstate->numberOfSectorsPerTrack);
    printf("  numberOfHeads =            %d\r\n", dstate->numberOfHeads);
    printf("  microsecondsPerSector =    %d\r\n", dstate->microsecondsPerSector);
    printf("  tester board version =     %d\r\n", dstate->tester_board_version);
    printf("  emulator board version =   %d\r\n", dstate->drive_board_version);
    tempval = read_drive_status1();
    printf("\r\n  Drive Status 1 = 0x%x\r\n", tempval);
    if((tempval & 0x80) != 0)
        printf("    BUS_ADDRESS_INVALID_L is active\r\n");
    //if((tempval & 0x40) == 0)
        //printf("    BUS_ADDRESS_ACCEPTED_L is not asserted\r\n");
    if((tempval & 0x20) != 0)
        printf("    BUS_WT_CHK_L is active\r\n");
    if((tempval & 0x10) == 0)
        printf("    BUS_FILE_READY_L is not asserted\r\n");
    if((tempval & 0x08) == 0)
        printf("    BUS_RK05_HIGH_DENSITY_L is not asserted\r\n");
    if((tempval & 0x04) != 0)
        printf("    BUS_DC_LO_L is active\r\n");
    if((tempval & 0x02) != 0)
        printf("    BUS_SEEK_INCOMPLETE_L is active\r\n");
    if((tempval & 0x01) != 0)
        printf("    BUS_WT_PROT_STATUS_L is active\r\n");

    tempval = read_drive_status2();
    printf("\r\n  Drive Status 2 = 0x%x\r\n", tempval);
    if((tempval & 0x80) != 0)
        printf("    Write Command is in Progress, waiting for write operation to finish\r\n");
    if((tempval & 0x40) != 0)
        printf("    Read Command is in Progress, waiting for read operation to finish\r\n");
    if((tempval & 0x01) == 0)
        printf("    BUS_RWS_RDY_L is not asserted\r\n");
}

void seek_loop(bool perform_rws_check){
    int list_item;
    int seek_time; // in increments of 10 msec
    bool restore_state;

    printf("  Seek Loop test. Hit any key to stop the seek loop.\r\n");
    if(perform_rws_check && check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    while(true){
        for(list_item = 0; list_item < list_count; list_item++){
            // a test for keyboard key hit to abort the loop
            // callback code
            if(char_from_callback != 0){
                printf("Ending the Seek Loop test\r\n");
                char_from_callback = 0; //reset the value
                return;
            }

            if(perform_rws_check){
                sleep_ms(1); // simple method to delay until BUS_RWS_RDY_L is ready to be sampled
                for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){
                    //if(is_rws_ready())
                        //break;
                    sleep_ms(1); 
                }
                //printf("seek_time=%d\r\n", seek_time);
                if(seek_time >= SEEK_TIMEOUT){
                    printf("### ERROR, seek time exceeded in seek_loop\r\n");
                    command_clear(); // clear the seek command which possibly didn't execute
                    return;
                }
            }
            else{
                sleep_ms(300); // delay for 300 msec between seeks when we don't wait for RWS Ready
                // measured seek time for 200 cylinder movement is 124 msec, so 300 msec is a sufficiently long delay
            }
            restore_state = ((list_cylinder[list_item] & 0x100) != 0) ? true : false;
            seek_to_cylinder(list_cylinder[list_item], restore_state);
        }
        //sleep_ms(100);
        //printf("cfc=%x\r\n", char_from_callback);
    }
}

void seek_step(Disk_State* dstate){
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int current_cylinder;
    int seek_increment;

    printf("  Seek Step Loop test. Hit any key to stop the seek step loop.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    current_cylinder = 0;
    seek_increment = 1;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf(" Ending the Seek Step test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }

        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in seek_step\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            //if(is_rws_ready())
                //break;
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in seek_step\r\n");
            command_clear();
            return;
        }
        seek_to_cylinder(current_cylinder, false); // perform the seek operation

        current_cylinder += seek_increment; //increment or decrement to next cylinder
        printf("current_cylinder=%d\r\n", current_cylinder);

        // if the step to the next cylinder was below zero then reverse the direction and set next cylinder to 1
        if(current_cylinder < 0){
            seek_increment = 1;
            current_cylinder = 1;
        }
        // if the step to the next cylinder was above the max cylinder then reverse the direction and set next cylinder to 
        // number of cylinders minus 2, which is the same as max cylinder minus 1
        else if(current_cylinder >= dstate->numberOfCylinders){
            seek_increment = -1;
            current_cylinder = dstate->numberOfCylinders - 2;
        }
        // if it's not an end case then just use the computed value above: current_cylinder += seek_increment;
    }

}

void read_loop(Disk_State* dstate){
    int list_item;
    bool restore_state;
    int cylinder, head, sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 4 sector disks

    printf("  Read Loop test. Hit any key to stop the loop.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf(" Ending the Read Loop test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }

        for(list_item = 0; list_item < list_count; list_item++){
            printf("    list_item = %d\r\n", list_item);

            // ============================================================
            // cylinder/head/sector from the chs list
            cylinder = list_cylinder[list_item];
            head = list_head[list_item];
            sector = list_sector[list_item];

            select_head(head);
            load_sector_address(sector);
            ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
            load_ram_address(ramaddress);

            // Read the cylinder/head/sector from disk (disk to tester DRAM)
            restore_state = ((cylinder & 0x100) != 0) ? true : false;
            seek_to_cylinder(cylinder, restore_state);
            // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
            for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
                sleep_ms(1); 
            }
            if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
                printf("### ERROR, address accepted time exceeded in read_loop\r\n");
                command_clear();
                return;
            }
            for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
                sleep_ms(1); 
            }
            if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
                printf("### ERROR, seek time exceeded in read_loop\r\n");
                command_clear();
                return;
            }
            //select_head(head);
            //load_sector_address(sector);

            read_sector();
            // wait for read to complete before we issue the next read command
            for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
                if(is_read_in_progress() == false)
                    break;
                sleep_ms(1); 
            }
            if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                printf("### ERROR, read command time exceeded in write_loop_zero reading data, testing\r\n");
                command_clear();
                return;
            }
        }
    }
}

void write_loop(Disk_State* dstate, bool verify){
    int list_item;
    bool restore_state;
    int prbs_reg;
    int cylinder, head, sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 8 sector disks
    int byte_errors, sector_errors, sectors_tested;
    bool error_in_this_sector;

    byte_errors = sector_errors = sectors_tested = 0;
    prbs_reg = 0x5555; // PRBS register starting seed value
    // advantage of a fixed seed is that the test runs with the same data and parameters every time, test results are repeatable
    if(verify)
        printf("  Write Loop Verify test. Hit any key to stop the loop.\r\n");
    else
        printf("  Write Loop No Verify test. Hit any key to stop the loop.\r\n");
    printf("  Hit 'P' or 'p' to view current progress.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(list_item = 0; list_item < list_count; list_item++){
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];
        restore_state = ((cylinder & 0x100) != 0) ? true : false;
        seek_to_cylinder(cylinder, restore_state);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop initialization\r\n");
            command_clear();
            return;
        }
        printf("    list_item = %d\r\n", list_item);
        select_head(head);
        load_sector_address(sector);
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            for(i = 0; i < 8; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
            storebyte(prbs_reg & 0xff);
        }
        //seek_to_cylinder(cylinder, false); // for debug just to see a pulse on the logic analyzer prior to the write_sector() call
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in write_loop writing data, initialization\r\n");
            command_clear();
            return;
        }
    }

    printf("  Reading and writing pseudorandom data to sectors in chs list\r\n");
    list_item = 0;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            if(verify)
                printf("  sector errors = %d, byte errors = %d, sectors tested = %d\r\n", sector_errors, byte_errors, sectors_tested);
            else
                printf("  sectors tested = %d\r\n", sectors_tested);
            // if the key was P or p then only print the current progress test result but don't quit
            if((char_from_callback != 'P') && (char_from_callback != 'p')){
                printf("  Ending the Write Loop test\r\n");
                char_from_callback = 0; //reset the value
                return;
            }
            char_from_callback = 0; //reset the value
        }

        // fetch cylinder/head/sector from the chs list
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];

        // If "verify" is enabled then save the tester DRAM data for the selected cylinder/head/sector in a CPU array
        if(verify){
            ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
            load_ram_address(ramaddress);
            //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
            for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
                savebytes[sectorbytes] = readbyte();
            }
        }

        // Read the cylinder/head/sector from disk (disk to tester DRAM)
        restore_state = ((cylinder & 0x100) != 0) ? true : false;
        seek_to_cylinder(cylinder, restore_state);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop read/write testing\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop read/write testing\r\n");
            command_clear();
            return;
        }
        assert_GP6();
        deassert_GP6();
        select_head(head);
        load_sector_address(sector);

        read_sector();
        // wait for read to complete before we compare the data read to the saved data
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
            if(is_read_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, read command time exceeded in write_loop reading data, testing\r\n");
            command_clear();
            return;
        }

        // If "verify" is enabled then compare the freshly read cylinder/head/sector data with the previously saved data in the CPU array
        if(verify){
            error_in_this_sector = false;
            ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
            load_ram_address(ramaddress);
            //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
            for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
                int read_temp = readbyte();
                if(savebytes[sectorbytes] != read_temp){
                    printf("### ERROR, data error, chs = %3d %1d %2d, byte %d, ref=%x, read=%x\r\n",
                        cylinder, head, sector, sectorbytes, savebytes[sectorbytes], read_temp);
                    error_in_this_sector = true;
                    byte_errors++;
                }
            }
            if(error_in_this_sector) sector_errors++;
        }
        sectors_tested++;

        // Compute new pseudorandom data for the cylinder/head/sector and write it to tester DRAM
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            for(i = 0; i < 16; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
            storebyte(prbs_reg & 0xff);
        }

        // Write the new cylinder/head/sector pseudorandom data to disk (tester DRAM to disk)
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if write is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in write_loop writing data, testing\r\n");
            command_clear();
            return;
        }
        list_item++;
        if(list_item >= list_count)
            list_item = 0;
    }
}

void read_sectors(Disk_State* dstate){
    int list_item;
    bool restore_state;
    int cylinder, head, sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 4 sector disks

    printf("  Read Sectors test. Display the sector data in sectors in the CHS list.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    for(list_item = 0; list_item < list_count; list_item++){
        printf("    list_item = %d\r\n", list_item);

        // ============================================================
        // cylinder/head/sector from the chs list
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];

        select_head(head);
        load_sector_address(sector);
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);

        // Read the cylinder/head/sector from disk (disk to tester DRAM)
        restore_state = ((cylinder & 0x100) != 0) ? true : false;
        seek_to_cylinder(cylinder, restore_state);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in read_sectors reading data\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in read_sectors reading data\r\n");
            command_clear();
            return;
        }
        //select_head(head);
        //load_sector_address(sector);

        read_sector();
        // wait for read to complete before we compare the data read from disk
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
            if(is_read_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, read command time exceeded in read_sectors reading data, testing\r\n");
            command_clear();
            return;
        }

        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        printf("    Cylinder = %d, Head = %d, Sector = %d", cylinder, head, sector);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            if((sectorbytes & 0xf) == 0)
                printf("\r\n    ");
            else if((sectorbytes & 0x7) == 0)
                printf("  ");
            int read_temp = readbyte();
            printf("%2x ", read_temp);
        }
        printf("\r\n");
    }
}

void write_loop_zero(Disk_State* dstate, bool verify){
    int list_item;
    bool restore_state;
    int cylinder, head, sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 8 sector disks
    int byte_errors, sector_errors, sectors_tested;
    bool error_in_this_sector;

    byte_errors = sector_errors = sectors_tested = 0;
    // advantage of a fixed seed is that the test runs with the same data and parameters every time, test results are repeatable
    if(verify)
        printf("  Write Loop Zero Verify test. Hit any key to stop the loop.\r\n");
    else
        printf("  Write Loop Zero No Verify test. Hit any key to stop the loop.\r\n");
    printf("  Hit 'P' or 'p' to view current progress.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(list_item = 0; list_item < list_count; list_item++){
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];
        restore_state = ((cylinder & 0x100) != 0) ? true : false;
        seek_to_cylinder(cylinder, restore_state);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop_zero initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop_zero initialization\r\n");
            command_clear();
            return;
        }
        printf("    list_item = %d\r\n", list_item);
        select_head(head);
        load_sector_address(sector);
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            storebyte(0);
        }
        //seek_to_cylinder(cylinder, false); // for debug just to see a pulse on the logic analyzer prior to the write_sector() call
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in write_loop_zero writing data, initialization\r\n");
            command_clear();
            return;
        }
    }

    printf("  Reading and writing zero data to sectors in chs list\r\n");
    list_item = 0;
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            if(verify)
                printf("  sector errors = %d, byte errors = %d, sectors tested = %d\r\n", sector_errors, byte_errors, sectors_tested);
            else
                printf("  sectors tested = %d\r\n", sectors_tested);
            // if the key was P or p then only print the current progress test result but don't quit
            if((char_from_callback != 'P') && (char_from_callback != 'p')){
                printf("  Ending the Write Loop Zero test\r\n");
                char_from_callback = 0; //reset the value
                return;
            }
            char_from_callback = 0; //reset the value
        }

        // cylinder/head/sector from the chs list
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];

        // Read the cylinder/head/sector from disk (disk to tester DRAM)
        restore_state = ((cylinder & 0x100) != 0) ? true : false;
        seek_to_cylinder(cylinder, restore_state);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop_zero read/write testing\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop_zero read/write testing\r\n");
            command_clear();
            return;
        }
        select_head(head);
        load_sector_address(sector);

        read_sector();
        // wait for read to complete before we compare the data read from disk
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
            if(is_read_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, read command time exceeded in write_loop_zero reading data, testing\r\n");
            command_clear();
            return;
        }

        // If "verify" is enabled then compare the freshly read cylinder/head/sector data with zero
        if(verify){
            error_in_this_sector = false;
            ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
            load_ram_address(ramaddress);
            //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
            for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
                int read_temp = readbyte();
                if(read_temp != 0){
                    printf("### ERROR, data error, chs = %3d %1d %2d, byte %d, data = %x\r\n", cylinder, head, sector, sectorbytes, read_temp);
                    error_in_this_sector = true;
                    byte_errors++;
                }
            }
            if(error_in_this_sector){
                sector_errors++;
                printf("  err summary: chs = %3d %1d %2d, sector errs = %d, byte errs = %d, sectors tested = %d\r\n",
                    cylinder, head, sector, sector_errors, byte_errors, sectors_tested);
            }
        }
        sectors_tested++;

        // Compute new data for the cylinder/head/sector and write it to tester DRAM
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            storebyte(0);
        }

        // Write the new cylinder/head/sector zero data to disk (tester DRAM to disk)
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if write is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in write_loop_zero writing data, testing\r\n");
            command_clear();
            return;
        }
        list_item++;
        if(list_item >= list_count)
            list_item = 0;
    }
}

void disk_initialize(Disk_State* dstate){
    // write the first word of the sector with Cylinder Address shifted left by 5 bits
    // the remaining words of the sector are all zero which includes the position of the CRC word at the end of the sector
    int cylinder, head, sector, permuted_sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;

    printf("  Disk Initialize to zero.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(cylinder = 0; cylinder < dstate->numberOfCylinders; cylinder++){
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in disk_initialize initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in disk_initialize initialization\r\n");
            command_clear();
            return;
        }
        if((cylinder % 20) == 0)
            printf("    cylinder = %d\r\n", cylinder);
        for(head = 0; head < dstate->numberOfHeads; head++){
            select_head(head);
            for(sector = 0; sector < dstate->numberOfSectorsPerTrack; sector++){
                // compute a permuted sector so the sector sequence is interleaved while writing the initialization data
                permuted_sector = ((sector % (dstate->numberOfSectorsPerTrack / 2)) * 2) + (sector / (dstate->numberOfSectorsPerTrack / 2));
                load_sector_address(permuted_sector);
                //printf("---cyl = %d, head = %d, sect = %d, psect = %d\r\n", cylinder, head, sector, permuted_sector);
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, permuted_sector);
                load_ram_address(ramaddress);
                int header_word = cylinder << 5; // cylinder shifted left by 5 bits is the header
                storebyte(header_word & 0xff);
                storebyte((header_word >> 8) & 0xff);
                //for debug to see the head and sector in the data
                //storebyte(head & 0xff);
                //storebyte(permuted_sector & 0xff);
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength - 32) / 8); sectorbytes++){ //shortened for debug, to write head and permuted_sector
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength - 16) / 8); sectorbytes++){
                for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start - 16 + 15) >> 4) * 2); sectorbytes++){
                    storebyte(0);
                }
                //seek_to_cylinder(cylinder, false); // for debug, just to see a pulse on the logic analyzer prior to the write_sector() call
                write_sector();
                for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
                    if(is_write_in_progress() == false)
                        break;
                    sleep_ms(1); 
                }
                if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                    printf("### ERROR, write command time exceeded in disk_initialize writing data, initialization\r\n");
                    printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
                    command_clear();
                    return;
                }
            }
        }
    }
    printf("  Initialization is complete.\r\n");
}

void write_sectors_ia(Disk_State* dstate){
    // write the first word of the sector with Cylinder Address shifted left by 5 bits
    // the remaining words of the sector are the Cylinder, Head, and Sector. The CRC word at the end of the sector is all-zero.
    int list_item;
    bool restore_state;
    int cylinder, head, sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 4 sector disks

    printf("  Disk Initialize specified sectors with their CHS address.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(list_item = 0; list_item < list_count; list_item++){
        // cylinder/head/sector from the chs list
        cylinder = list_cylinder[list_item];
        head = list_head[list_item];
        sector = list_sector[list_item];
        printf("    list_item = %d, cyl = %d, head = %d, sect = %d\r\n", list_item, cylinder, head, sector);

        select_head(head);
        load_sector_address(sector);
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_sectors_ia initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_sectors_ia initialization\r\n");
            command_clear();
            return;
        }
        int header_word = cylinder << 5; // cylinder shifted left by 5 bits is the header
        storebyte(header_word & 0xff);
        storebyte((header_word >> 8) & 0xff);
        //write cylinder, head, and sector to the data block of all sectors
        //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength - 16) / 16); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < ((dstate->bit_times_data_bits_after_start - 16 + 15) >> 4); sectorbytes++){ // this loop stores words
            storebyte(cylinder);
            storebyte((head << 4) | sector);
        }
        //seek_to_cylinder(cylinder, false); // for debug, just to see a pulse on the logic analyzer prior to the write_sector() call
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in disk_initialize writing data, initialization\r\n");
            printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
            command_clear();
            return;
        }
    }
    printf("  Initialization is complete.\r\n");
}

void disk_initaddr(Disk_State* dstate){
    // write the first word of the sector with Cylinder Address shifted left by 5 bits
    // the remaining words of the sector are the Cylinder, Head, and Sector. The CRC word at the end of the sector is all-zero.
    int cylinder, head, sector, permuted_sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;

    printf("  Disk Initialize all sectors with their CHS address.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(cylinder = 0; cylinder < dstate->numberOfCylinders; cylinder++){
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in disk_initaddr initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in disk_initaddr initialization\r\n");
            command_clear();
            return;
        }
        if((cylinder % 20) == 0)
            printf("    cylinder = %d\r\n", cylinder);
        for(head = 0; head < dstate->numberOfHeads; head++){
            select_head(head);
            for(sector = 0; sector < dstate->numberOfSectorsPerTrack; sector++){
                // compute a permuted sector so the sector sequence is interleaved while writing the initialization data
                permuted_sector = ((sector % (dstate->numberOfSectorsPerTrack / 2)) * 2) + (sector / (dstate->numberOfSectorsPerTrack / 2));
                load_sector_address(permuted_sector);
                //printf("---cyl = %d, head = %d, sect = %d, psect = %d\r\n", cylinder, head, sector, permuted_sector);
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, permuted_sector);
                load_ram_address(ramaddress);
                int header_word = cylinder << 5; // cylinder shifted left by 5 bits is the header
                storebyte(header_word & 0xff);
                storebyte((header_word >> 8) & 0xff);
                //for debug to see the head and sector in the data
                //storebyte(head & 0xff);
                //storebyte(permuted_sector & 0xff);
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength - 32) / 8); sectorbytes++){ //shortened for debug, to write head and permuted_sector
                //write cylinder, head, and sector to the data block of all sectors
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength - 16) / 16); sectorbytes++){
                for(sectorbytes = 0; sectorbytes < ((dstate->bit_times_data_bits_after_start - 16 + 15) >> 4); sectorbytes++){ // this loop stores words
                    storebyte(cylinder);
                    storebyte(head<<4 | permuted_sector);
                }
                //seek_to_cylinder(cylinder, false); // for debug, just to see a pulse on the logic analyzer prior to the write_sector() call
                write_sector();
                for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
                    if(is_write_in_progress() == false)
                        break;
                    sleep_ms(1); 
                }
                if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                    printf("### ERROR, write command time exceeded in disk_initialize writing data, initialization\r\n");
                    printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
                    command_clear();
                    return;
                }
            }
        }
    }
    printf("  Initialization is complete.\r\n");
}

void write_loop_random(Disk_State* dstate){
    int prbs_reg;
    int cylinder, head, sector, permuted_sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;
    static uint8_t savebytes[SAVE_BUFFER_SIZE]; // for RK11-E mode this needs to be at least ((256 * 18) + 32) / 8 = 580, 
        // so make it oversized, even for 8 sector disks
    int byte_errors, sector_errors, sectors_tested;
    bool error_in_this_sector;

    byte_errors = sector_errors = sectors_tested = 0;
    prbs_reg = 0x5555; // PRBS register starting seed value ==============================================================================
    // advantage of a fixed seed is that the test runs with the same data and parameters every time, test results are repeatable
    printf("  Write Loop Random test. Hit any key to stop the loop.\r\n");
    printf("  Hit 'P' or 'p' to view current progress.\r\n");
    if(check_drive_ready() == false){
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  Initializing disk data\r\n");
    for(cylinder = 0; cylinder < dstate->numberOfCylinders; cylinder++){ //==============================
    //for(cylinder = 0; cylinder < 1; cylinder++){ // just 1 cylinder for debugging =======================
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop_random initialization\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop_random initialization\r\n");
            command_clear();
            return;
        }
        if((cylinder % 20) == 0)
            printf("    cylinder = %d\r\n", cylinder);
        for(head = 0; head < dstate->numberOfHeads; head++){
            select_head(head);
            for(sector = 0; sector < dstate->numberOfSectorsPerTrack; sector++){
                // compute a permuted sector so the sector sequence is interleaved while writing the initialization data
                permuted_sector = ((sector % (dstate->numberOfSectorsPerTrack / 2)) * 2) + (sector / (dstate->numberOfSectorsPerTrack / 2));
                load_sector_address(permuted_sector);
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, permuted_sector);
                load_ram_address(ramaddress);
                //prbs_reg = (cylinder << 5) | (head << 4) | permuted_sector; // PRBS register starting value for each sector
                //printf("\r\nDEBUG, C = %3x, H = %x, S = %2x, prbsreg = %6x\r\n", cylinder, head, permuted_sector, prbs_reg);
                //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
                for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
                    //if((sectorbytes & 0xf) == 0)
                        //printf("\r\n    "); 
                    //else if((sectorbytes & 0x7) == 0)
                        //printf("  "); 
                    for(i = 0; i < 8; i++) shift_prbs16(&prbs_reg); // shift 8 times so the next value is less correlated with the previous value
                    storebyte(prbs_reg & 0xff);
                    //printf("%2x ", prbs_reg & 0xff);
                    //prbs_reg++;
                }
                //seek_to_cylinder(cylinder, false); // for debug just to see a pulse on the logic analyzer prior to the write_sector() call
                write_sector();
                for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
                    if(is_write_in_progress() == false)
                        break;
                    sleep_ms(1); 
                }
                if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                    printf("### ERROR, write command time exceeded in write_loop_random writing data,\r\n   during initialization\r\n");
                    printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
                    command_clear();
                    return;
                }
            }
        }
    }
    printf("  Reading and writing pseudorandom sectors with pseudorandom data\r\n");
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  sector errors = %d, byte errors = %d, sectors tested = %d\r\n", sector_errors, byte_errors, sectors_tested);
            // if the key was P or p then only print the current progress test result but don't quit
            if((char_from_callback != 'P') && (char_from_callback != 'p')){
                printf("  Ending the Write Loop Random test\r\n");
                char_from_callback = 0; //reset the value
                return;
            }
            char_from_callback = 0; //reset the value
        }

        // select cylinder/head/sector using pseudorandom value
        for(i = 0; i < 16; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
        cylinder = (prbs_reg * dstate->numberOfCylinders) / 65535;
        for(i = 0; i < 16; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
        head = (prbs_reg * dstate->numberOfHeads) / 65535;
        for(i = 0; i < 16; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
        sector = (prbs_reg * dstate->numberOfSectorsPerTrack) / 65535;

        // Save the tester DRAM data for the selected cylinder/head/sector in a CPU array
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            savebytes[sectorbytes] = readbyte();
        }

        // to be safe, wipe the tester DRAM data for the selected cylinder/head/sector, make it all 0xff before we read from the disk
        // This is so we know data was actually read from the disk to be present in this cylinder/head/sector location in the DRAM.
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            storebyte(0xff);
        }

        // Read the cylinder/head/sector from disk (disk to tester DRAM)
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in write_loop_random read/write testing\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in write_loop_random read/write testing\r\n");
            command_clear();
            return;
        }
        select_head(head);
        load_sector_address(sector);

        read_sector();
        // wait for read to complete before we compare the data read to the saved data
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
            if(is_read_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
            printf("### ERROR, read command time exceeded in write_loop_random reading data, testing\r\n");
            command_clear();
            return;
        }

        // Compare the freshly read cylinder/head/sector data with the previously saved data in the CPU array
        error_in_this_sector = false;
        ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, sector);
        load_ram_address(ramaddress);
        int sector_number_of_bytes = (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); // number of bytes in the sector data area
        int remainder_bits = dstate->bit_times_data_bits_after_start &0xf;  // number of bits in the last partial word, 0 if full word
        int bit_mask_h = 0xffff >> (16 - remainder_bits); // mask for the last partial word, 0xffff if full word
        int bit_mask_l = 0xff & bit_mask_h;  // lower byte of mask for the last partial word
        bit_mask_h = bit_mask_h >> 8; // upper byte of mask for the last partial word
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            int read_temp = readbyte();
            if((sectorbytes == (sector_number_of_bytes - 2)) && (remainder_bits != 0)){ // if this is the second to last partial byte in the sector
                // apply a mask to ignore the unused bits in the next to last partial byte
                read_temp = read_temp & bit_mask_l;
                savebytes[sectorbytes] = savebytes[sectorbytes] & bit_mask_l;
            }
            if((sectorbytes == (sector_number_of_bytes - 1)) && (remainder_bits != 0)){ // if this is the last partial byte in the sector
                // apply a mask to ignore the unused bits in the last partial byte
                read_temp = read_temp & bit_mask_h;
                savebytes[sectorbytes] = savebytes[sectorbytes] & bit_mask_h;
            }
            if(savebytes[sectorbytes] != read_temp){
                //printf("### ERROR, data error, chs = %3d %1d %2d, byte %d, ref=%x, read=%x\r\n", // print the error details
                    //cylinder, head, sector, sectorbytes, savebytes[sectorbytes], read_temp); 
                printf("### Dt err, chs = %3d %1d %2d, B# %d, ref=%2x, read=%2x, ML=%2x, MH=%2x, #B=%d, rem=%d\r\n", // debug
                    cylinder, head, sector, sectorbytes, savebytes[sectorbytes], read_temp, bit_mask_l, bit_mask_h, sector_number_of_bytes, remainder_bits); // debug
                error_in_this_sector = true;
                byte_errors++;
            }
        }
        sectors_tested++;
        if(error_in_this_sector){
            sector_errors++;
            printf("  err summary: chs = %3d %1d %2d, sector errs = %d, byte errs = %d, sectors tested = %d\r\n",
                cylinder, head, sector, sector_errors, byte_errors, sectors_tested);
        }

        // Compute new pseudorandom data for the cylinder/head/sector and write it to tester DRAM
        load_ram_address(ramaddress);
        //for(sectorbytes = 0; sectorbytes < (dstate->dataLength / 8); sectorbytes++){
        for(sectorbytes = 0; sectorbytes < (((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2); sectorbytes++){
            for(i = 0; i < 16; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
            storebyte(prbs_reg & 0xff);
        }

        // Write the new cylinder/head/sector pseudorandom data to disk (tester DRAM to disk)
        write_sector();
        for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
            if(is_write_in_progress() == false)
                break;
            sleep_ms(1); 
        }
        if(readwrite_time >= READWRITE_TIMEOUT){ // if write is not complete (timeout) then print error and abort
            printf("### ERROR, write command time exceeded in write_loop_random writing data, testing\r\n");
            printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
            command_clear();
            return;
        }
    }
}

void disk_write_image(Disk_State* dstate, char* filename){
    int cylinder, head, sector, permuted_sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress; 
    int i;

    microSD_LED_off();
    printf("  External Disk Write Image.\r\n");
    if(check_drive_ready() == false){ // check to see if the connected drive is ready, if so then continue
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  external drive is ready\r\n");

    if(!is_card_present()){ // check to see if the microSD is present, if so then continue
        //error_code = 1;
        printf("*** ERROR, microSD card is not inserted\r\n");
        return;
    }
    printf("  microSD card is present\r\n");

    // Check to see if the file system can be started.
    printf("  starting filesystem\r\n");
    if(!file_init_and_mount()) {
        printf("*** ERROR, could not init and mount microSD filesystem\r\n");
        return;
    }

    printf("  opening file '%s'\r\n", filename);
    microSD_LED_on();
    if(file_open_read_disk_image_n(filename) != FR_OK){
        printf("*** ERROR, could not open microSD file '%s'\r\n", filename);
        microSD_LED_off();
        return;
    }

    printf("  reading file header data\r\n");
    if(read_image_file_header(dstate) != 0){
        printf("*** ERROR, could not read microSD file header\r\n");
        microSD_LED_off();
        return;
    }

    printf("  reading disk image data\r\n");
    if(read_disk_image_data_tester(dstate) != FR_OK){
        printf("*** ERROR, could not read disk image data from microSD file\r\n");
        microSD_LED_off();
        return;
    }

    printf("  closing file\r\n");
    if(file_close_disk_image() != FR_OK){
        printf("*** ERROR, could not read disk image data from microSD file\r\n");
        microSD_LED_off();
        return;
    }
    microSD_LED_off();
    printf("  file closed and filesystem unmounted\r\n");

    printf("  Writing disk data\r\n");
    for(cylinder = 0; cylinder < dstate->numberOfCylinders; cylinder++){
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in disk_write_image wile writing\r\n");
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in disk_write_image while writing.\r\n");
            command_clear();
            return;
        }
        if((cylinder % 10) == 0)
            printf("    cylinder = %d\r\n", cylinder);
        for(head = 0; head < dstate->numberOfHeads; head++){
            select_head(head);
            for(sector = 0; sector < dstate->numberOfSectorsPerTrack; sector++){
                // compute a permuted sector so the sector sequence is interleaved while writing the initialization data
                permuted_sector = ((sector % (dstate->numberOfSectorsPerTrack / 2)) * 2) + (sector / (dstate->numberOfSectorsPerTrack / 2));
                load_sector_address(permuted_sector);
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, permuted_sector);
                //load_ram_address(ramaddress);
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength + 7) >> 3); sectorbytes++){ // round the byte count up to the next integer value
                    //for(i = 0; i < 8; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
                    //storebyte(prbs_reg & 0xff);
                //}
                //seek_to_cylinder(cylinder, false); // for debug just to see a pulse on the logic analyzer prior to the write_sector() call
                write_sector();
                for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive write Ready or timeout
                    if(is_write_in_progress() == false)
                        break;
                    sleep_ms(1); 
                }
                if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                    printf("### ERROR, write command time exceeded in disk_write_image writing data,\r\n");
                    printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
                    command_clear();
                    return;
                }
            }
        }
    }
    printf("  Writing data to disk is complete.\r\n");
}

// Loop for all cylinders on the disk:
//   Read a cylinder of data from the connected drive and write to the tester DRAM
//   Read the cylinder of data from the tester DRAM and write to the microSD
//
void disk_read_image(Disk_State* dstate, char* filename){
    int cylinder, head, sector, permuted_sector, sectorbytes;
    int seek_time;          // in increments of 1 msec
    int addr_accepted_time; // in increments of 1 msec
    int readwrite_time;     // in increments of 1 msec
    int ramaddress;
    int i;

    microSD_LED_off();
    printf("  External Disk Read Image.\r\n");
    if(check_drive_ready() == false){ // check to see if the connected drive is ready, if so then continue
        printf(" Drive is not ready. Operation terminated.\r\n");
        return;
    }
    printf("  external drive is ready\r\n");

    if(!is_card_present()){ // check to see if the microSD is present, if so then continue
        //error_code = 1;
        printf("*** ERROR, microSD card is not inserted\r\n");
        return;
    }
    printf("  microSD card is present\r\n");

    // Check to see if the file system can be started.
    if(!file_init_and_mount()) {
        printf("*** ERROR, could not init and mount microSD filesystem\r\n");
        return;
    }
    printf("  filesystem started\r\n");

    printf("  microSD LED is on\r\n");
    microSD_LED_on();

    // open the file for writing
    printf("  opening the disk image file\r\n");
    if(file_open_write_disk_image_n(filename) != FR_OK){
        printf("*** ERROR, could not open microSD file [%s]\r\n", filename);
        microSD_LED_off();
        return;
    }

    // write the file header
    printf("  writing the file header\r\n");
    strncpy(dstate->imageName, filename, 10);
    dstate->imageName[10] = '\0';
    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
    if(write_image_file_header(dstate) != 0){
        printf("*** ERROR, could not write microSD file header\r\n");
        microSD_LED_off();
        return;
    }

    // read data from the connected disk into Tester DRAM
    printf("  Reading external disk data\r\n");
    for(cylinder = 0; cylinder < dstate->numberOfCylinders; cylinder++){
        seek_to_cylinder(cylinder, false);
        // wait for BUS_ADDR_ACCEPTED_L to be asserted or timeout
        for(addr_accepted_time = 0; ((addr_accepted_time < ADDR_ACCEPTED_TIMEOUT) && (is_addr_accepted_ready() == false)); addr_accepted_time++){
            sleep_ms(1); 
        }
        if(addr_accepted_time >= ADDR_ACCEPTED_TIMEOUT){ // if address accepted has not been asserted (timeout) then print error and abort
            printf("### ERROR, address accepted time exceeded in disk_read_image while reading. Cylinder = %d\r\n", cylinder);
            command_clear();
            return;
        }
        for(seek_time = 0; ((seek_time < SEEK_TIMEOUT) && (is_rws_ready() == false)); seek_time++){ // wait for drive RWS Ready or timeout
            sleep_ms(1); 
        }
        if(seek_time >= SEEK_TIMEOUT){ // if not ready to seek (timeout) then print error and abort
            printf("### ERROR, seek time exceeded in disk_read_image while reading. Cylinder = %d\r\n", cylinder);
            command_clear();
            microSD_LED_off();
            return;
        }
        if((cylinder % 20) == 0)
            printf("    cylinder = %d\r\n", cylinder);
        for(head = 0; head < dstate->numberOfHeads; head++){
            select_head(head);
            for(sector = 0; sector < dstate->numberOfSectorsPerTrack; sector++){
                // compute a permuted sector so the sector sequence is interleaved while writing the initialization data
                permuted_sector = ((sector % (dstate->numberOfSectorsPerTrack / 2)) * 2) + (sector / (dstate->numberOfSectorsPerTrack / 2));
                load_sector_address(permuted_sector);
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylinder, head, permuted_sector);
                load_ram_address(ramaddress);
                //for(sectorbytes = 0; sectorbytes < ((dstate->dataLength + 7) >> 3); sectorbytes++){ // round the byte count up to the next integer value
                    //for(i = 0; i < 8; i++) shift_prbs16(&prbs_reg); // shift 16 times so the next value is less correlated with the previous
                    //storebyte(prbs_reg & 0xff);
                //}
                //seek_to_cylinder(cylinder, false); // for debug just to see a pulse on the logic analyzer prior to the write_sector() call
                read_sector();
                for(readwrite_time = 0; readwrite_time < READWRITE_TIMEOUT; readwrite_time++){ // wait for drive read Ready or timeout
                    if(is_read_in_progress() == false)
                        break;
                    sleep_ms(1);
                }
                if(readwrite_time >= READWRITE_TIMEOUT){ // if read is not complete (timeout) then print error and abort
                    printf("### ERROR, read command time exceeded in disk_read_image reading data,\r\n");
                    printf("    Check the controller mode setting, DISP STATUS, then MODE CONT <setting>\r\n");
                    command_clear();
                    microSD_LED_off();
                    return;
                }
            }
            //compute_ram_address(int number_of_sectors, int cylinder, int head, int sector)
        }
    }
    printf("  Reading data from disk is complete.\r\n");

    // regarding the data that we just wrote into the Tester DRAM, write that data out to the microSD card
    printf("  Writing external disk data to microSD.\r\n");
    // Check to see if the file system can be started.
    if(write_disk_image_data(dstate) != FR_OK) {
        printf("*** ERROR, disk data could not be written to microSD\r\n");
        microSD_LED_off();
        return;
    }
    printf("  writing image to microSD is complete\r\n");

    printf("  Closing the microSD file.\r\n");
    // close the microSD file.
    if(file_close_disk_image() != FR_OK) {
        printf("*** ERROR, microSD file could not be closed\r\n");
        microSD_LED_off();
        return;
    }
    printf("  Closing the microSD file is complete\r\n  The entire operation is complete\r\n");
    microSD_LED_off();

    // force unmount
    //printf("  Force unmount.\r\n");
    //force_unmount();
}

void mode_rk11d(Disk_State* dstate, bool on_off){
    if(on_off){
        set_rk11de_mode();
        dstate->rk11d = true;
        printf("  Mode RK11D ON.\r\n");
    }
    else{
        clear_rk11de_mode();
        dstate->rk11d = false;
        printf("  Mode RK11D OFF.\r\n");
    }
}

void mode_wrprot(Disk_State* dstate, bool on_off){
    printf("  Mode WTPROT ");
    if(on_off){
        printf("ON.\r\n");
        set_wtprot_mode();
        dstate->wtprot = true;
    }
    else{
        printf("OFF.\r\n");
        clear_wtprot_mode();
        dstate->wtprot = false;
    }
}

void ramtest(int start_address, int num_bytes){
    #define MASK_FOR_DOT 0xfffff
    int prbs_reg;
    int bytecount, i;
    int byte_errors;

    byte_errors = 0;
    prbs_reg = 0x12345678; // initialize PRBS register starting seed value
    // fixed seed, 2^31 - 1 length sequence, the PRBS data is uncorrelated with the RAM address
    if(num_bytes == 0){
        printf("### Error, number of bytes is zero, no test\r\n");
        return;
    }
    printf("  Ramtest. Test range from address %x to %x\r\n", start_address, start_address + num_bytes - 1);
    printf("  Filling the entire test range with a pseudorandom pattern.\r\n  ");
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        if((bytecount & MASK_FOR_DOT) == 0) // print a progress dot on the console
            printf(".");
        shift_prbs31(&prbs_reg); // shift 1 time
        storebyte(prbs_reg & 0xff);
    }

    printf("\r\n  Reading back and checking the test range.\r\n  ");
    prbs_reg = 0x12345678; // re-initialize PRBS register starting seed value
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        if((bytecount & MASK_FOR_DOT) == 0) // print a progress dot on the console
            printf(".");
        shift_prbs31(&prbs_reg); // shift 1 time
        int tempval = readbyte();
        if(tempval != (prbs_reg & 0xff)){
            printf("### Error, address = %x, ideal = %x, readback = %x\r\n", start_address + bytecount, prbs_reg & 0xff, tempval);
            byte_errors++;
        }
    }

    prbs_reg = 0x12345678; // initialize PRBS register starting seed value
    // fixed seed
    printf("\r\n  Filling the entire test range with an inverted pseudorandom pattern.\r\n  ");
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        if((bytecount & MASK_FOR_DOT) == 0) // print a progress dot on the console
            printf(".");
        shift_prbs31(&prbs_reg); // shift 1 time
        storebyte(~prbs_reg & 0xff);
    }

    printf("\r\n  Reading back the inverted data and checking the test range.\r\n  ");
    prbs_reg = 0x12345678; // re-initialize PRBS register starting seed value
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        if((bytecount & MASK_FOR_DOT) == 0) // print a progress dot on the console
            printf(".");
        shift_prbs31(&prbs_reg); // shift 1 time
        int tempval = readbyte();
        if(tempval != (~prbs_reg & 0xff)){
            printf("### Error, address = %x, ideal = %x, readback = %x\r\n", start_address + bytecount, ~prbs_reg & 0xff, tempval);
            byte_errors++;
        }
    }
    printf("\r\n Test complete. Byte error count = %d\r\n", byte_errors);
}

void set_interface_test_mode(Disk_State* dstate){
    dstate->Drive_Address = -1; // address of -1 indicates Interface Test Mode
    enable_interface_test_mode();
    printf("  Must reboot to exit Interface Test Mode!\r\n");
}

// it's about time to build a command dispatch table instead of this long sequence of "if then else"
//
void command_parse_and_dispatch (Disk_State* dstate)
{
    int p1_numeric, p2_numeric, p3_numeric;
    int tempval;

    if(extract_argc == 0){ // don't process commands if the number of command fields is zero
        printf("no commands\r\n");
        return;
    }

    if((strcmp((char *) "ADDR", extract_argv[0])==0) || (strcmp((char *) "A", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if((strcmp((char *) "LOOP", extract_argv[1])==0) || (strcmp((char *) "L", extract_argv[1])==0)){
            address_loop_test(dstate);
        }
        else if((strlen(extract_argv[1])==1) && (strcmp(extract_argv[1], (char *) "0") >= 0) && (strcmp(extract_argv[1], (char *) "7") <= 0)){
            sscanf(extract_argv[1], "%d", &p2_numeric);
            if((p2_numeric >= 0) && (p2_numeric <= 7)){
                dstate->Drive_Address = p2_numeric;
                load_drive_address(p2_numeric);
                printf(" Drive Address = %x\r\n", dstate->Drive_Address);
            }
        }
        else
            printf("### ERROR, invalid field2 \"%s\", or invalid Drive Address, must be 0 to 7\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "MAKELIST", extract_argv[0])==0) || (strcmp((char *) "M", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if(strcmp((char *) "CYL", extract_argv[1])==0){
            makelist_cyl();
        }
        else if(strcmp((char *) "CHS", extract_argv[1])==0){
            if(extract_argc == 2)
                makelist_chs();
            else
                printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "DISPLAY", extract_argv[0])==0) || (strcmp((char *) "DISP", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if(strcmp((char *) "CYL", extract_argv[1])==0){
            display_cyl();
        }
        else if(strcmp((char *) "CHS", extract_argv[1])==0){
            if(extract_argc == 2)
                display_chs();
            else
                printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        }
        else if((strcmp((char *) "STATUS", extract_argv[1])==0) || (strcmp((char *) "S", extract_argv[1])==0)){
            if(extract_argc == 2)
                display_status(dstate);
            else
                printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "SEEK", extract_argv[0])==0) || (strcmp((char *) "S", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if((strcmp((char *) "LOOP", extract_argv[1])==0) || (strcmp((char *) "L", extract_argv[1])==0)){
            seek_loop(true);
        }
        else if((strcmp((char *) "LOOPN", extract_argv[1])==0) || (strcmp((char *) "LN", extract_argv[1])==0)){
            seek_loop(false);
        }
        else if((strcmp((char *) "STEP", extract_argv[1])==0) || (strcmp((char *) "S", extract_argv[1])==0)){
            seek_step(dstate);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "READ", extract_argv[0])==0) || (strcmp((char *) "R", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if((strcmp((char *) "LOOP", extract_argv[1])==0) || (strcmp((char *) "L", extract_argv[1])==0)){
            read_loop(dstate);
        }
        else if((strcmp((char *) "SECTOR", extract_argv[1])==0) || (strcmp((char *) "S", extract_argv[1])==0)){
            read_sectors(dstate);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "WRITE", extract_argv[0])==0) || (strcmp((char *) "W", extract_argv[0])==0)){
        if(extract_argc != 3)
            printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        else if((strcmp((char *) "LOOP", extract_argv[1])==0) || (strcmp((char *) "L", extract_argv[1])==0)){
            if(extract_argc != 3)
                printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            else if((strcmp((char *) "VERIFY", extract_argv[2])==0) || (strcmp((char *) "V", extract_argv[2])==0)){
                write_loop(dstate, true);
            }
            else if((strcmp((char *) "NOVERIFY", extract_argv[2])==0) || (strcmp((char *) "N", extract_argv[2])==0)){
                write_loop(dstate, false);
            }
            else if((strcmp((char *) "ZEROVERIFY", extract_argv[2])==0) || (strcmp((char *) "ZV", extract_argv[2])==0)){
                write_loop_zero(dstate, true);
            }
            else if((strcmp((char *) "ZERONOVERIFY", extract_argv[2])==0) || (strcmp((char *) "ZN", extract_argv[2])==0)){
                write_loop_zero(dstate, false);
            }
            else if((strcmp((char *) "RANDOM", extract_argv[2])==0) || (strcmp((char *) "R", extract_argv[2])==0)){
                write_loop_random(dstate);
            }
            else
                printf("### ERROR, invalid field3 \"%s\"\r\n", extract_argv[2]);
        }
        else if((strcmp((char *) "ONCE", extract_argv[1])==0) || (strcmp((char *) "O", extract_argv[1])==0)){
            if(extract_argc != 3)
                printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            else if((strcmp((char *) "INITIALIZE", extract_argv[2])==0) || (strcmp((char *) "INIT", extract_argv[2])==0)){
                disk_initialize(dstate);
            }
            else if((strcmp((char *) "INITADDR", extract_argv[2])==0) || (strcmp((char *) "IA", extract_argv[2])==0)){
                disk_initaddr(dstate);
            }
            else if((strcmp((char *) "SECTOR", extract_argv[2])==0) || (strcmp((char *) "S", extract_argv[2])==0)){
                write_sectors_ia(dstate);
            }
            else
                printf("### ERROR, invalid field3 \"%s\"\r\n", extract_argv[2]);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "DISK", extract_argv[0])==0) || (strcmp((char *) "DSK", extract_argv[0])==0)){
        if(extract_argc != 3)
            printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        else if((strcmp((char *) "WRITE", extract_argv[1])==0) || (strcmp((char *) "W", extract_argv[1])==0)){
            disk_write_image(dstate, extract_argv[2]);
        }
        else if((strcmp((char *) "READ", extract_argv[1])==0) || (strcmp((char *) "R", extract_argv[1])==0)){
            disk_read_image(dstate, extract_argv[2]);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "REGISTER", extract_argv[0])==0) || (strcmp((char *) "REG", extract_argv[0])==0)){
        if(extract_argc != 3)
            printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        else{
            sscanf(extract_argv[1], "%x", &p2_numeric);
            sscanf(extract_argv[2], "%x", &p3_numeric);
            tempval = read_write_spi_register(p2_numeric & 0xff, p3_numeric & 0xff);
            if(p2_numeric >= 0x80)
                printf("  read reg 0x%x -> 0x%x\r\n", p2_numeric, tempval);
            else
                printf("  write reg 0x%x <- 0x%x\r\n", p2_numeric, p3_numeric);
        }
    }
    else if((strcmp((char *) "MODE", extract_argv[0])==0) || (strcmp((char *) "MD", extract_argv[0])==0)){
        if(extract_argc != 3)
            printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        else if(strcmp((char *) "RK11D", extract_argv[1])==0){
            if((strcmp((char *) "OFF", extract_argv[2])==0) || (strcmp((char *) "0", extract_argv[2])==0)){
                if(extract_argc == 3)
                    mode_rk11d(dstate, false);
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if((strcmp((char *) "ON", extract_argv[2])==0) || (strcmp((char *) "1", extract_argv[2])==0)){
                if(extract_argc == 3)
                    mode_rk11d(dstate, true);
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
        }
        else if((strcmp((char *) "WTPROT", extract_argv[1])==0) || (strcmp((char *) "WP", extract_argv[1])==0)){
            if((strcmp((char *) "OFF", extract_argv[2])==0) || (strcmp((char *) "0", extract_argv[2])==0)){
                if(extract_argc == 3)
                    mode_wrprot(dstate, false);
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if((strcmp((char *) "ON", extract_argv[2])==0) || (strcmp((char *) "1", extract_argv[2])==0)){
                if(extract_argc == 3)
                    mode_wrprot(dstate, true);
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else
                printf("### ERROR, invalid field3 \"%s\"\r\n", extract_argv[2]);
        }
        else if((strcmp((char *) "CONTROLLER", extract_argv[1])==0) || (strcmp((char *) "CONT", extract_argv[1])==0) || (strcmp((char *) "C", extract_argv[1])==0)){
            if(strcmp((char *) "RK8E", extract_argv[2])==0){
                if(extract_argc == 3){
                    // initialize states to PDP-8 RK8-E values
                    strcpy(dstate->controller, "RK8-E");
                    strcpy(dstate->imageName, "imageName");
                    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
                    strcpy(dstate->imageDate, "1/1/2025");
                    dstate->bitRate = 1440000;
                    dstate->preamble1Length = 120;
                    dstate->preamble2Length = 82;
                    // was dstate->bit_times_sector_to_start = 210;
                    dstate->bit_times_data_bits_after_start = 3104;
                    dstate->postambleLength = 36;
                    dstate->numberOfCylinders = 203;
                    dstate->numberOfSectorsPerTrack = 16;
                    dstate->numberOfHeads = 2;
                    dstate->microsecondsPerSector = 2500;
                    dstate->rk11d = false;
                    update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if(strcmp((char *) "RK11D", extract_argv[2])==0){
                if(extract_argc == 3){
                    // initialize states to PDP-11 RK11-D values
                    strcpy(dstate->controller, "RK11-D");
                    strcpy(dstate->imageName, "imageName");
                    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
                    strcpy(dstate->imageDate, "1/1/2025");
                    dstate->bitRate = 1440000;
                    dstate->preamble1Length = 128;
                    dstate->preamble2Length = 80;
                    // was dstate->bit_times_sector_to_start = 216;
                    dstate->bit_times_data_bits_after_start = 4128;
                    dstate->postambleLength = 16;
                    dstate->numberOfCylinders = 203;
                    dstate->numberOfSectorsPerTrack = 12;
                    dstate->numberOfHeads = 2;
                    dstate->microsecondsPerSector = 3333;
                    dstate->rk11d = true;
                    update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if(strcmp((char *) "RK11E", extract_argv[2])==0){
                if(extract_argc == 3){
                    // initialize states to PDP-15 RK11-E values
                    strcpy(dstate->controller, "RK11-E");
                    strcpy(dstate->imageName, "imageName");
                    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
                    strcpy(dstate->imageDate, "1/1/2025");
                    dstate->bitRate = 1545000;
                    dstate->preamble1Length = 128;
                    dstate->preamble2Length = 80;
                    // was dstate->bit_times_sector_to_start = 217;
                    dstate->bit_times_data_bits_after_start = 4640;
                    dstate->postambleLength = 16;
                    dstate->numberOfCylinders = 203;
                    dstate->numberOfSectorsPerTrack = 12;
                    dstate->numberOfHeads = 2;
                    dstate->microsecondsPerSector = 3333;
                    dstate->rk11d = true;
                    update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if(strcmp((char *) "ALTO", extract_argv[2])==0){
                if(extract_argc == 3){
                    // initialize states to Xerox Alto values
                    strcpy(dstate->controller, "XEROX_ALTO");
                    strcpy(dstate->imageName, "imageName");
                    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
                    strcpy(dstate->imageDate, "1/1/2025");
                    dstate->bitRate = 1600000;
                    dstate->preamble1Length = 120;
                    dstate->preamble2Length = 82;
                    // was dstate->bit_times_sector_to_start = 210;
                    dstate->bit_times_data_bits_after_start = 4272;
                    dstate->postambleLength = 16;
                    dstate->numberOfCylinders = 203;
                    dstate->numberOfSectorsPerTrack = 12;
                    dstate->numberOfHeads = 2;
                    dstate->microsecondsPerSector = 3333;
                    dstate->rk11d = false;
                    update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else if(strcmp((char *) "NOVA", extract_argv[2])==0){
                if(extract_argc == 3){
                    // initialize states to Data General Nova values
                    strcpy(dstate->controller, "DG_NOVA");
                    strcpy(dstate->imageName, "imageName");
                    strcpy(dstate->imageDescription, "image read from a disk connected to the Tester");
                    strcpy(dstate->imageDate, "1/1/2025");
                    dstate->bitRate = 1440000;
                    dstate->preamble1Length = 187;
                    dstate->preamble2Length = 101;
                    dstate->bit_times_data_bits_after_start = 4182;
                    //dstate->bit_times_data_bits_after_start = 4192;
                    dstate->postambleLength = 72;
                    dstate->numberOfCylinders = 203;
                    dstate->numberOfSectorsPerTrack = 12;
                    dstate->numberOfHeads = 2;
                    dstate->microsecondsPerSector = 3333;
                    dstate->rk11d = false;
                    update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
            else{
                if(extract_argc == 3){
                    // 3rd parameter is the DPD filename
                    if(open_and_read_drive_parameters(dstate, extract_argv[2]) != FR_OK){
                        printf("*** ERROR, could not open and read DPD file\r\n");
                    }
                    else
                        update_fpga_disk_state(dstate);
                }
                else
                    printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
            }
        }
        else if(strcmp((char *) "ITEST", extract_argv[1])==0){
            if(extract_argc == 3){
                if((strcmp((char *) "SCANINPUTS", extract_argv[2])==0) || (strcmp((char *) "SCANI", extract_argv[2])==0) || (strcmp((char *) "I", extract_argv[2])==0)){
                    set_interface_test_mode(dstate); // cannot come back from Interface Test Mode
                    scan_inputs_test(dstate);
                }
                else if((strcmp((char *) "SCANOUTPUTS", extract_argv[2])==0) || (strcmp((char *) "SCANO", extract_argv[2])==0) || (strcmp((char *) "O", extract_argv[2])==0)){
                    set_interface_test_mode(dstate); // cannot come back from Interface Test Mode
                    scan_outputs_test(dstate);
                }
                else if((strcmp((char *) "ADDRESS", extract_argv[2])==0) || (strcmp((char *) "ADDR", extract_argv[2])==0) || (strcmp((char *) "A", extract_argv[2])==0)){
                    set_interface_test_mode(dstate); // cannot come back from Interface Test Mode
                    addr_switch_test();
                }
                else if((strcmp((char *) "ROCKER", extract_argv[2])==0) || (strcmp((char *) "ROCK", extract_argv[2])==0) || (strcmp((char *) "R", extract_argv[2])==0)){
                    set_interface_test_mode(dstate); // cannot come back from Interface Test Mode
                    rocker_switch_test();
                }
                else if((strcmp((char *) "LEDTEST", extract_argv[2])==0) || (strcmp((char *) "LED", extract_argv[2])==0) || (strcmp((char *) "L", extract_argv[2])==0)){
                    set_interface_test_mode(dstate); // cannot come back from Interface Test Mode
                    led_test();
                }
                else
                    printf("### ERROR, invalid field3 \"%s\"\r\n", extract_argv[2]);
            }
            else
                printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        }
        else
            printf("### ERROR, invalid field2 \"%s\"\r\n", extract_argv[1]);
    }
    else if(strcmp((char *) "?", extract_argv[0])==0){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else {
            printf("  ADDR [A] <digit 0-7> or LOOP [L]\r\n  MAKELIST [M] CYL or CHS\r\n");
            printf("  DISPLAY [DISP] CYL or CHS or STATUS [S]\r\n");
            printf("  SEEK [S] LOOP [L] or LOOPN [LN] or STEP [S]\r\n");
            printf("  READ [R] LOOP [L] or SECTOR [S]\r\n");
            printf("  WRITE [W] LOOP [L] NOVERIFY [N] or VERIFY [V] or ZERONOVERIFY [ZN] or ZEROVERIFY [ZV] or RANDOM\r\n");
            printf("  WRITE [W] ONCE [O] INITIALIZE [INIT] or INITADDR [IA] or SECTOR [S]\r\n");
            printf("  DISK WRITE <filename>\r\n  DISK READ <filename>\r\n");
            printf("  DIRECTORY [DIR]\r\n");
            printf("  REGISTER <register address> <register data>\r\n");
            printf("  MODE [MD] RK11D ON [1] or OFF [0]\r\n  MODE [MD] WTPROT [WP] ON [1] or OFF [0]\r\n");
            printf("  MODE [MD] CONTROLLER [CONT] RK8E or RK11D or RK11E or ALTO\r\n");
            printf("  MODE [MD] ITEST SCANINPUTS [I] or SCANOUTPUTS [O] or ADDRESS [ADDR] [A] or ROCKER [ROCK] [R] or LEDTEST [LED] [L] or DOORTEST [DOOR] [M]\r\n");
            printf("  BOARDVERSION [BV] <emulator board version>\r\n");
            printf("  RAMTEST [MEMTEST] <hex starting address> <hex number of bytes (2000000 max)>\r\n");
        }
    }
    else if((strcmp((char *) "BOARDVERSION", extract_argv[0])==0) || (strcmp((char *) "BV", extract_argv[0])==0)){
        if(extract_argc != 2)
            printf("### ERROR, %d fields entered, should be 2 fields\r\n", extract_argc);
        else if((strlen(extract_argv[1])==1) && (strcmp(extract_argv[1], (char *) "0") >= 0) && (strcmp(extract_argv[1], (char *) "2") <= 0)){
            sscanf(extract_argv[1], "%d", &p2_numeric);
            if((p2_numeric >= 0) && (p2_numeric <= 7)){
                dstate->drive_board_version = p2_numeric;
                printf(" Emulator Drive Board Version = %x\r\n", dstate->drive_board_version);
            }
        }
        else
            printf("### ERROR, invalid field2 \"%s\", or invalid Drive Board Version, must be 0 to 2\r\n", extract_argv[1]);
    }
    else if((strcmp((char *) "DIRECTORY", extract_argv[0])==0) || (strcmp((char *) "DIR", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else {
            display_directory();
        }
    }
    else if((strcmp((char *) "RAMTEST", extract_argv[0])==0) || (strcmp((char *) "MEMTEST", extract_argv[0])==0)){
        if(extract_argc != 3)
            printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        else{
            sscanf(extract_argv[1], "%x", &p2_numeric);
            sscanf(extract_argv[2], "%x", &p3_numeric);
            ramtest(p2_numeric, p3_numeric);
        }
    }
    else
        printf("### ERROR, invalid command, field1 \"%s\" not recognized\r\n", extract_argv[0]);
}
