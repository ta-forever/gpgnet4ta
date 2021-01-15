set(CMAKE_AUTOMOC ON)

add_library(gpgnet
	GpgNetParse.h
	GpgNetParse.cpp
    GpgNetSend.h
    GpgNetSend.cpp
    GpgNetClient.h
    GpgNetClient.cpp)

target_link_libraries(gpgnet
    Qt5::Core
    tademo)

target_include_directories(gpgnet 
    PUBLIC
    ${CMAKE_SOURCE_DIR})
