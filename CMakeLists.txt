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

find_package(Qt6 REQUIRED COMPONENTS Widgets)
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
    # src/ui/nodeitem.cpp  — parked: the node-link card + file-viewer, kept for a
    #   future click-to-inspect detail panel; not built in the treemap-only canvas.
    src/ui/treemapitem.cpp
    src/ui/graphscene.cpp
    src/ui/canvasview.cpp
    src/ui/mainwindow.cpp
)
target_include_directories(hfsgraph PRIVATE src)
target_link_libraries(hfsgraph PRIVATE Qt6::Widgets KF6::CoreAddons)

include(GNUInstallDirs)
install(TARGETS hfsgraph DESTINATION ${CMAKE_INSTALL_BINDIR})
