cmake_minimum_required(VERSION 3.10)
project(TVStreammerSAT5 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 203.76: opt-in compiler warnings and a real CTest entry point. Both are ON by
# default so CI catches new warnings and the unit tests are always registered.
option(TVSTREAMMERSAT5_WARNINGS "Build with -Wall -Wextra" ON)
option(TVSTREAMMERSAT5_BUILD_TESTS "Build the C++ unit tests and register them with CTest" ON)

function(tvstreammersat5_apply_warnings target)
    if(TVSTREAMMERSAT5_WARNINGS)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_library(CRYPT_LIBRARY crypt)

pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-mpegts-1.0)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(DVBCSA QUIET libdvbcsa)
if(NOT DVBCSA_FOUND)
    pkg_check_modules(DVBCSA QUIET dvbcsa)
endif()
if(NOT DVBCSA_FOUND)
    find_path(DVBCSA_INCLUDE_DIRS dvbcsa/dvbcsa.h)
    find_library(DVBCSA_LIBRARIES dvbcsa)
    if(NOT DVBCSA_INCLUDE_DIRS OR NOT DVBCSA_LIBRARIES)
        message(FATAL_ERROR "libdvbcsa development files were not found")
    endif()
endif()

find_package(Boost REQUIRED COMPONENTS thread)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${GSTREAMER_INCLUDE_DIRS}
    ${LIBCURL_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${DVBCSA_INCLUDE_DIRS}
)

link_directories(
    ${GSTREAMER_LIBRARY_DIRS}
    ${LIBCURL_LIBRARY_DIRS}
    ${JSONCPP_LIBRARY_DIRS}
    ${Boost_LIBRARY_DIRS}
    ${DVBCSA_LIBRARY_DIRS}
)

add_executable(TVStreammerSAT5
    src/main.cpp
    src/utils.cpp
    src/ConfigManager.cpp
    src/DvbSatellite.cpp
    src/CardManager.cpp
    src/CaBackend.cpp
    src/TelegramNotifier.cpp
    src/UdpInput.cpp
    src/StableUdpOutput.cpp
    src/NetupOutputPacer.cpp
    src/NetworkTsInput.cpp
    src/HlsSegmentScheduler.cpp
    src/mpts/MptsOutputManager.cpp
    src/StreamManager.cpp
    src/TranscoderModule.cpp
    src/GstTranscoderProcess.cpp
    src/protocols/GstProtocolTypes.cpp
    src/protocols/GstInputProtocols.cpp
    src/protocols/GstOutputProtocols.cpp
    src/protocols/stream/StreamInputProtocol.cpp
    src/protocols/stream/StreamOutputProtocol.cpp
    src/protocols/stream/inputs/StreamTestInputProtocol.cpp
    src/protocols/stream/inputs/StreamHttpInputProtocol.cpp
    src/protocols/stream/inputs/StreamHlsInputProtocol.cpp
    src/protocols/stream/inputs/StreamUdpInputProtocol.cpp
    src/protocols/stream/inputs/StreamRtpInputProtocol.cpp
    src/protocols/stream/inputs/StreamSrtInputProtocol.cpp
    src/protocols/stream/inputs/StreamRtspInputProtocol.cpp
    src/protocols/stream/inputs/StreamRtmpInputProtocol.cpp
    src/protocols/stream/inputs/StreamFileInputProtocol.cpp
    src/protocols/stream/outputs/StreamUdpOutputProtocol.cpp
    src/protocols/stream/outputs/StreamRtpOutputProtocol.cpp
    src/protocols/stream/outputs/StreamHttpOutputProtocol.cpp
    src/protocols/stream/outputs/StreamHlsOutputProtocol.cpp
    src/protocols/stream/outputs/StreamSrtOutputProtocol.cpp
    src/protocols/stream/outputs/StreamRtspOutputProtocol.cpp
    src/protocols/stream/outputs/StreamRtmpOutputProtocol.cpp
    src/protocols/inputs/GstHttpInputProtocol.cpp
    src/protocols/inputs/GstHlsInputProtocol.cpp
    src/protocols/inputs/GstUdpInputProtocol.cpp
    src/protocols/inputs/GstRtpInputProtocol.cpp
    src/protocols/inputs/GstSrtInputProtocol.cpp
    src/protocols/inputs/GstRtspInputProtocol.cpp
    src/protocols/inputs/GstRtmpInputProtocol.cpp
    src/protocols/outputs/GstOutputProtocolUtils.cpp
    src/protocols/outputs/GstFifoOutputProtocol.cpp
    src/protocols/outputs/GstUdpOutputProtocol.cpp
    src/protocols/outputs/GstRtpOutputProtocol.cpp
    src/protocols/outputs/GstSrtOutputProtocol.cpp
    src/protocols/outputs/GstHttpOutputProtocol.cpp
    src/protocols/outputs/GstHlsOutputProtocol.cpp
    src/protocols/outputs/GstRtspOutputProtocol.cpp
    src/protocols/outputs/GstRtmpOutputProtocol.cpp
    src/HttpServer.cpp
    src/OscamMiniManager.cpp
)

