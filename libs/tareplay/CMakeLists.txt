add_library(tareplay STATIC
    TaDemoCompiler.h
    TaDemoCompiler.cpp
    TaDemoCompilerClient.h
    TaDemoCompilerClient.cpp
    TaDemoCompilerMessages.h
    TaDemoCompilerMessages.cpp
    TaReplayClient.h
    TaReplayClient.cpp
    TaReplayServer.h
    TaReplayServer.cpp
    TaReplayServerMessages.h
    TaReplayServerMessages.cpp
    )

target_link_libraries(tareplay
    gpgnet
    tapacket
    Qt5::Core
    Qt5::Network
    )

target_include_directories(tareplay 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR})
