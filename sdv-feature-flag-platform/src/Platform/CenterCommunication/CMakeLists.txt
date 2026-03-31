add_library(CenterCommunicationLib
    src/Signal.cpp
)

target_include_directories(CenterCommunicationLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
