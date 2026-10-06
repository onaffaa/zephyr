# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world_relocatable_elf)

if(DEFINED CONFIG_PIC_OPTIONS)
    zephyr_compile_options(-fPIC -ftls-model=local-exec)
    add_link_options(-pie)
endif()

target_sources(app PRIVATE src/main.c)

# main()'s link-time address is only known after the final link, so patch
# it into main.c's placeholder sentinel bytes once linking is done.
# add_custom_command(TARGET ... POST_BUILD) requires the target to have
# been created in this directory, so depend on it by name instead.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main_link_addr_patched.stamp
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/patch_main_link_addr.py
          --nm ${CMAKE_NM}
          $<TARGET_FILE:${logical_target_for_zephyr_elf}>
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/main_link_addr_patched.stamp
  DEPENDS ${logical_target_for_zephyr_elf}
  COMMENT "Patching main() link-time address into $<TARGET_FILE:${logical_target_for_zephyr_elf}>"
  VERBATIM
)
add_custom_target(patch_main_link_addr ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/main_link_addr_patched.stamp
)
