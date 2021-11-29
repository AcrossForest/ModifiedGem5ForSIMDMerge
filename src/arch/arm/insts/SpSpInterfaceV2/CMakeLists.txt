cmake_minimum_required(VERSION 3.10)
project(SpSpInst)

# set(CMAKE_SYSTEM_PROCESSOR "arm")


set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_FLAGS "-fsanitize=address")

add_library(SpSpInterfaceV2 INTERFACE)
target_include_directories(SpSpInterfaceV2
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
    set(USE_ARM TRUE)
    target_compile_options(SpSpInterfaceV2 INTERFACE "-march=armv8.6-a+sve")
    target_compile_definitions(SpSpInterfaceV2 INTERFACE __SPSP_USE_ARM__)
    message("Build for ARM. Use real SpSp Instructions" )
else()
    set(USE_ARM FALSE)
    target_compile_definitions(SpSpInterfaceV2 INTERFACE __SPSP_USE_X86__)
    message("Build for X86. Use fake SpSp Instructions implemented using vectors.")
endif()

add_executable(SpSpTest
    ${PROJECT_SOURCE_DIR}/test/testAdd.cpp
)
# target_link_libraries(SpSpTest )

target_link_libraries(SpSpTest
    PUBLIC "-static"
        SpSpInterfaceV2
)
