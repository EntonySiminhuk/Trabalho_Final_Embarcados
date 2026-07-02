cmake_minimum_required(VERSION 3.20.0)

set(BOARD nucleo_g474re)
#set(BOARD nucleo_g431rb)

# Nao sobrescrevemos CONF_FILE: o Zephyr usa prj.conf por padrao e mescla
# automaticamente boards/<BOARD>.conf (boards/nucleo_g474re.conf) por cima.

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(se28cp_app)

target_sources(app PRIVATE src/zbus.c src/keyboard.c src/uart_cli.c src/main.c)
