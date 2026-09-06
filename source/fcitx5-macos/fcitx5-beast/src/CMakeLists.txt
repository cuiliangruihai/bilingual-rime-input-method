add_fcitx5_addon(beast beast.cpp)
target_link_libraries(beast Fcitx5::Core)

if ("${FCITX_ADDON_TYPE}" STREQUAL "SharedLibrary")
    install(TARGETS beast DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
endif()

configure_file(beast.conf.in.in beast.conf.in @ONLY)
fcitx5_translate_desktop_file(${CMAKE_CURRENT_BINARY_DIR}/beast.conf.in beast.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/beast.conf" DESTINATION "${CMAKE_INSTALL_PREFIX}/share/fcitx5/addon")
