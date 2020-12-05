add_library(tademo
    TPacket.h
    TPacket.cpp
    DPlayPacket.h
    DPlayPacket.cpp
    TAPacketParser.h
    TAPacketParser.cpp
    TADemoParser.h
    TADemoParser.cpp
    HexDump.h
    HexDump.cpp)

target_include_directories(tademo 
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
