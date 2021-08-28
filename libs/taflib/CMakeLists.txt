set(CMAKE_AUTOMOC ON)

if (WIN32)
    set(WIN32_CONDITIONAL_SOURCES
        MessageBoxThread.h
        MessageBoxThread.cpp
    )
else()
    set(WIN32_CONDITIONAL_SOURCES "")
endif()


add_library(taflib STATIC
    ConsoleReader.h
    ConsoleReader.cpp
    DuplicateDetection.h
    DuplicateDetection.cpp
    HexDump.h
    HexDump.cpp
    Logger.h
    Logger.cpp
    nswfl_crc32.h
    nswfl_crc32.cpp
    Watchdog.h
    Watchdog.cpp
    ${WIN32_CONDITIONAL_SOURCES}
    )

target_link_libraries(taflib
    Qt5::Core
    )

target_include_directories(taflib 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR})
