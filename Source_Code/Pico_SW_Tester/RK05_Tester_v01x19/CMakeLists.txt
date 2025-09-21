# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(RK05_Tester_v01x19 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

#add_executable(RK05_Tester_v01x19
#        RK05_Tester_v01x19.c
#        )
add_executable(RK05_Tester_v01x19
	RK05_Tester_v02.cpp
	tester_hardware.cpp
	display_functions.cpp
	tester_command.cpp
	microsd_file_ops.cpp
	ssd1306a.cpp
	hw_config.c
	)

# Tell CMake where to find other source code
add_subdirectory(lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

# pull in common dependencies
target_link_libraries(RK05_Tester_v01x19 pico_stdlib FatFs_SPI hardware_i2c hardware_spi hardware_gpio hardware_pwm)

# create map/bin/hex file etc.
pico_add_extra_outputs(RK05_Tester_v01x19)

# add url via pico_set_program_url

# Enable UART output, disable USB output
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
