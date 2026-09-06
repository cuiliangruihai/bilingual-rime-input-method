cmake_minimum_required(VERSION 3.30)

project(fcitx5-beast VERSION 0.1.0)

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TEST "Build Test" On)

find_package(Gettext REQUIRED)
if (NOT TARGET Fcitx5::Core)
    # Build as an independent project.
    find_package(Fcitx5Core 5.1.12 REQUIRED)
    include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
endif()
find_package(Boost 1.83 REQUIRED)

add_compile_definitions(-DFCITX_GETTEXT_DOMAIN=\"fcitx5-beast\")
fcitx5_add_i18n_definition()

add_subdirectory(src)
add_subdirectory(po)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif()
