cmake_minimum_required(VERSION 3.24)
project(DeyeMonitor VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.5)
include(GNUInstallDirs)

add_library(deye_core STATIC
    src/chargeestimator.cpp src/chargeestimator.h
    src/profiles.cpp src/profiles.h
    src/protocol.cpp src/protocol.h
    src/discovery.cpp src/discovery.h
    src/diagnosticprobe.cpp src/diagnosticprobe.h
    src/monitor.cpp src/monitor.h)
target_include_directories(deye_core PUBLIC src)
target_link_libraries(deye_core PUBLIC Qt6::Core Qt6::Network)
file(GLOB PROFILE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/profiles/*.json")
qt_add_resources(deye_core profile_data PREFIX "/profiles" BASE profiles FILES ${PROFILE_FILES})

qt_add_executable(deye-monitor-qt src/main.cpp src/historychart.cpp src/historychart.h)
target_link_libraries(deye-monitor-qt PRIVATE deye_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
set(DEYE_UI_FILES
    qml/Main.qml qml/Icon.qml qml/MetricCard.qml qml/FlowView.qml
    qml/HistoryPanel.qml qml/SettingsWindow.qml qml/DetailView.qml qml/ProbeWindow.qml
    assets/com.deye.monitor.svg)
qt_add_resources(deye-monitor-qt qml_data PREFIX "/" FILES ${DEYE_UI_FILES})
set_target_properties(deye-monitor-qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    MACOSX_BUNDLE TRUE WIN32_EXECUTABLE TRUE)
install(TARGETS deye-monitor-qt BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(DEYE_DESKTOP_EXEC "\"${CMAKE_INSTALL_FULL_BINDIR}/deye-monitor-qt\"")
    configure_file(packaging/com.deye.monitor.desktop.in desktop-install/com.deye.monitor.desktop @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/desktop-install/com.deye.monitor.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/com.deye.monitor.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

    # This launcher follows the checkout's binary so rebuilding updates what the taskbar launches.
    set(DEYE_DESKTOP_EXEC "\"${CMAKE_SOURCE_DIR}/bin/deye-monitor-qt\"")
    configure_file(packaging/com.deye.monitor.desktop.in desktop-development/com.deye.monitor.desktop @ONLY)
    add_custom_target(install-desktop
        COMMAND ${CMAKE_COMMAND}
            "-DDEYE_LAUNCHER=${CMAKE_CURRENT_BINARY_DIR}/desktop-development/com.deye.monitor.desktop"
            "-DDEYE_ICON=${CMAKE_CURRENT_SOURCE_DIR}/assets/com.deye.monitor.svg"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/install-desktop.cmake"
        DEPENDS deye-monitor-qt
        COMMENT "Installing the user's Deye Monitor launcher and SVG taskbar icon"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE deye_core Qt6::Test)
    add_test(NAME core COMMAND test_core)
    qt_add_executable(test_discovery tests/test_discovery.cpp)
    target_link_libraries(test_discovery PRIVATE deye_core Qt6::Test)
    add_test(NAME discovery COMMAND test_discovery)
    qt_add_executable(test_probe tests/test_probe.cpp)
    target_link_libraries(test_probe PRIVATE deye_core Qt6::Test)
    add_test(NAME probe COMMAND test_probe)
    qt_add_executable(test_chargeestimator tests/test_chargeestimator.cpp)
    target_link_libraries(test_chargeestimator PRIVATE deye_core Qt6::Test)
    add_test(NAME chargeestimator COMMAND test_chargeestimator)
    qt_add_executable(test_cli tests/test_cli.cpp)
    target_link_libraries(test_cli PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(test_cli PRIVATE DEYE_EXECUTABLE="$<TARGET_FILE:deye-monitor-qt>")
    add_dependencies(test_cli deye-monitor-qt)
    add_test(NAME cli COMMAND test_cli)
    qt_add_executable(test_ui tests/test_ui.cpp src/historychart.cpp src/historychart.h)
    qt_add_resources(test_ui qml_test_data PREFIX "/" FILES ${DEYE_UI_FILES})
    target_link_libraries(test_ui PRIVATE deye_core Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
    add_test(NAME ui COMMAND test_ui)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QT_QPA_PLATFORMTHEME=generic")
endif()
