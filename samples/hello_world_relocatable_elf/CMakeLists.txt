# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world_relocatable_elf)

if(DEFINED CONFIG_PIC_OPTIONS)
    zephyr_compile_options(-fPIC -ftls-model=initial-exec)
    add_link_options(-pie)
endif()

target_sources(app PRIVATE src/main.c)
