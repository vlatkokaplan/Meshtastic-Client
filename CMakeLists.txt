cmake_minimum_required(VERSION 3.16)
project(meshtastic-vibe-client VERSION 1.0.0 LANGUAGES CXX)

# Human-friendly display name used in messages and UI
set(PROJECT_DISPLAY_NAME "Meshtastic Vibe Client")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialPort
    WebChannel
    Sql
)

find_package(Qt6 COMPONENTS WebEngineWidgets)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Proto source directory
set(PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}/proto)

# Core proto files (order matters for dependencies)
set(PROTO_FILES
    ${PROTO_PATH}/meshtastic/portnums.proto
    ${PROTO_PATH}/meshtastic/telemetry.proto
    ${PROTO_PATH}/meshtastic/channel.proto
    ${PROTO_PATH}/meshtastic/config.proto
    ${PROTO_PATH}/meshtastic/module_config.proto
    ${PROTO_PATH}/meshtastic/mesh.proto
    ${PROTO_PATH}/meshtastic/admin.proto
)

# Generate protobuf sources with proper import path
set(PROTO_SRCS)
set(PROTO_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
    file(RELATIVE_PATH PROTO_REL_DIR ${PROTO_PATH} ${PROTO_DIR})

    set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.cc")
    set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.h")

    list(APPEND PROTO_SRCS ${PROTO_SRC})
    list(APPEND PROTO_HDRS ${PROTO_HDR})

    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_REL_DIR}"
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${PROTO_PATH}
            --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating protobuf sources for ${PROTO_NAME}"
        VERBATIM
    )
endforeach()

# Mark generated files
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED TRUE)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/PacketListWidget.cpp
    src/TracerouteWidget.cpp
    src/SerialConnection.cpp
    src/MeshtasticProtocol.cpp
    src/NodeManager.cpp
    src/MapWidget.cpp
    src/Database.cpp
    src/MessagesWidget.cpp
    src/AppSettings.cpp
    src/ConfigWidget.cpp
    src/AppSettingsTab.cpp
    src/DeviceConfig.cpp
    src/DashboardStatsWidget.cpp
    src/RadioConfigTab.cpp
    src/DeviceConfigTab.cpp
    src/PositionConfigTab.cpp
    src/ChannelsConfigTab.cpp
)

set(HEADERS
    src/MainWindow.h
    src/PacketListWidget.h
    src/TracerouteWidget.h
    src/SerialConnection.h
    src/MeshtasticProtocol.h
    src/NodeManager.h
    src/MapWidget.h
    src/Database.h
    src/MessagesWidget.h
    src/AppSettings.h
    src/ConfigWidget.h
    src/AppSettingsTab.h
    src/DeviceConfig.h
    src/DashboardStatsWidget.h
    src/RadioConfigTab.h
    src/DeviceConfigTab.h
    src/PositionConfigTab.h
    src/ChannelsConfigTab.h
)

# Resources
set(RESOURCES
    resources/resources.qrc
)

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${RESOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::WebChannel
    Qt6::Sql
    protobuf::libprotobuf
)

if(Qt6WebEngineWidgets_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_WEBENGINE=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_WEBENGINE=0)
endif()


# Copy resources to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        ${CMAKE_CURRENT_BINARY_DIR}/resources
    COMMENT "Copying resources to build directory"
)

# Install rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY resources/
    DESTINATION share/${PROJECT_NAME}
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== ${PROJECT_DISPLAY_NAME} Configuration ===")
message(STATUS "Qt6 Version: ${Qt6_VERSION}")
message(STATUS "Protobuf Version: ${Protobuf_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Map: WebEngine + Leaflet")
message(STATUS "========================================")

# CPack configuration for DEB package generation
include(CPack)
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "meshtastic-vibe-client")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "vlatkokaplan")
set(CPACK_PACKAGE_CONTACT "vlatkokaplan@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "Qt6 desktop client for Meshtastic mesh network communication")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Meshtastic mesh network client with WebEngine map view")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "vlatkokaplan")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6, libqt6serialport6, libqt6webenginewidgets6, libprotobuf32")
set(CPACK_DEBIAN_PACKAGE_SECTION "communication")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/vlatkokaplan/Meshtastic-Client")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
