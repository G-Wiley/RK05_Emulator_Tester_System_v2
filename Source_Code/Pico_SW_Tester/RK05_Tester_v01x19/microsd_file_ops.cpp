// *********************************************************************************
// microsd_file_ops.cpp
//   file operations to read and write headers and RK05 disk image data
// *********************************************************************************
// 
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>

#include "ff.h" /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */
#include "sd_card.h"
#include "hw_config.h"

#include "disk_state_definitions.h"
//#include "display_functions.h"
//#include "tester_state_definitions.h"  // commented-out 2/5/2025
#include "tester_hardware.h"
//#include "microsd_file_ops.h" // commented-out 2/7/2025
#include "dpd_definitions.h"

// max sector for 4 sector pack, 2% speed variation, ((40 msec / 4 sectors) * 1.6e6 bits/sec * 1.02) / 8 bits/byte + 4 bytes length fields = 2044
#define MAX_SECTOR_SIZE 2044 
//#define FILE_OPS_OKAY   0
//#define FILE_OPS_ERROR  1

static FATFS fs;
static FIL fil;
static int ret;

//const char configfilename[] = "config.txt";
static char diskimagefilename[FF_LFN_BUF + 1] = "";
static uint8_t sectordata[MAX_SECTOR_SIZE];

/* Search a directory for objects and display it */

static void force_unmount()
{
    f_unmount("0:");

    // Force SD card reinitialization.
    sd_card_t *pSD = sd_get_by_num(0);
    pSD->m_Status |= STA_NOINIT;
}

bool file_init_and_mount()
{
    if (!sd_init_driver()){
        printf("*** ERROR, could not initialize microSD card\r\n");
        return(false);
    }
    return(true);
}

void debug_print_string_hex(char* str, int length){
    int i;

    printf("  ");
    for(i=0; i<length; i++){
        printf("%2x ", str[i]);
    }
    printf("\r\n");
}

void display_directory (void)
{
    FRESULT fr;     // Return value
    DIR dj;         // Directory object
    FILINFO fno;    // File information
    int filecount;  // count the number of files on the card

    printf("  microSD Card Directory\r\n------------------------\r\n");
    if ((fr = f_mount(&fs, "0:", 1)) != FR_OK){
        printf("*** ERROR, could not mount filesystem\r\n");
        return;
    }

    fr = f_findfirst(&dj, &fno, "", "?*"); /* Start to search for photo files */
    filecount = 0;
    printf("------------------------\r\n");

    while ((fr == FR_OK) && (fno.fname[0] != '\0')) {            // Repeat while an item is found
        if(strcmp(fno.fname, "System Volume Information") != 0){ // skip the System Volume Information folder
            filecount++;
            printf("  %s\n", fno.fname);          // Print the object name
        }
        fr = f_findnext(&dj, &fno);               /* Search for next item */
    }
    if(filecount == 1)
        printf("     %d file\r\n", filecount);
    else
        printf("     %d files\r\n", filecount);

    f_closedir(&dj);
    force_unmount();
}

FRESULT file_open_read_disk_image_n(char* filename)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    printf("  microSD file_open_read_disk_image_n\r\n");
    if ((fr = f_mount(&fs, "0:", 1)) != FR_OK){
        printf("*** ERROR, could not mount microSD filesystem before open for read (%d)\r\n", fr);
        return(fr);
    }

    // Save the file name
    strcpy(diskimagefilename, filename);

    if ((fr = f_open(&fil, diskimagefilename, FA_READ))!= FR_OK){
        printf("*** ERROR, could not open microSD disk image file for read (%d)\r\n", fr);
        force_unmount();
        return(fr);
    }
    return(FR_OK);
}

FRESULT file_open_write_disk_image_n(char* filename)
{
    FRESULT fr;
    printf("  microSD file_open_write_disk_image_n '%s'\r\n", filename);
    if ((fr = f_mount(&fs, "0:", 1)) != FR_OK){
        printf("*** ERROR, could not mount filesystem before open for write (%d)\r\n", fr);
        return(fr);
    }

    strcpy(diskimagefilename, filename);
    printf("filename = %s\r\n", diskimagefilename);
    if((fr = f_open(&fil, diskimagefilename, FA_WRITE | FA_CREATE_ALWAYS))!= FR_OK){
        printf("*** ERROR, could not open microSD disk image file for write (%d)\r\n", fr);
        force_unmount();
        return(fr);
    }
    return(FR_OK);
}

