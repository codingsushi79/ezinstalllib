cmake_minimum_required(VERSION 3.16)
project(ezinstalllib VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(EZI_BUILD_CLI "Build the ezi command-line tool" ON)
option(EZI_USE_CURL "Use libcurl for HTTP downloads" ON)

find_package(CURL QUIET)

if(EZI_USE_CURL AND NOT CURL_FOUND)
    message(WARNING "libcurl not found — downloads will use the curl(1) command")
endif()

set(EZI_SOURCES
    src/platform.c
    src/path.c
    src/util.c
    src/ops.c
    src/parser.c
    src/executor.c
)

add_library(ezinstalllib STATIC ${EZI_SOURCES})
target_include_directories(ezinstalllib
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CURL_FOUND)
    target_link_libraries(ezinstalllib PUBLIC CURL::libcurl)
    target_compile_definitions(ezinstalllib PRIVATE EZI_HAVE_CURL=1)
endif()

if(WIN32)
    target_compile_definitions(ezinstalllib PRIVATE EZI_PLATFORM_WINDOWS=1)
    target_link_libraries(ezinstalllib PRIVATE shell32)
else()
    target_compile_definitions(ezinstalllib PRIVATE EZI_PLATFORM_UNIX=1)
    target_link_libraries(ezinstalllib PRIVATE m)
endif()

if(EZI_BUILD_CLI)
    add_executable(ezi tools/ezi.c)
    target_link_libraries(ezi PRIVATE ezinstalllib)

    add_executable(run_demo examples/run_demo.c)
    target_link_libraries(run_demo PRIVATE ezinstalllib)
endif()

install(TARGETS ezinstalllib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES include/ezinstalllib.h DESTINATION include)

if(EZI_BUILD_CLI)
    install(TARGETS ezi RUNTIME DESTINATION bin)
endif()
