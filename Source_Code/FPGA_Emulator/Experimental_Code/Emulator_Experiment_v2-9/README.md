Emulator v2.9 is an experimental version to test a possible update to provide ATTEN1 through ATTEN4 outputs for DG Nova computers.

<p>Most of the changes are in the top level file: RK05_emulator_top_v06.v
There are also changes in spi_interface.v that define an ATTEN Mode Enable bit. Currently there is no software support for this feature and atten_mode_enable is not used in the FPGA code. The signal appears only in lines that are commented out, so hardware that has FPGA firmware v2.9 is always in "Atten Mode".

<p>Microsoft Word files that show the code changes in RK05_emulator_top_v06.v and spi_interface.v are: RK05_emulator_top_v06_diff_v05.docx and spi_interface_v6_diff_v5.docx
