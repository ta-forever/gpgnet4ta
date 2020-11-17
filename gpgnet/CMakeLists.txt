
add_library(gpgnet
    GpgNetReceive.h
    GpgNetReceive.cpp
    GpgNetSend.h
    GpgNetSend.cpp)

target_link_libraries(gpgnet
    Qt5::Core)

target_include_directories(gpgnet 
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
