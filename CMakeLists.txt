cmake_minimum_required(VERSION 3.16)

project(hfsgraph VERSION 0.0.0 LANGUAGES CXX
        DESCRIPTION "Re-wire a directory hierarchy to match its semantic structure")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# KDE Frameworks tooling (extra-cmake-modules)
find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# 6.3+: QTest::mouseMove delivers button-held moves, which ui::Driver's `drag` relies on.
find_package(Qt6 6.3 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)
find_package(KF6 REQUIRED COMPONENTS CoreAddons KIO)
# Engine / OS-facility components are added as the code lands (see ADRs):
#   KF6::Solid       — mount/device detection for volume descriptors (ADR-200)
#   KF6::Baloo       — tag interop via the standard, daemon-optional (ADR-100/400)
#   libbtrfsutil     — snapshots / reflink (ADR-200), linked directly
#   <sys/xattr.h>    — durable UUID identity (ADR-100)

add_executable(hfsgraph
    src/main.cpp
    src/core/scanner.cpp
    src/core/group.cpp
    src/core/groupstore_io.cpp # ADR-102 #15 JSON group-store persistence (XDG sidecar)
    src/core/commit.cpp        # ADR-200 #16 commit engine — dry-run verification half
    src/core/move.cpp
    src/platform/identity.cpp # ADR-100 durable id (xattr) + runtime fingerprint (lstat)
    # src/ui/nodeitem.cpp  — parked: the node-link card + file-viewer, kept for a
    #   future click-to-inspect detail panel; not built in the treemap-only canvas.
    src/ui/treemapitem.cpp
    src/ui/squarify.cpp
    src/ui/treemaplayout.cpp  # cached treemap geometry (cells, hit-test, replay)
    src/ui/selection.cpp      # file selection model (#37)
    src/ui/fileactions.cpp    # actions on the selection: KIO + staged file ops (#38)
    src/ui/filetypestyle.cpp
    src/ui/grouppanel.cpp
    src/ui/rotatedheader.cpp
    src/ui/queuepanel.cpp
    src/ui/frameitem.cpp
    src/ui/graphscene.cpp
    src/ui/canvasview.cpp
    src/ui/mainwindow.cpp
    src/ui/driver.cpp         # scripted UI driver (--script): input events + screenshots
)
target_include_directories(hfsgraph PRIVATE src)
target_link_libraries(hfsgraph PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test KF6::CoreAddons
                      KF6::KIOCore KF6::KIOGui KF6::KIOWidgets)