FRESULT file_close_disk_image()
{
    // Close file
    FRESULT fr;
    fr = f_close(&fil);
    if (fr != FR_OK) {
        printf("ERROR: Could not close microSD file (%d)\r\n", fr);
        microSD_LED_off();
        return(fr);
    }
    //unmount the drive
    if ((fr = f_unmount("0:")) != FR_OK){
        printf("*** ERROR, could not unmount microSD filesystem (%d)\r\n", fr);
        microSD_LED_off();
        return(fr);
    }

    // Force SD card reinitialization in case it has been swapped or removed and the volume is remounted.
    sd_card_t *pSD = sd_get_by_num(0);
    pSD->m_Status |= STA_NOINIT;

    return(FR_OK);
}

FRESULT dpd_gets(char* dest_str, UINT str_length, char* readbuffer, UINT* str_index, UINT bytesread)
{
    int i;
    FRESULT fr;     // Return value

    for(i=0; i<str_length; i++){
        dest_str[i] = readbuffer[*str_index];
        if(*str_index >= (bytesread - 1)){ // if the index is past the end of the read buffer then put a null there
            dest_str[i] = '\0';
            printf("*** ERROR, past end of data in microSD input buffer, index (%d), bytesread (%d)\r\n", *str_index, bytesread);
            return(FR_INVALID_PARAMETER);
        }
        if(dest_str[i] == '\r'){ // if the character is CR put a null there
            dest_str[i] = '\0';
            *str_index += 2;     // move the index past the CR and LF
            break;
        }
        *str_index += 1;
    }
    if(i == str_length) // if the destination string is completely filled, then put a null at the end
        dest_str[str_length - 1] = '\0';
    return(FR_OK);
}

