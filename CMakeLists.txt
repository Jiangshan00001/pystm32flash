cmake_minimum_required(VERSION 3.6)

project(pystm32flash)



if(WIN32)
set(__WIN32__  1)
add_definitions(-D__WIN32__)
message("set win32")
endif()

if(UNIX AND NOT APPLE)# 
set(__linux__  1)
add_definitions(-D__linux__)
message("set linux")
endif()


add_subdirectory(libstm32flash_src)
add_subdirectory(pystm32flash_src)