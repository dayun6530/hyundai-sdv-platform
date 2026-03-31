find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/eventbus.proto)

add_library(eventbus_proto ${PROTO_FILES})
target_link_libraries(eventbus_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)
target_include_directories(eventbus_proto
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
)

protobuf_generate(TARGET eventbus_proto LANGUAGE cpp)
protobuf_generate(
    TARGET eventbus_proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
)

add_library(EventBus
    src/Event.cpp
    src/EventQueue.cpp
    src/EventBusProxy.cpp
)

target_include_directories(EventBus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(EventBus
    PUBLIC
        eventbus_proto
)
