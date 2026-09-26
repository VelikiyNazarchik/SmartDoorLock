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
set(CMAKE_PROJECT_NAME SmartDoorLock)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Application layers: App (tasks + services) and Drivers/BSP (device drivers)
set(APP_SOURCES
    App/Src/access_ctrl.c
    App/Src/access_list.c
    App/Src/app.c
    App/Src/app_time.c
    App/Src/crc32.c
    App/Src/event_log.c
    App/Src/flash_store.c
    App/Src/logger.c
    App/Src/menu.c
    App/Src/supervisor.c
    App/Src/task_ctrl.c
    App/Src/task_input.c
    App/Src/task_log.c
    App/Src/task_net.c
    App/Src/task_rfid.c
    App/Src/task_ui.c
    App/Src/timeutil.c
    App/Src/ui_screens.c
    Drivers/BSP/buzzer/buzzer.c
    Drivers/BSP/gc9a01/gc9a01.c
    Drivers/BSP/gfx/font5x7.c
    Drivers/BSP/gfx/gfx.c
    Drivers/BSP/joystick/joystick.c
    Drivers/BSP/joystick/joystick_math.c
    Drivers/BSP/lock/lock.c
    Drivers/BSP/rc522/rc522.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES})

# Our own code must build without warnings; generated/vendor code keeps its default flags.
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App/Inc
    Drivers/BSP/buzzer
    Drivers/BSP/gc9a01
    Drivers/BSP/gfx
    Drivers/BSP/joystick
    Drivers/BSP/lock
    Drivers/BSP/rc522
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
