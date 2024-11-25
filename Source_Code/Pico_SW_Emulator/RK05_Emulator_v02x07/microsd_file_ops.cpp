// *********************************************************************************
// microsd_file_ops.cpp
//   file operations to read and write headers and RK05 disk image data
// *********************************************************************************
// 
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>

//#include "hardware/spi.h"
#include "ff.h" /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */
#include "sd_card.h"
#include "ff.h"
#include "hw_config.h"

#include "disk_state_definitions.h"
#include "display_functions.h"
#include "emulator_state_definitions.h"
#include "emulator_hardware.h"

// max sector for 4 sector pack, 2% speed variation, ((40 msec / 4 sectors) * 1.6e6 bits/sec * 1.02) / 8 bits/byte + 4 bytes length fields = 2044
#define MAX_SECTOR_SIZE 2044 
#define FILE_OPS_OKAY   0
#define FILE_OPS_ERROR  1

static FATFS fs;
static FIL fil;
static int ret;

//const char configfilename[] = "config.txt";
static char diskimagefilename[FF_LFN_BUF + 1] = "";
static uint8_t sectordata[MAX_SECTOR_SIZE];  // largest possible sector data is 580 for RK11-E

static void force_unmount()
{
    f_unmount("0:");

    // Force SD card reinitialization.
    sd_card_t *pSD = sd_get_by_num(0);
    pSD->m_Status |= STA_NOINIT;
}

int file_init_and_mount()
{
    FRESULT fr;
    if (!sd_init_driver()){
        //error_code = 2;
        printf("*** ERROR, could not initialize microSD card\r\n");
        display_error((char *) "cannot init", (char *) "microSD card");
        return(100);
    }
    return(FILE_OPS_OKAY);
}

int file_open_read_disk_image()
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    printf("file_open_read_disk_image\r\n");
    if ((fr = f_mount(&fs, "0:", 1)) != FR_OK){
        printf("*** ERROR, could not mount filesystem before open for read (%d)\r\n", fr);
        display_error((char *) "cannot mount", (char *) "filesystem");
        return(fr);
    }

    // Find first disk image file name
    fr = f_findfirst(&dir, &fno, "", "?*.rke");
    f_closedir(&dir);

    if (fr != FR_OK) {
        printf("*** ERROR, no disk image file available (%d)\r\n", fr);
        display_error((char *) "no disk", (char *) "image found");
        force_unmount();
        return(fr);
    }

    // Save the file name
    strncpy(diskimagefilename, fno.fname, FF_LFN_BUF);
    diskimagefilename[FF_LFN_BUF] = '\0';

    if ((fr = f_open(&fil, diskimagefilename, FA_READ))!= FR_OK){
        printf("*** ERROR, could not open disk image file for read (%d)\r\n", fr);
        display_error((char *) "cannot open", (char *) "disk image");
        force_unmount();
        return(fr);
    }
    return(FILE_OPS_OKAY);
}

int file_open_write_disk_image()
{
    FRESULT fr;
    printf("file_open_write_disk_image\r\n");
    if ((fr = f_mount(&fs, "0:", 1)) != FR_OK){
        printf("*** ERROR, could not mount filesystem before open for write (%d)\r\n", fr);
        display_error((char *) "cannot mount", (char *) "filesystem");
        return(fr);
    }

    if((fr = f_open(&fil, diskimagefilename, FA_WRITE | FA_CREATE_ALWAYS))!= FR_OK){
        printf("*** ERROR, could not open disk image file for write (%d)\r\n", fr);
        display_error((char *) "cannot open", (char *) "disk image");
        force_unmount();
        return(fr);
    }
    return(FILE_OPS_OKAY);
}

int file_close_disk_image()
{
    // Close file
    FRESULT fr;
    fr = f_close(&fil);
    if (fr != FR_OK) {
        printf("ERROR: Could not close file (%d)\r\n", fr);
        return(fr);
    }
    //unmount the drive
    if ((fr = f_unmount("0:")) != FR_OK){
        printf("*** ERROR, could not unmount filesystem (%d)\r\n", fr);
        display_error((char *) "can't unmount", (char *) "filesystem");
        return(fr);
    }

    // Force SD card reinitialization in case it has been swapped or removed and the volume is remounted.
    sd_card_t *pSD = sd_get_by_num(0);
    pSD->m_Status |= STA_NOINIT;

    return(FILE_OPS_OKAY);
}