include(GNUInstallDirs)
install(TARGETS hfsgraph DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---- Tests (ctest) ----------------------------------------------------------
# The move-staging model (ADR-302) is pure core, so it gets a real unit test. Only
# Qt6::Core is needed (no GUI). `make test` / `make check` run these via ctest.
enable_testing()
add_executable(move_test
    tests/move_test.cpp
    src/core/move.cpp
    src/core/group.cpp
)
target_include_directories(move_test PRIVATE src)
target_link_libraries(move_test PRIVATE Qt6::Core Qt6::Gui) # Gui only for QColor in group.h
add_test(NAME move_test COMMAND move_test)

# Durable identity (ADR-100): xattr round-trip + fingerprint + keyFor, exercised against
# a real temp directory. Self-skips the xattr asserts when the fs lacks user.* support.
add_executable(identity_test
    tests/identity_test.cpp
    src/platform/identity.cpp
    src/core/group.cpp
)
target_include_directories(identity_test PRIVATE src)
target_link_libraries(identity_test PRIVATE Qt6::Core Qt6::Gui) # Gui only for QColor in group.h
add_test(NAME identity_test COMMAND identity_test)

# Group-store JSON persistence (ADR-102 #15): save/load round-trip into a temp XDG dir
# (QStandardPaths test mode), so the suite never touches the real ~/.local/share.
add_executable(groupstore_io_test
    tests/groupstore_io_test.cpp
    src/core/groupstore_io.cpp
    src/core/group.cpp
)
target_include_directories(groupstore_io_test PRIVATE src)
target_link_libraries(groupstore_io_test PRIVATE Qt6::Core Qt6::Gui) # Gui for QColor
add_test(NAME groupstore_io_test COMMAND groupstore_io_test)

# Commit-engine dry-run verification (ADR-200 #16a): per-op legality, fingerprint drift,
# and volume-boundary classification, with an injected stat function (no disk needed).
add_executable(commit_test
    tests/commit_test.cpp
    src/core/commit.cpp
    src/core/move.cpp
    src/core/group.cpp
    src/platform/identity.cpp
)
target_include_directories(commit_test PRIVATE src)
target_link_libraries(commit_test PRIVATE Qt6::Core Qt6::Gui) # Gui only for QColor in group.h
add_test(NAME commit_test COMMAND commit_test)

# Squarify layout (ADR-301) is now a pure function, so it gets its own unit test.
# QRectF lives in Qt6::Core — no GUI needed.
add_executable(squarify_test
    tests/squarify_test.cpp
    src/ui/squarify.cpp
)
target_include_directories(squarify_test PRIVATE src)
target_link_libraries(squarify_test PRIVATE Qt6::Core)
add_test(NAME squarify_test COMMAND squarify_test)

# Treemap layout cache (ADR-301): geometry separated from painting. Pure Qt Core.
add_executable(treemaplayout_test
    tests/treemaplayout_test.cpp
    src/ui/treemaplayout.cpp
    src/ui/squarify.cpp
)
target_include_directories(treemaplayout_test PRIVATE src)
target_link_libraries(treemaplayout_test PRIVATE Qt6::Core)
add_test(NAME treemaplayout_test COMMAND treemaplayout_test)

# Driver smoke test: run the app headless through a command script — load a tree, zoom,
# screenshot — and assert the capture isn't blank. Exercises the real event paths
# (ui::Driver), so a regression that stops the canvas painting fails here.
add_test(NAME driver_smoke
         COMMAND hfsgraph --script ${CMAKE_SOURCE_DIR}/tests/driver/smoke.txt
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# Lazy deepening: a synthetic fixture (deterministic, no dependence on the repo's
# own layout or on build dirs) generated into the build tree first.
add_test(NAME driver_deepen
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_deepen --depth 2 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/deepen.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_deepen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_deepen"
    TIMEOUT 60)
# Inside a leaf cell: contents anchor to the viewport instead of an off-screen corner.
add_test(NAME driver_zoom_inside
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_zoom --depth 2 --seed 3 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/zoom_inside.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_zoom_inside PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_zoom"
    TIMEOUT 60)
# File selection: inside a leaf, band-select its files, Ctrl-band adds, a click on
# empty body clears.
add_test(NAME driver_select
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_zoom --depth 2 --seed 3 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/select.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_select PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_zoom"
    TIMEOUT 60)
# Fractional fit (ADR-301): a click on a remainder dot selects that file; Shift spans
# grid and band.
add_test(NAME driver_band
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_zoom --depth 2 --seed 3 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/band.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_band PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_zoom"
    TIMEOUT 60)
# Staged file ops (#38): rename, trash and move-file through the ledger; the projection
# shows the result and the queue counts the ops.
add_test(NAME driver_fileops
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_zoom --depth 2 --seed 3 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/fileops.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_fileops PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_zoom"
    TIMEOUT 60)
# Layout focus (#40 spike): zoom in until one cell covers the viewport and the focus
# engages (breadcrumb frames + re-squarified subtree), zoom out until it pops/clears.
add_test(NAME driver_focus
         COMMAND sh -c "python3 '${CMAKE_SOURCE_DIR}/tests/driver/make_synth.py' synth_focus --depth 3 >/dev/null && '$<TARGET_FILE:hfsgraph>' --script '${CMAKE_SOURCE_DIR}/tests/driver/focus.txt'"
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(driver_focus PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_BINARY_DIR}/synth_focus"
    TIMEOUT 60)
set_tests_properties(driver_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=${CMAKE_SOURCE_DIR}/src"
    TIMEOUT 60)
