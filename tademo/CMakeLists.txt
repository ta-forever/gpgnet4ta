set(CMAKE_AUTOMOC ON)

add_library(tademo STATIC
    TPacket.h
    TPacket.cpp
    DPlayPacket.h
    DPlayPacket.cpp
    TAPacketParser.h
    TAPacketParser.cpp
    TADemoParser.h
    TADemoParser.cpp
    HexDump.h
    HexDump.cpp
    nswfl_crc32.h
    nswfl_crc32.cpp
    DuplicateDetection.h
    DuplicateDetection.cpp
    Watchdog.h
    Watchdog.cpp
    TestPackets.h
    TestPackets.cpp)

target_include_directories(tademo 
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tademo
    Qt5::Core)