target_link_libraries(TVStreammerSAT5
    ${GSTREAMER_LIBRARIES}
    ${LIBCURL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${Boost_LIBRARIES}
    OpenSSL::Crypto
    pthread
    dl
    glib-2.0
    gio-2.0
)


if(TVSTREAMMERSAT5_BUILD_TESTS)
    enable_testing()
    # 203.76: the three behavioural tests that already existed in tests/ were
    # never compiled by any build; now they run under ctest.
    add_executable(tvstreammersat5_test_http_origin tests/test_http_origin_20376.cpp)
    target_include_directories(tvstreammersat5_test_http_origin PRIVATE ${CMAKE_SOURCE_DIR}/src)
    tvstreammersat5_apply_warnings(tvstreammersat5_test_http_origin)
    add_test(NAME http_origin_policy COMMAND tvstreammersat5_test_http_origin)

    add_executable(tvstreammersat5_test_ts_media_progress tests/test_ts_media_progress_20369.cpp)
    tvstreammersat5_apply_warnings(tvstreammersat5_test_ts_media_progress)
    add_test(NAME ts_media_progress COMMAND tvstreammersat5_test_ts_media_progress)

    add_executable(tvstreammersat5_test_udp_media_delivery tests/test_udp_media_delivery_20370.cpp)
    tvstreammersat5_apply_warnings(tvstreammersat5_test_udp_media_delivery)
    add_test(NAME udp_media_delivery COMMAND tvstreammersat5_test_udp_media_delivery)

    add_executable(tvstreammersat5_test_sd_widescreen tests/test_sd_widescreen_20371.cpp)
    tvstreammersat5_apply_warnings(tvstreammersat5_test_sd_widescreen)
    add_test(NAME sd_widescreen_geometry COMMAND tvstreammersat5_test_sd_widescreen)
endif()

option(TVSTREAMMERSAT5_BUILD_CA_PLUGIN_EXAMPLE "Build non-decoding CaBackend ABI example plugin" OFF)
if(TVSTREAMMERSAT5_BUILD_CA_PLUGIN_EXAMPLE)
    add_library(tvstreammersat5_ca_passthrough_example SHARED
        examples/ca_backend_passthrough_plugin.cpp
    )
    target_include_directories(tvstreammersat5_ca_passthrough_example PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    set_target_properties(tvstreammersat5_ca_passthrough_example PROPERTIES
        PREFIX ""
        OUTPUT_NAME "tvstreammersat5-ca-passthrough-example"
    )
endif()

add_library(tvstreammersat5_ca_newcamd SHARED
    src/ca/backends/newcamd/NewcamdPlugin.cpp
    src/ca/backends/newcamd/NewcamdClient.cpp
)
target_include_directories(tvstreammersat5_ca_newcamd PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tvstreammersat5_ca_newcamd PRIVATE
    ${Boost_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${DVBCSA_LIBRARIES}
    OpenSSL::Crypto
    $<$<BOOL:${CRYPT_LIBRARY}>:${CRYPT_LIBRARY}>
)
set_target_properties(tvstreammersat5_ca_newcamd PROPERTIES
    PREFIX ""
    OUTPUT_NAME "tvstreammersat5-ca-newcamd"
)
tvstreammersat5_apply_warnings(TVStreammerSAT5)
tvstreammersat5_apply_warnings(tvstreammersat5_ca_newcamd)

install(TARGETS TVStreammerSAT5 RUNTIME DESTINATION bin)
install(PROGRAMS scripts/import_config.py scripts/import_astra_channels.py scripts/import_flussonic_channels.py
        DESTINATION libexec/tvstreammersat5)
install(FILES web/vendor/mpegts.min.js web/vendor/hls.min.js web/vendor/adminlte-3.2.0.min.css web/vendor/LICENSE.adminlte
        DESTINATION web/vendor)
install(TARGETS tvstreammersat5_ca_newcamd LIBRARY DESTINATION /opt/tvstreammersat5/ca-plugins)

include(${CMAKE_SOURCE_DIR}/cmake/OscamMini.cmake)
