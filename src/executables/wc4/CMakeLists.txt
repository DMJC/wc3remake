file (GLOB WC4_SOURCE "*.cpp")
file (GLOB WC4_HEADER "*.h")
if(WIN32)
    set(APP_ICON_RESOURCE "${CMAKE_SOURCE_DIR}/resources/debugger.rc")
endif()
add_executable (neowc4 ${WC4_SOURCE} ${WC4_HEADER} ${APP_ICON_RESOURCE})
set(WC4_TARGET_NAME neowc4 PARENT_SCOPE)
target_include_directories(neowc4 PUBLIC ${SC_INCLUDES})
target_include_directories(neowc4 PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(neowc4 PUBLIC ${OPENGL_INCLUDE_DIR})
target_link_libraries(neowc4 PRIVATE wing_commander_4 strike_commander engine realspace commons strike_commander)
target_link_libraries(neowc4 PUBLIC ${SDL2_LIBRARIES})
target_link_libraries(neowc4 PRIVATE imgui::imgui)
target_link_libraries(neowc4 PUBLIC ${OPENGL_LIBRARIES})
target_link_libraries(neowc4 PRIVATE stb)
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_link_options(neowc4 PRIVATE "-Wl,-no_fixup_chains")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR "${CMAKE_CONFIGURATION_TYPES}" MATCHES "Debug")
    add_custom_command(TARGET neowc4 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:neowc4>/assets"
        COMMENT "Copie des fichiers resources vers assets (Debug)"
    )
endif()
