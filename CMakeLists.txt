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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(KF6 REQUIRED COMPONENTS CoreAddons)
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
    src/ui/filetypestyle.cpp
    src/ui/grouppanel.cpp
    src/ui/queuepanel.cpp
    src/ui/frameitem.cpp
    src/ui/graphscene.cpp
    src/ui/canvasview.cpp
    src/ui/mainwindow.cpp
)
target_include_directories(hfsgraph PRIVATE src)
target_link_libraries(hfsgraph PRIVATE Qt6::Widgets Qt6::Concurrent KF6::CoreAddons)

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
