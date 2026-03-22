cmake_minimum_required(VERSION 3.16)
project(odbc_xa_switch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ODBC REQUIRED)

add_library(odbcxaswitch SHARED
    source/xa.cpp
    source/odbc.cpp
    source/context.cpp
    source/rm/mssql.cpp
    source/rm/oradb.cpp
    source/rm/pgsql.cpp
)

target_include_directories(odbcxaswitch
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(odbcxaswitch
    PUBLIC
        ODBC::ODBC
)

target_compile_options(odbcxaswitch
    PRIVATE
        -Wall
)

set_target_properties(odbcxaswitch PROPERTIES
    OUTPUT_NAME odbcxaswitch
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

install(TARGETS odbcxaswitch
    LIBRARY DESTINATION lib
)

install(DIRECTORY
    include/odbc-xa-switch
    DESTINATION include
)
