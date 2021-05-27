add_library(jdplay STATIC
    JDPlay.h
    JDPlay.cpp)

target_include_directories(jdplay 
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DIRECT_PLAY_INCLUDE_DIR})

target_link_libraries(jdplay
  ${DIRECT_PLAY_LIBS}
)
