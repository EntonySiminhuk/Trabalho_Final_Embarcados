cmake_minimum_required(VERSION 3.20.0)

set(BOARD nucleo_g474re)
#set(BOARD nucleo_g431rb)

set(CONF_FILE "boards/nucleo_g474re.conf")
list(APPEND CONF_FILE "boards/nucleo_g474re.conf")

#set(CONF_FILE "boards/nucleo_g431rb.conf")
#list(APPEND CONF_FILE "boards/nucleo_g431rb.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(se28cp_app)

#target_include_directories(app PRIVATE "~/zephyrproject/modules/lib/" "~/zephyrproject/modules/lib/cmsis-dsp/Include/")
target_sources(app PRIVATE src/zbus.c src/keyboard.c src/sensor.c src/main.c)


