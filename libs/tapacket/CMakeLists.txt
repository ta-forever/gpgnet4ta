add_library(tapacket STATIC
    DPlayPacket.h
    DPlayPacket.cpp
    TADemoParser.h
    TADemoParser.cpp
    TADemoRecords.h
    TADemoWriter.h
    TADemoWriter.cpp
    TAPacketParser.h
    TAPacketParser.cpp
    TestPackets.h
    TestPackets.cpp
    TPacket.h
    TPacket.cpp
    )

target_include_directories(tapacket 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tapacket
    taflib
    Qt5::Core
    )
