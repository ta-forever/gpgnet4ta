set(CMAKE_AUTOMOC ON)
add_library(dplayreg STATIC
    DPlayReg.h
    DPlayReg.cpp
)

target_link_libraries(dplayreg
    Qt5::Core)

target_include_directories(dplayreg
    PUBLIC
    )
