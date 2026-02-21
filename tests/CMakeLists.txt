cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test Sql)
find_package(OpenSSL REQUIRED)

enable_testing()

# CMAKE_BINARY_DIR is always the root build directory (e.g. build/).
# Generated protobuf headers live in build/meshtastic/*.pb.h
# meshtastic_proto is a static library defined in the root CMakeLists.txt
# that compiles PROTO_SRCS where add_custom_command lives.  Linking against
# it is cleaner than listing ${PROTO_SRCS} as sources here, which would break
# AUTOMOC's timestamp_deps target in this subdirectory scope.

# ---- test_protocol ----
add_executable(test_protocol
    test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/MeshtasticProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/DeviceConfig.cpp
)

target_include_directories(test_protocol PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_protocol PRIVATE
    Qt6::Core
    Qt6::Test
    meshtastic_proto
    OpenSSL::Crypto
)

add_test(NAME Protocol COMMAND test_protocol)

# ---- test_nodemanager ----
add_executable(test_nodemanager
    test_nodemanager.cpp
    DatabaseStub.cpp
    ${CMAKE_SOURCE_DIR}/src/Database.h
    ${CMAKE_SOURCE_DIR}/src/NodeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/MeshtasticProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/DeviceConfig.cpp
)

target_include_directories(test_nodemanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_nodemanager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Sql
    meshtastic_proto
    OpenSSL::Crypto
)

add_test(NAME NodeManager COMMAND test_nodemanager)
