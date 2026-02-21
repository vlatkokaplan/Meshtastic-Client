cmake_minimum_required(VERSION 3.16)

# PROTO_SRCS paths are inherited from the parent scope but the GENERATED
# property is directory-scoped, so re-declare it here so CMake doesn't
# complain that the .pb.cc files don't exist yet on a clean build.
set_source_files_properties(${PROTO_SRCS} PROPERTIES GENERATED TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test Sql)
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)

enable_testing()

# CMAKE_BINARY_DIR is always the root build directory (e.g. build/).
# Generated protobuf headers live in build/meshtastic/*.pb.h
# PROTO_SRCS is inherited from parent scope via add_subdirectory.

# ---- test_protocol ----
add_executable(test_protocol
    test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/MeshtasticProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/DeviceConfig.cpp
    ${PROTO_SRCS}
)

target_include_directories(test_protocol PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}          # generated protobuf headers (meshtastic/*.pb.h)
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(test_protocol PRIVATE
    Qt6::Core
    Qt6::Test
    protobuf::libprotobuf
    OpenSSL::Crypto
)

add_test(NAME Protocol COMMAND test_protocol)

# ---- test_nodemanager ----
# NodeManager only needs Qt::Core + Qt::Sql (via Database.h forward decl).
# We deliberately exclude Database.cpp to avoid its transitive QWidget dep.
add_executable(test_nodemanager
    test_nodemanager.cpp
    DatabaseStub.cpp
    ${CMAKE_SOURCE_DIR}/src/Database.h
    ${CMAKE_SOURCE_DIR}/src/NodeManager.cpp
    ${CMAKE_SOURCE_DIR}/src/MeshtasticProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/DeviceConfig.cpp
    ${PROTO_SRCS}
)

target_include_directories(test_nodemanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(test_nodemanager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Sql
    protobuf::libprotobuf
    OpenSSL::Crypto
)

add_test(NAME NodeManager COMMAND test_nodemanager)
