cmake_minimum_required(VERSION 3.6)

include(GNUInstallDirs)


configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py"  "${PROJECT_BINARY_DIR}/__init__.py" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cffi_helpers.py"  "${PROJECT_BINARY_DIR}/cffi_helpers.py"   COPYONLY)

set(head_files ${PROJECT_BINARY_DIR}/__init__.py ${PROJECT_BINARY_DIR}/cffi_helpers.py)
install(FILES ${head_files} DESTINATION  ${CMAKE_INSTALL_PREFIX})
