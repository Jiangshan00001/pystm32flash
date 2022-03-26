cmake_minimum_required(VERSION 3.4.1)



#project(libstm32flash)

aux_source_directory(. SRC_LIST) 


list(REMOVE_ITEM SRC_LIST ./serial_posix.c)
list(REMOVE_ITEM SRC_LIST ./serial_w32.c)
list(REMOVE_ITEM SRC_LIST ./libmain.c)

add_executable(stm32flash  ${SRC_LIST})

list(APPEND SRC_LIST ./libmain.c)
list(REMOVE_ITEM SRC_LIST ./main.c)
#add_library(libstm32flash SHARED ${SRC_LIST})
add_library(stm32flashlib SHARED ${SRC_LIST})
set_target_properties(stm32flashlib PROPERTIES PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/libmain.h)


add_subdirectory(parsers)

target_link_libraries(stm32flash parsers)
target_link_libraries(stm32flashlib parsers)

INSTALL(TARGETS stm32flashlib 
        LIBRARY DESTINATION lib
		RUNTIME DESTINATION bin
        PUBLIC_HEADER DESTINATION include)
		
INSTALL(TARGETS stm32flash 
        LIBRARY DESTINATION lib
		RUNTIME DESTINATION bin
        PUBLIC_HEADER DESTINATION include)		