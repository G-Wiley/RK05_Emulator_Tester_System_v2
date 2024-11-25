// *********************************************************************************
// emulator_command.cpp
//  implementation of command parsing and execution of commands that apply to the
//  emulator Interface Test Mode
// 
// *********************************************************************************
// 
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>

#include "disk_state_definitions.h"
//#include "display_functions.h"
//#include "display_timers.h"
#include "emulator_hardware.h"
#include "microsd_file_ops.h"
#include "display_functions.h"

#include "emulator_global.h"

#define SEEK_TIMEOUT 1000
#define READWRITE_TIMEOUT 1000

// shift_prbs function shifts a 16-order PRBS register by one state
void shift_prbs(int* prbs){
    int counter; // a counter is a fast and easy way to calculate the parity of the taps
    counter = 0;
    // x^16 + x^14 + x^13 + x^11 + 1
    //if((*prbs & 0x1) != 0) counter++;
    //if((*prbs & 0x4) != 0) counter++;
    //if((*prbs & 0x8) != 0) counter++;
    //if((*prbs & 0x20) != 0) counter++;
    //*prbs = *prbs >> 1;
    // inverted feedback, if parity of the taps is even then shift a 1 into the MSB
    //if((counter & 1) == 0) *prbs = *prbs | 0x8000;

    // x^24 + x^23 + x^22 + x^4 + 1
    if((*prbs & 0x1) != 0) counter++;
    if((*prbs & 0x2) != 0) counter++;
    if((*prbs & 0x4) != 0) counter++;
    if((*prbs & 0x080000) != 0) counter++;
    *prbs = *prbs >> 1;
    // inverted feedback, if parity of the taps is even then shift a 1 into the MSB
    if((counter & 1) == 0) *prbs = *prbs | 0x800000;
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
#define CYCLE_TIMEOUT 400

void scan_inputs_test(Disk_State* dstate){
    int toggle_timer;
    int step_count = 0;
    int ipin, p_ipin;
    int ref, readval;
    int error_count, sample_count;

    printf("  Scan Bus Inputs test. Hit any key to stop the test loop.\r\n");

    // BUS_SEL_DR_3_L is the clock to sample the other signals. Confirm that it's toggling
    ipin = ((read_test_inputs() & 0x80000) == 0) ? 0 : 1;
    p_ipin = ipin;
    //printf("ipin=%d, p_ipin=%d\r\n", ipin, p_ipin);
    for(toggle_timer = 0; toggle_timer < TOGGLE_TIMEOUT; toggle_timer++){ // change in BUS_SEL_DR_3_L
        p_ipin = ipin;
        ipin = ((read_test_inputs() & 0x80000) == 0) ? 0 : 1;
        //if(((ipin == 1) && (p_ipin == 0)) || ((ipin == 0) && (p_ipin == 1)))
        if(ipin != p_ipin)
            break;
        sleep_ms(1); 
    }
    //printf("ipin=%d, p_ipin=%d\r\n", ipin, p_ipin);
    //printf("  toggle_timer = %d\r\n", toggle_timer);
    if(toggle_timer >= TOGGLE_TIMEOUT){ // if BUS_SEL_DR_3_L not toggling then print error and abort
        if(ipin == 1)
            printf("### ERROR, \"BUS_SEL_DR_3_L\" is stuck high\r\n");
        else
            printf("### ERROR, \"BUS_SEL_DR_3_L\" is stuck low\r\n");
        return;
    }
    printf("  signal \"BUS_SEL_DR_3_L\" is toggling\r\n");

    // BUS_CYL_ADD_0_L resets the sequence to verify the other signals. Confirm that it's toggling
    ipin = ((read_test_inputs() & 0x00001) == 0) ? 0 : 1;
    p_ipin = ipin;
    for(toggle_timer = 0; toggle_timer < CYCLE_TIMEOUT; toggle_timer++){ // change in BUS_CYL_ADD_0_L
        p_ipin = ipin;
        ipin = ((read_test_inputs() & 0x00001) == 0) ? 0 : 1;
        if((ipin == 0) && (p_ipin == 1)){
            step_count = 0;
            break;
        }
        sleep_ms(1); 
    }
    //printf("  toggle_timer = %d\r\n", toggle_timer);
    if(toggle_timer >= CYCLE_TIMEOUT){ // if BUS_CYL_ADD_0_L not toggling then print error and abort
        if(ipin == 1)
            printf("### ERROR, \"BUS_CYL_ADD_0_L\" is stuck high\r\n");
        else
            printf("### ERROR, \"BUS_CYL_ADD_0_L\" is stuck low\r\n");
        return;
    }
    printf("  signal \"BUS_CYL_ADD_0_L\" is toggling\r\n");

    // loop until a key is pressed
    error_count = sample_count = 0;
    //p_ipin = ipin = ((read_test_inputs() & 0x80000) == 0) ? 0 : 1;
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
            switch(dstate->Board_version){
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
                    printf("## ERROR, invalid dstate->drive_board_version, %d\r\n", dstate->Board_version);
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
        assert_outputs(step_count);
        step_count++;
        switch(dstate->FPGA_version){
            case 0:
                if(step_count >= 16)
                    step_count = 0;
                break;
            case 1:
                if(step_count >= 20)
                    step_count = 0;
                break;
            case 2:
                if(dstate->Board_version >= 2){
                    if(step_count >= 22)
                        step_count = 0;
                }
                else {
                    if(step_count >= 20)
                        step_count = 0;
                }
                break;
            default:
                printf("### Illegal FPGA Version: %d\r\n", dstate->FPGA_version);
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
            printf("  Ending the Address Switch Test\r\n");
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
            printf("  Ending the Rocker Switch Test\r\n");
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

void door_test(){
    printf("  microSD drive door test. Hit any key to stop the test loop.\r\n");
    printf("  Toggle the RUN/LOAD switch to change the door position.\r\n");
    // loop until a key is pressed
    while(true){
        // a test for keyboard key hit to abort the loop
        // callback code
        if(char_from_callback != 0){
            printf("  Ending the Door Test\r\n");
            char_from_callback = 0; //reset the value
            return;
        }
        if(read_load_switch()){
            set_cpu_ready_indicator();
            clear_cpu_load_indicator();
            open_drive_door();
        }
        else{
            set_cpu_load_indicator();
            clear_cpu_ready_indicator();
            close_drive_door();
        }
        drive_door_status();
        sleep_ms(100);
    }
}

void card_directory(){
    printf("  Card Directory test.\r\n");
    printf("  card_directory() not yet implemented\r\n");
}

void vsense_test(){
    printf("  Voltage Threshold/DC Low test.\r\n");
    printf("  vsense_test() not yet implemented\r\n");
}

void ramtest(int start_address, int num_bytes){
    int prbs_reg;
    int bytecount, i;
    int byte_errors;

    byte_errors = 0;
    prbs_reg = 0x12345678; // initialize PRBS register starting seed value
    // fixed seed
    printf("  Ramtest.\r\n");
    printf("  Filling the entire test range with a pseudorandom pattern.\r\n");
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        shift_prbs31(&prbs_reg); // shift 1 time
        storebyte(prbs_reg & 0xff);
    }

    printf("  Reading back and checking the test range.\r\n");
    prbs_reg = 0x12345678; // re-initialize PRBS register starting seed value
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        shift_prbs31(&prbs_reg); // shift 1 time
        int tempval = readbyte();
        if(tempval != (prbs_reg & 0xff)){
            printf("Error, address = %x, ideal = %x, readback = %x\r\n", start_address + bytecount, prbs_reg & 0xff, tempval);
            byte_errors++;
        }
    }

    prbs_reg = 0x12345678; // initialize PRBS register starting seed value
    // fixed seed
    printf("  Filling the entire test range with an inverted pseudorandom pattern.\r\n");
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        shift_prbs31(&prbs_reg); // shift 1 time
        storebyte(~prbs_reg & 0xff);
    }

    printf("  Reading back the inverted data and checking the test range.\r\n");
    prbs_reg = 0x12345678; // re-initialize PRBS register starting seed value
    load_ram_address(start_address);
    for(bytecount = 0; bytecount < num_bytes; bytecount++){
        shift_prbs31(&prbs_reg); // shift 1 time
        int tempval = readbyte();
        if(tempval != (~prbs_reg & 0xff)){
            printf("Error, address = %x, ideal = %x, readback = %x\r\n", start_address + bytecount, ~prbs_reg & 0xff, tempval);
            byte_errors++;
        }
    }
    printf(" Test complete. Byte error count = %d\r\n", byte_errors);
}

void command_parse_and_dispatch (Disk_State* dstate)
{
    int p1_numeric, p2_numeric, p3_numeric;
    int tempval;

    if(extract_argc == 0){ // don't process commands if the number of command fields is zero
        printf("no commands\r\n");
        return;
    }

    if((strcmp((char *) "SCANINPUTS", extract_argv[0])==0) || (strcmp((char *) "SCANI", extract_argv[0])==0) || (strcmp((char *) "I", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            scan_inputs_test(dstate);
        }
    }
    else if((strcmp((char *) "SCANOUTPUTS", extract_argv[0])==0) || (strcmp((char *) "SCANO", extract_argv[0])==0) || (strcmp((char *) "O", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            scan_outputs_test(dstate);
        }
    }
    else if((strcmp((char *) "ADDRESS", extract_argv[0])==0) || (strcmp((char *) "ADDR", extract_argv[0])==0) || (strcmp((char *) "A", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            addr_switch_test();
        }
    }
    else if((strcmp((char *) "ROCKER", extract_argv[0])==0) || (strcmp((char *) "ROCK", extract_argv[0])==0) || (strcmp((char *) "R", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            rocker_switch_test();
        }
    }
    else if((strcmp((char *) "LEDTEST", extract_argv[0])==0) || (strcmp((char *) "LED", extract_argv[0])==0) || (strcmp((char *) "L", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            led_test();
        }
    }
    else if((strcmp((char *) "DOORTEST", extract_argv[0])==0) || (strcmp((char *) "DOOR", extract_argv[0])==0) || (strcmp((char *) "M", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            door_test();
        }
    }
    else if((strcmp((char *) "DIRECTORY", extract_argv[0])==0) || (strcmp((char *) "DIR", extract_argv[0])==0) || (strcmp((char *) "D", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            card_directory();
        }
    }
    else if((strcmp((char *) "VSENSE", extract_argv[0])==0) || (strcmp((char *) "DCLOW", extract_argv[0])==0) || (strcmp((char *) "V", extract_argv[0])==0)){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else{
            vsense_test();
        }
    }
    //else if((strcmp((char *) "REGISTER", extract_argv[0])==0) || (strcmp((char *) "REG", extract_argv[0])==0)){
        //if(extract_argc != 3)
            //printf("### ERROR, %d fields entered, should be 3 fields\r\n", extract_argc);
        //else{
            //sscanf(extract_argv[1], "%x", &p2_numeric);
            //sscanf(extract_argv[2], "%x", &p3_numeric);
            //tempval = read_write_spi_register(p2_numeric & 0xff, p3_numeric & 0xff);
            //if(p2_numeric >= 0x80)
                //printf("  read reg 0x%x -> 0x%x\r\n", p2_numeric, tempval);
            //else
                //printf("  write reg 0x%x <- 0x%x\r\n", p2_numeric, p3_numeric);
        //}
    //}
    else if(strcmp((char *) "?", extract_argv[0])==0){
        if(extract_argc != 1)
            printf("### ERROR, %d fields entered, should be 1 field\r\n", extract_argc);
        else {
            printf("  SCANINPUTS, SCANI, I\r\n  SCANOUTPUTS, SCANO, O\r\n");
            printf("  ADDRESS, ADDR, A\r\n  ROCKER, ROCK, R\r\n  LEDTEST, LED, L\r\n");
            printf("  DOORTEST, DOOR, M\r\n  DIRECTORY, DIR, D\r\n  VSENSE, DCLOW, V\r\n");
            printf("  RAMTEST, MEMTEST <hex start address> <hex number of bytes>\r\n");
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

void emulator_command_mode(Disk_State* dstate){
    int i;

    read_rocker_switches(dstate);
    printf("emulator-testmode>");
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
    printf("\r\n");
    //printf("\r\n  %d chr, [%s], ", i, inputdata);
    extract_command_fields(inputdata);
    //printf("  extract_argc = %d\r\n", extract_argc); // debug/test code to view the extracted input line
    //for(i=0; i<25; i++){
        //if(inputdata[i] >= ' ')
            //printf("    %2d - %2x \'%c\'\r\n", i, inputdata[i], inputdata[i]);
        //else
            //printf("    %2d - %2x\r\n", i, inputdata[i]);
    //} // end of debug/test code
    command_parse_and_dispatch (dstate);
    sleep_ms(100); // might remove this later or insert it in the command entry loop
}