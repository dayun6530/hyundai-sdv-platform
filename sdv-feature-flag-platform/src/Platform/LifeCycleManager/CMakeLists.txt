add_library(LifeCycleManagerLib
    src/Feature.cpp
    src/Flag.cpp
    src/LogData.cpp
)

target_include_directories(LifeCycleManagerLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(LifeCycleManagerLib
    PUBLIC
        EventBus
)
