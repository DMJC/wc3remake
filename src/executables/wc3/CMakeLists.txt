file (GLOB WC3_SOURCE "*.cpp")
file (GLOB WC3_HEADER "*.h")
if(WIN32)
    set(APP_ICON_RESOURCE "${CMAKE_SOURCE_DIR}/resources/debugger.rc")
endif()
add_executable (neowc3 ${WC3_SOURCE} ${WC3_HEADER} ${APP_ICON_RESOURCE})
set(WC3_TARGET_NAME neowc3 PARENT_SCOPE)
target_include_directories(neowc3 PUBLIC ${SC_INCLUDES})
target_include_directories(neowc3 PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(neowc3 PUBLIC ${OPENGL_INCLUDE_DIR})
target_link_libraries(neowc3 PRIVATE wing_commander_3 strike_commander engine realspace commons strike_commander)
target_link_libraries(neowc3 PUBLIC ${SDL2_LIBRARIES})
target_link_libraries(neowc3 PRIVATE imgui::imgui)
target_link_libraries(neowc3 PUBLIC ${OPENGL_LIBRARIES})
target_link_libraries(neowc3 PRIVATE stb)
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_link_options(neowc3 PRIVATE "-Wl,-no_fixup_chains")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR "${CMAKE_CONFIGURATION_TYPES}" MATCHES "Debug")
    add_custom_command(TARGET neowc3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:neowc3>/assets"
        COMMENT "Copie des fichiers resources vers assets (Debug)"
    )
endif()
