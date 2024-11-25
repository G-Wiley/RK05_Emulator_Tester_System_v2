cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C++ standards
project(RK05_Emulator_v00 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

add_executable(RK05_Emulator_v00
	RK05_Emulator_v00.cpp
	emulator_hardware.cpp
	display_functions.cpp
	emulator_state.cpp
	emulator_command.cpp
	microsd_file_ops.cpp
	ssd1306a.cpp
	hw_config.c
	)

# Tell CMake where to find other source code
add_subdirectory(lib/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

# Pull in our pico_stdlib which pulls in commonl
#target_link_libraries(RK05_Emulator_v00 pico_stdlib FatFs_SPI text_extended_ascii hardware_i2c pico_ssd1306)
#target_link_libraries(RK05_Emulator_v00 pico_stdlib FatFs_SPI hardware_i2c pico_ssd1306 hardware_spi)
target_link_libraries(RK05_Emulator_v00 pico_stdlib FatFs_SPI hardware_i2c hardware_spi hardware_gpio hardware_pwm hardware_adc)
#target_link_libraries(RK05_Emulator_v00 pico_stdlib FatFs_SPI hardware_i2c hardware_spi hardware_gpio hardware_pwm pico_ssd1306)
#target_link_libraries(RK05_Emulator_v00 pico_stdlib FatFs_SPI hardware_i2c hardware_spi)

# create map/bin/hex file etc.
#pico_add_extra_outputs(RK05_Emulator_v00 text_extended_ascii)
#pico_add_extra_outputs(text_extended_ascii)
pico_add_extra_outputs(RK05_Emulator_v00)

# Enable usb output, disable uart output
#pico_enable_stdio_usb(${PROJECT_NAME} 1)
#pico_enable_stdio_uart(${PROJECT_NAME} 0)
