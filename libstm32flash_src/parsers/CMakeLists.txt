aux_source_directory(. PSRC_LIST) # 搜索当前目录下的所有.cpp文件
add_library(parsers ${PSRC_LIST})

set_property(TARGET parsers PROPERTY POSITION_INDEPENDENT_CODE ON)
