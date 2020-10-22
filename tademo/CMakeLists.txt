add_library(tademo
    TADemoParser.h
    TADemoParser.cpp
    TPacket.h
    TPacket.cpp
    GameMonitor.h
    GameMonitor.cpp
    HexDump.h
    HexDump.cpp)

target_include_directories(tademo 
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
