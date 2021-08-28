set(CMAKE_AUTOMOC ON)
add_library(gpgnet STATIC
    GpgNetClient.h
    GpgNetClient.cpp
    GpgNetParse.h
    GpgNetParse.cpp
    GpgNetSend.h
    GpgNetSend.cpp
    GpgNetServerMessages.h
    GpgNetServerMessages.cpp
)

target_link_libraries(gpgnet
    Qt5::Core
    Qt5::Network)

target_include_directories(gpgnet 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs)
