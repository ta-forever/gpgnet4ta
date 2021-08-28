set(CMAKE_AUTOMOC ON)

add_library(gpgnet STATIC
    GpgNetServerMessages.h
    GpgNetServerMessages.cpp
    GpgNetParse.h
    GpgNetParse.cpp
    GpgNetSend.h
    GpgNetSend.cpp
    GpgNetClient.h
    GpgNetClient.cpp)

target_link_libraries(gpgnet
    Qt5::Core
    Qt5::Network
    tademo)

target_include_directories(gpgnet 
    PUBLIC
    ${CMAKE_SOURCE_DIR})
