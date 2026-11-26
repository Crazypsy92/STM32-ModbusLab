cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32-ModbusLab)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    BSP/Src/delay.c
    BSP/Src/i2c_stm32_ll.c
    BSP/Src/bsp_i2c.c
    BSP/Src/oled_port.c
    BSP/Src/multi_button.c
    BSP/Src/key_port.c
    BSP/Src/oled.c
    BSP/Src/oled_data.c
    BSP/Src/at24cxx.c
    BSP/Src/at24xx_port.c
    BSP/Src/app_test.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    BSP/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# 板级 IIC 后端选择：SOFT（默认，GPIO 模拟） / HARD（I2C2）
# OLED 与 AT24xx 共用同一条总线，该选项同时作用于两者
set(BSP_I2C_BACKEND "SOFT" CACHE STRING "Board IIC backend: SOFT or HARD")
set_property(CACHE BSP_I2C_BACKEND PROPERTY STRINGS SOFT HARD)
if(BSP_I2C_BACKEND STREQUAL "HARD")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BSP_I2C_BACKEND_SOFT=0)
    message(STATUS "BSP IIC backend: HARD (I2C2, PB10/PB11)")
else()
    message(STATUS "BSP IIC backend: SOFT (GPIO bit-bang, PB10/PB11)")
endif()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
