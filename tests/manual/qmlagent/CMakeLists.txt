# Copyright (C) 2026 Penk Chen <penkia@gmail.com>
# SPDX-License-Identifier: Apache-2.0

qt_add_executable(qmlagent_smoke
    main.cpp
)

target_link_libraries(qmlagent_smoke PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

target_compile_definitions(qmlagent_smoke PRIVATE QT_QML_DEBUG)

qt_add_qml_module(qmlagent_smoke
    URI QmlAgentSmoke
    VERSION 1.0
    QML_FILES
        Smoke.qml
)

qt_add_executable(qmlagent_controls_smoke
    controls_main.cpp
)

target_link_libraries(qmlagent_controls_smoke PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_definitions(qmlagent_controls_smoke PRIVATE QT_QML_DEBUG)

qt_add_qml_module(qmlagent_controls_smoke
    URI QmlAgentControlsSmoke
    VERSION 1.0
    QML_FILES
        ControlsSmoke.qml
)

qt_add_executable(qmlagent_diagnostic_fixture
    fixture_main.cpp
)

target_link_libraries(qmlagent_diagnostic_fixture PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

target_compile_definitions(qmlagent_diagnostic_fixture PRIVATE QT_QML_DEBUG)

qt_add_executable(tst_qmlagentintegration
    tst_qmlagentintegration.cpp
)

target_link_libraries(tst_qmlagentintegration PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::QmlDebugPrivate
    Qt6::Test
)

target_compile_definitions(tst_qmlagentintegration PRIVATE
    TEST_QMLAGENTCTL_EXECUTABLE="$<TARGET_FILE:qmlagentctl>"
    TEST_QMLAGENTMCP_EXECUTABLE="$<TARGET_FILE:qmlagent-mcp>"
    TEST_QMLAGENTLAUNCHER_EXECUTABLE="$<TARGET_FILE:qmlagent-launcher>"
)

add_test(NAME qmlagent_tcp_smoke
    COMMAND tst_qmlagentintegration
)

# QT_QUICK_CONTROLS_STYLE=Basic: the suite verifies QmlAgent contracts, not
# Qt's platform styles. The macOS native style paints AppKit NSViews, which
# is crash-prone under the offscreen platform and responds differently to
# synthetic drags; Basic keeps the run deterministic and identical across
# macOS and Linux.
set_tests_properties(qmlagent_tcp_smoke PROPERTIES
    ENVIRONMENT "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1;QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QMLAGENT_SMOKE_APP=$<TARGET_FILE:qmlagent_smoke>;QMLAGENT_CONTROLS_APP=$<TARGET_FILE:qmlagent_controls_smoke>;QMLAGENT_FIXTURE_APP=$<TARGET_FILE:qmlagent_diagnostic_fixture>;QMLAGENT_FIXTURES_DIR=${CMAKE_SOURCE_DIR}/fixtures/diagnostics;QMLAGENT_WORKFLOW_FIXTURES_DIR=${CMAKE_SOURCE_DIR}/fixtures/workflows"
    ENVIRONMENT_MODIFICATION "QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/plugins"
    TIMEOUT 90
)
