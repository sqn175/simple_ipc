cmake_minimum_required(VERSION 2.8.3)

project(Test)

set(CMAKE_CXX_STANDARD 11)

IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    message("debug mode")
ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)

IF(TEST MATCHES DEBUG)
    message("test mode")
ENDIF(TEST MATCHES DEBUG)


add_executable(test
    main.cpp
    msg_comm.hpp 
    msg_send.hpp
    msg_recv.hpp
    ipc_lock.h
    ipc_lock.cpp
)

target_link_libraries(test pthread rt)