bool deserialize_int(int *vp)
{
    FRESULT fr;
    UINT nr;
    uint8_t buf[4];
    int value = 0;

    fr = f_read(&fil, buf, 4, &nr);
    if (fr != FR_OK || nr != 4) {
        printf("###ERROR, Header data read error fr=%d, nr=%u\r\n", fr, nr);
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
        printf("###ERROR, Header data write error fr=%d, nw=%u\r\n", fr, nw);
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
        printf("###ERROR, Header data read error fr=%d, nr=%u\r\n", fr, nr);
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
        printf("###ERROR, Header string too long: %d (MAX=%d)\r\n", size, MAX_SBUF);
        return(false);
    }

    // pad string with zeroes and enforce zero terminator.
    strncpy(buf, cp, size - 1);
    buf[size - 1] = '\0';

    fr = f_write(&fil, buf, size, &nw);
    if (fr != FR_OK || nw != size) {
        printf("###ERROR, Header data write error fr=%d, nw=%u\r\n", fr, nw);
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

    printf("Reading header from file '%s'\r\n", diskimagefilename);

    if (!deserialize_string(tmp, sizeof(magicNumber)) || strncmp(tmp, magicNumber, sizeof(magicNumber)) != 0) {
        // invalid magic
        return 2;
    }

    if (!deserialize_string(tmp, sizeof(versionNumber)) || strncmp(tmp, versionNumber, sizeof(versionNumber)) != 0) {
        // unexpected version
        return 3;
    }


    rc =       deserialize_string(dstate->imageName, sizeof(dstate->imageName));
    rc = rc && deserialize_string(dstate->imageDescription, sizeof(dstate->imageDescription));
    rc = rc && deserialize_string(dstate->imageDate, sizeof(dstate->imageDate));
    rc = rc && deserialize_string(dstate->controller, sizeof(dstate->controller));
    rc = rc && deserialize_int(&dstate->bitRate);                 
    //rc = rc && deserialize_int(&dstate->preamble1Length);         
    //rc = rc && deserialize_int(&dstate->preamble2Length);         
    //rc = rc && deserialize_int(&dstate->dataLength);              
    //rc = rc && deserialize_int(&dstate->postambleLength);         
    rc = rc && deserialize_int(&dstate->numberOfCylinders);       
    rc = rc && deserialize_int(&dstate->numberOfSectorsPerTrack); 
    rc = rc && deserialize_int(&dstate->numberOfHeads);           
    rc = rc && deserialize_int(&dstate->microsecondsPerSector);

    if (rc) {
        printf("controller = %s\r\n", dstate->controller);
        printf("bitRate = %d\r\n", dstate->bitRate);
        //printf("preamble1Length = %d\r\n", dstate->preamble1Length);
        //printf("preamble2Length = %d\r\n", dstate->preamble2Length);
        //printf("dataLength = %d\r\n", dstate->dataLength);
        //printf("postambleLength = %d\r\n", dstate->postambleLength);
        printf("numberOfCylinders = %d\r\n", dstate->numberOfCylinders);
        printf("numberOfSectorsPerTrack = %d\r\n", dstate->numberOfSectorsPerTrack);
        printf("numberOfHeads = %d\r\n", dstate->numberOfHeads);
        printf("microsecondsPerSector = %d\r\n", dstate->microsecondsPerSector);

        // write the data read from the JSON  header into the FPGA registers
        update_fpga_disk_state(dstate);
        return 0;
    }

    return 1;
}

int write_image_file_header(struct Disk_State* dstate)
{
    bool rc;

    printf("Writing header to file '%s'\r\n", diskimagefilename);

    rc =       serialize_string(magicNumber, sizeof(magicNumber));
    rc = rc && serialize_string(versionNumber, sizeof(versionNumber));
    rc = rc && serialize_string(dstate->imageName, sizeof(dstate->imageName));
    rc = rc && serialize_string(dstate->imageDescription, sizeof(dstate->imageDescription));
    rc = rc && serialize_string(dstate->imageDate, sizeof(dstate->imageDate));
    rc = rc && serialize_string(dstate->controller, sizeof(dstate->controller));
    rc = rc && serialize_int(dstate->bitRate);                 
    //rc = rc && serialize_int(dstate->preamble1Length);         
    //rc = rc && serialize_int(dstate->preamble2Length);         
    //rc = rc && serialize_int(dstate->dataLength);              
    //rc = rc && serialize_int(dstate->postambleLength);         
    rc = rc && serialize_int(dstate->numberOfCylinders);       
    rc = rc && serialize_int(dstate->numberOfSectorsPerTrack); 
    rc = rc && serialize_int(dstate->numberOfHeads);           
    rc = rc && serialize_int(dstate->microsecondsPerSector);   

    return rc ? 0 : 1;

}

int read_disk_image_data(struct Disk_State* dstate)
{
    FRESULT fr;
    UINT nr;
    uint8_t *bp;
    //int bytecount = dstate->dataLength / 8;
    //int bytecount = (dstate->dataLength / 8) + 4 + 6;
    int bytecount;
    int sectorcount;
    int headcount;
    int cylindercount;
    int ramaddress;
    char display_line_2[30];

    printf("Reading disk data from file '%s'\r\n", diskimagefilename);
    printf("  %s\r\n", dstate->controller);
    printf(" cylinders=%d, heads=%d, sectors=%d\r\n", dstate->numberOfCylinders, dstate->numberOfHeads, dstate->numberOfSectorsPerTrack);
    for (cylindercount = 0; cylindercount < dstate->numberOfCylinders; cylindercount++){
        if ((cylindercount % 20) == 0)
            printf("  cylindercount = %d\r\n", cylindercount);
        if ((cylindercount % 10) == 0){
            sprintf(display_line_2," Cyl %d", cylindercount);
            display_status((char *) "Read card", display_line_2);
        }
        for (headcount = 0; headcount < dstate->numberOfHeads; headcount++){
            for( sectorcount = 0; sectorcount < dstate->numberOfSectorsPerTrack; sectorcount++){
                // first read the two parameters:
                //   1. Bit times from sector pulse to start bit (16-bit value)
                //   2. Number of data bits after the start bit (16-bit value)
                bytecount = 4;
                fr = f_read(&fil, sectordata, bytecount, &nr);
                if (fr != FR_OK || nr != bytecount) {
                    printf("###ERROR, Image data read error fr=%d, nr=%u\r\n", fr, nr);
                    return(FILE_OPS_ERROR);
                }

                ramaddress = (cylindercount << 14) | (headcount << 13) | (sectorcount << 9);
                load_ram_address(ramaddress);

                // copy the first four bytes to DRAM and compute the wordcount and bytecount
                //printf("  C=%3d H=%d S=%2d, %2x %2x %2x %2x,", cylindercount, headcount, sectorcount, sectordata[0], sectordata[1], sectordata[2], sectordata[3]);
                bp = sectordata;
                for (int i = 0; i < bytecount; i++){
                    storebyte(*bp++);
                }
                int sector_data_bit_count = (sectordata[3] << 8) | sectordata[2];
                int wordcount = (sector_data_bit_count + 15) >> 4; // round the word count up to the next integer value
                bytecount = wordcount * 2;
                //printf("  bc=%d\r\n", bytecount);

                fr = f_read(&fil, sectordata, bytecount, &nr);
                if (fr != FR_OK || nr != bytecount) {
                    printf("###ERROR, Image data read error fr=%d, nr=%u\r\n", fr, nr);
                    return(FILE_OPS_ERROR);
                }

                //gpio_put(22, 1); // for debugging to time the loop
                bp = sectordata;
                for (int i = 0; i < bytecount; i++){
                    storebyte(*bp++);
                }
                //gpio_put(22, 0); // for debugging to time the loop
            }
        }
    }

    return(FILE_OPS_OKAY);
}

int write_disk_image_data(struct Disk_State* dstate)
{
    FRESULT fr;
    UINT nw;
    uint8_t *bp;
    //int bytecount = dstate->dataLength / 8;
    //int bytecount = (dstate->dataLength / 8) + 4 + 6;
    int bytecount;
    int sectorcount;
    int headcount;
    int cylindercount;
    int ramaddress;
    char display_line_2[30];

    printf("Writing disk image data to file '%s':\r\n", diskimagefilename);
    printf(" cylinders=%d, heads=%d, sectors=%d\r\n", dstate->numberOfCylinders, dstate->numberOfHeads, dstate->numberOfSectorsPerTrack);
    for (cylindercount = 0; cylindercount < dstate->numberOfCylinders; cylindercount++){
        if ((cylindercount % 20) == 0)
            printf("  cylindercount = %d\r\n", cylindercount);
        if ((cylindercount % 10) == 0){
            sprintf(display_line_2,"Cyl %d", cylindercount);
            display_status((char *) "Write card", display_line_2);
        }
        for (headcount = 0; headcount < dstate->numberOfHeads; headcount++){
            for( sectorcount = 0; sectorcount < dstate->numberOfSectorsPerTrack; sectorcount++){
                ramaddress = (cylindercount << 14) | (headcount << 13) | (sectorcount << 9);
                load_ram_address(ramaddress);

                //gpio_put(22, 1); // for debugging to time the loop
                bytecount = 4;
                bp = sectordata;
                for (int i = 0; i < bytecount; i++){
                    *bp++ = readbyte();
                }
                int sector_data_bit_count = (sectordata[3] << 8) | sectordata[2];
                int wordcount = (sector_data_bit_count + 15) >> 4; // round the word count up to the next integer value
                bytecount = wordcount * 2;

                // bp is already pointing to the proper place in the sectordata array
                for (int i = 0; i < bytecount; i++){
                    *bp++ = readbyte();
                }
                //gpio_put(22, 0); // for debugging to time the loop

                bytecount += 4; // add 4 bytes to account for the two 16-bit length fields
                fr = f_write(&fil, sectordata, bytecount, &nw);
                if (fr != FR_OK || nw != bytecount) {
                    printf("###ERROR, Image data write error fr=%d, nw=%u\r\n", fr, nw);
                    return(FILE_OPS_ERROR);
                }
            }
        }
    }
    return(FILE_OPS_OKAY);
}