FRESULT open_and_read_drive_parameters(Disk_State* dstate, char* filename)
{
    FRESULT fr;     // Return value
    char DPDmagicNumber[DPDmagicLength];
    char DPDversionNumber[DPDversionLength];
    #define READBUFSIZE 700
    char readbuffer[READBUFSIZE];
    #define TEMPBUFSIZE 20
    char tempbuffer[TEMPBUFSIZE];
    UINT char_index;
    UINT bytes_read;
    int temp_result;

    if((fr = file_open_read_disk_image_n(filename)) != FR_OK){
        printf("*** ERROR, could not mount microSD filesystem and open file before reading (%d)\r\n", fr);
        return(fr);
    }
    if(fr = f_read(&fil, readbuffer, READBUFSIZE, &bytes_read)){
        printf("*** ERROR reading DPD file (%d)\r\n", fr);
        return(fr);
    }
    if(bytes_read >= READBUFSIZE){
        printf("*** ERROR DPD file size is larger than the read buffer\r\n");
        return(FR_INVALID_PARAMETER);
    }

    char_index = 0;
    if((fr = dpd_gets(DPDmagicNumber, DPDmagicLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    debug_print_string_hex(DPDmagicNumber, DPDmagicLength);
    if(strcmp("DPD/25$q", DPDmagicNumber) != 0){
        printf("*** ERROR, DPD file magic number is incorrect (%s)\r\n", DPDmagicNumber);
        return(FR_INVALID_PARAMETER);
    }
    if((fr = dpd_gets(DPDversionNumber, DPDversionLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    //debug_print_string_hex(DPDversionNumber, DPDversionLength);
    if((fr = dpd_gets(&dstate->imageName[0], ImageNameLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    if((fr = dpd_gets(&dstate->imageDescription[0], ImageDescriptionLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    if((fr = dpd_gets(&dstate->imageDate[0], ImageDateLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    if((fr = dpd_gets(&dstate->controller[0], ControllerNameLength, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);

    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    //debug_print_string_hex(tempbuffer, TEMPBUFSIZE);
    sscanf(tempbuffer, "%d", &dstate->bitRate);
    //printf("%d\r\n", dstate->bitRate);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->numberOfCylinders);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->numberOfSectorsPerTrack);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->numberOfHeads);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->microsecondsPerSector);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->preamble1Length);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->preamble2Length);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->bit_times_data_bits_after_start);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &dstate->postambleLength);
    if((fr = dpd_gets(tempbuffer, TEMPBUFSIZE, readbuffer, &char_index, bytes_read)) != FR_OK)
        return(fr);
    sscanf(tempbuffer, "%d", &temp_result);
    dstate->rk11d = (temp_result == 0) ? false : true;

    if((fr = file_close_disk_image()) != FR_OK){
        printf("*** ERROR, could not close file and unmount microSD filesystem after reading (%d)\r\n", fr);
        return(fr);
    }
    return(FR_OK);
}

bool deserialize_int(int *vp)
{
    FRESULT fr;
    UINT nr;
    uint8_t buf[4];
    int value = 0;

    fr = f_read(&fil, buf, 4, &nr);
    if (fr != FR_OK || nr != 4) {
        printf("###ERROR, microSD Header data read error fr=%d, nr=%u\r\n", fr, nr);
        return(false);
    }

    value  = buf[0] << 24;
    value |= buf[1] << 16;
    value |= buf[2] <<  8;
    value |= buf[3] <<  0;

    *vp = value;

    return(true);
}

bool serialize_int(int value)
{
    FRESULT fr;
    UINT nw;
    uint8_t buf[4];

    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >>  8) & 0xFF;
    buf[3] = (value >>  0) & 0xFF;

    fr = f_write(&fil, buf, 4, &nw);
    if (fr != FR_OK || nw != 4) {
        printf("###ERROR, microSD Header data write error fr=%d, nw=%u\r\n", fr, nw);
        return(false);
    }

    return(true);
}

bool deserialize_string(char *cp, int size)
{
    FRESULT fr;
    UINT nr;
    int value = 0;

    fr = f_read(&fil, cp, size, &nr);
    if (fr != FR_OK || nr != size) {
        printf("###ERROR, microSD Header data read error fr=%d, nr=%u\r\n", fr, nr);
        return(false);
    }

    return(true);
}

#define MAX_SBUF 256
bool serialize_string(char *cp, int size)
{
    FRESULT fr;
    UINT nw;
    static char buf[MAX_SBUF];

    if (size > MAX_SBUF) {
        printf("###ERROR, microSD Header string too long: %d (MAX=%d)\r\n", size, MAX_SBUF);
        return(false);
    }

    // pad string with zeroes and enforce zero terminator.
    strncpy(buf, cp, size - 1);
    buf[size - 1] = '\0';

    fr = f_write(&fil, buf, size, &nw);
    if (fr != FR_OK || nw != size) {
        printf("###ERROR, microSD Header data write error fr=%d, nw=%u\r\n", fr, nw);
        return(false);
    }

    return(true);
}

static char magicNumber[10] = "\x89RK05\r\n\x1A"; 
static char versionNumber[4] = "1.1";

int read_image_file_header(struct Disk_State* dstate)
{
    bool rc;
    static char tmp[10];

    printf("  Reading header from file '%s'\r\n", diskimagefilename);

    if (!deserialize_string(tmp, sizeof(magicNumber)) || strncmp(tmp, magicNumber, sizeof(magicNumber)) != false) {
        // invalid magic
        printf("###ERROR, invalid magic number in microSD image file header\r\n");
        return 2;
    }

    if (!deserialize_string(tmp, sizeof(versionNumber)) || strncmp(tmp, versionNumber, sizeof(versionNumber)) != false) {
        // unexpected version
        printf("###ERROR, invalid Version Number in microSD image file header\r\n");
        return 3;
    }


    rc =       deserialize_string(dstate->imageName, sizeof(dstate->imageName));
    rc = rc && deserialize_string(dstate->imageDescription, sizeof(dstate->imageDescription));
    rc = rc && deserialize_string(dstate->imageDate, sizeof(dstate->imageDate));
    rc = rc && deserialize_string(dstate->controller, sizeof(dstate->controller));
    rc = rc && deserialize_int(&dstate->bitRate);
    rc = rc && deserialize_int(&dstate->numberOfCylinders);       
    rc = rc && deserialize_int(&dstate->numberOfSectorsPerTrack); 
    rc = rc && deserialize_int(&dstate->numberOfHeads);           
    rc = rc && deserialize_int(&dstate->microsecondsPerSector);

    if (rc) {
        printf("controller = %s\r\n", dstate->controller);
        printf("bitRate = %d\r\n", dstate->bitRate);
        printf("numberOfCylinders = %d\r\n", dstate->numberOfCylinders);
        printf("numberOfSectorsPerTrack = %d\r\n", dstate->numberOfSectorsPerTrack);
        printf("numberOfHeads = %d\r\n", dstate->numberOfHeads);
        printf("microsecondsPerSector = %d\r\n", dstate->microsecondsPerSector);

        // write the data read from the header into the FPGA registers
        update_fpga_disk_state(dstate);
        return 0;
    }

    return 1;
}

int write_image_file_header(struct Disk_State* dstate)
{
    bool rc;

    printf("  Writing header to microSD file '%s'\r\n", diskimagefilename);

    rc =       serialize_string(magicNumber, sizeof(magicNumber));
    rc = rc && serialize_string(versionNumber, sizeof(versionNumber));
    rc = rc && serialize_string(dstate->imageName, sizeof(dstate->imageName));
    rc = rc && serialize_string(dstate->imageDescription, sizeof(dstate->imageDescription));
    rc = rc && serialize_string(dstate->imageDate, sizeof(dstate->imageDate));
    rc = rc && serialize_string(dstate->controller, sizeof(dstate->controller));
    rc = rc && serialize_int(dstate->bitRate);
    rc = rc && serialize_int(dstate->numberOfCylinders);       
    rc = rc && serialize_int(dstate->numberOfSectorsPerTrack); 
    rc = rc && serialize_int(dstate->numberOfHeads);           
    rc = rc && serialize_int(dstate->microsecondsPerSector);   

    return rc ? 0 : 1;

}

FRESULT read_disk_image_data_tester(struct Disk_State* dstate)
{
    FRESULT fr;
    UINT nr;
    uint8_t *bp;
    int bytecount;
    int sectorcount;
    int headcount;
    int cylindercount;
    int ramaddress;

    printf("  Reading disk data from microSD file '%s'\r\n", diskimagefilename);
    printf("  %s\r\n", dstate->controller);
    printf("  cylinders=%d, heads=%d, sectors=%d\r\n", dstate->numberOfCylinders, dstate->numberOfHeads, dstate->numberOfSectorsPerTrack);
    for (cylindercount = 0; cylindercount < dstate->numberOfCylinders; cylindercount++){
        if ((cylindercount % 20) == 0)
            printf("    cylindercount = %d\r\n", cylindercount);
        for (headcount = 0; headcount < dstate->numberOfHeads; headcount++){
            for( sectorcount = 0; sectorcount < dstate->numberOfSectorsPerTrack; sectorcount++){
                // first read the two parameters:
                //   1. Bit times from sector pulse to start bit (16-bit value)
                //   2. Number of data bits after the start bit (16-bit value)
                bytecount = 4;
                fr = f_read(&fil, sectordata, bytecount, &nr);
                if (fr != FR_OK) {
                    printf("###ERROR, Image data read error fr=%d, nr=%u\r\n", fr, nr);
                    microSD_LED_off();
                    return(fr);
                }
                if (nr != bytecount) {
                    printf("###ERROR, Image data read error, number read != bytecount, fr=%d, nr=%u\r\n", fr, nr);
                    microSD_LED_off();
                    return(FR_INVALID_PARAMETER);
                }

                // compute the DRAM address of this sector and load it into the hardware address counter
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylindercount, headcount, sectorcount);
                load_ram_address(ramaddress);

                // compute the byte count based on "Number of data bits after the start bit"
                int sector_data_bit_count = (sectordata[3] << 8) | sectordata[2];
                int wordcount = (sector_data_bit_count + 15) >> 4; // round the word count up to the next integer value
                bytecount = wordcount * 2;
                //printf("  bc=%d\r\n", bytecount);

                bp = sectordata;
                fr = f_read(&fil, sectordata, bytecount, &nr);
                if (fr != FR_OK) {
                    printf("###ERROR, Image data read error fr=%d, nr=%u\r\n", fr, nr);
                    microSD_LED_off();
                    return(fr);
                }
                if (nr != bytecount) {
                    printf("###ERROR, Image data read error, number read != bytecount, fr=%d, nr=%u\r\n", fr, nr);
                    microSD_LED_off();
                    return(FR_INVALID_PARAMETER);
                }

                gpio_put(22, 1); // for debugging to time the loop
                bp = sectordata;
                for (int i = 0; i < bytecount; i++){
                    storebyte(*bp++);
                }
                gpio_put(22, 0); // for debugging to time the loop

                if(sector_data_bit_count < dstate->bit_times_data_bits_after_start){
                    printf("## short sector read from microSD, C=%d, H=%d, S=%d, bits=%d\r\n", 
                        cylindercount, headcount, sectorcount, sector_data_bit_count);
                    // pad the data following the short sector with all-zero data
                    int ideal_bytecount = ((dstate->bit_times_data_bits_after_start + 15) >> 4) * 2;
                    if(bytecount < ideal_bytecount){
                        for (int i = 0; i < (ideal_bytecount - bytecount); i++){
                            storebyte(0);
                        }
                    }
                }
            }
        }
    }

    return(FR_OK);
}

FRESULT write_disk_image_data(struct Disk_State* dstate)
{
    FRESULT fr;
    UINT nw;
    uint8_t *bp;
    int bytecount;
    int sectorcount;
    int headcount;
    int cylindercount;
    int ramaddress;

    printf("  Writing disk image data to microSD file '%s':\r\n", diskimagefilename);
    printf("  cylinders=%d, heads=%d, sectors=%d\r\n", dstate->numberOfCylinders, dstate->numberOfHeads, dstate->numberOfSectorsPerTrack);
    for (cylindercount = 0; cylindercount < dstate->numberOfCylinders; cylindercount++){
        if ((cylindercount % 20) == 0)
            printf("    cylinder = %d\r\n", cylindercount);
        for (headcount = 0; headcount < dstate->numberOfHeads; headcount++){
            for( sectorcount = 0; sectorcount < dstate->numberOfSectorsPerTrack; sectorcount++){
                ramaddress = compute_ram_address(dstate->numberOfSectorsPerTrack, cylindercount, headcount, sectorcount);
                load_ram_address(ramaddress);

                gpio_put(22, 1); // for debugging to time the loop

                // pre-set the two 16-bit values at the beginning of the sector data
                int bit_times_sector_to_start = dstate->preamble1Length + dstate->preamble2Length;
                sectordata[0] =  bit_times_sector_to_start & 0xff;
                sectordata[1] = (bit_times_sector_to_start >> 8) & 0xff;
                sectordata[2] =  dstate->bit_times_data_bits_after_start & 0xff;
                sectordata[3] = (dstate->bit_times_data_bits_after_start >> 8) & 0xff;
                bp = sectordata + 4; // initialize the byte pointer just past the two 16-bit values
                int wordcount = (dstate->bit_times_data_bits_after_start + 15) >> 4; // round the word count up to the next integer value
                bytecount = wordcount * 2;

                // bp is already pointing to the proper place in the sectordata array
                // copy from the Tester DRAM to the sector data array
                for (int i = 0; i < bytecount; i++){
                    *bp++ = readbyte();
                }
                gpio_put(22, 0); // for debugging to time the loop

                bytecount += 4; // add 4 bytes to account for the two 16-bit length fields
                //printf("C = %d, H = %d, S = %d, bytecount = %d\r\n", cylindercount, headcount, sectorcount, bytecount); // for debugging
                fr = f_write(&fil, sectordata, bytecount, &nw);
                if (fr != FR_OK) {
                    printf("###ERROR, microSD Image data write error fr=%d, nw=%u\r\n", fr, nw);
                    microSD_LED_off();
                    return(fr);
                }
                if (nw != bytecount) {
                    printf("###ERROR, microSD Image data write, number written != bytecount, fr=%d, nw=%u\r\n", fr, nw);
                    microSD_LED_off();
                    return(FR_INVALID_PARAMETER);
                }
            }
        }
    }
    return(FR_OK);
}

