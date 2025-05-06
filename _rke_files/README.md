# rke Files
rke Files<p>
The rke files in this folder can be copied to a microSD card that the emulator will load. When the RUN/LOAD switch is toggled to the RUN position, the emulator loads the first rke file that it finds on the microSD card. RPi software version 2 and above and FPGA firmware version 2 and above are required to use rke files. RPi software version 2 and above and FPGA firmware version 2 and above will work with the v1 and v2 hardware.<p>

os8.rke (for PDP-8) is a bootable image of OS8<p>

scratch1.rke (for PDP-8) is a scratch disk, meaning that it has a valid empty file allocation table so OS8 will recognize it as a disk with no files on it.<p>

For PDP-11 please use the RK05 Emulator RK11D Utility which is in the folder: RK05_Emulator_RK11D_Utility.<p>

For Data General Nova the file DG_NOVA_Z.RKE has the proper data rate, number of sectors and data length and contains all-zero data. The first word of each sector contains a shifted version of the cylinder address.<p>

The emulator reads rke files from memory cards inserted into the microSD socket. These files have an rke file extension which indicates that the file has the unique format intended for the RK05 emulator. An rke file has a file header followed by a binary image of the disk data in the controller-independent emulator data format. The header describes disk pack parameters that are independent of the disk controller on each type of computer. There is one exception to this rule; the bit rate used by the controller is a parameter in the header. Header parameters are values such as: number of number of cylinders, number of heads, number of sectors per track, microseconds per sector, and bit rate.<p>

Controller-Independent Emulator Data Format.pdf - describes the structure of rke files and provides an example of an rke file for a PDP-8.
