file (GLOB SC_SOURCE "*.cpp")
file (GLOB SC_HEADER "*.h")
if(WIN32)
    set(APP_ICON_RESOURCE "${CMAKE_SOURCE_DIR}/resources/debugger.rc")
endif()
find_package(nfd CONFIG QUIET)

add_executable (debugger ${SC_SOURCE} ${SC_HEADER} ${APP_ICON_RESOURCE})
set(DEBUGGER_TARGET_NAME debugger PARENT_SCOPE)
target_include_directories(debugger PUBLIC ${SC_INCLUDES})
target_include_directories(debugger PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(debugger PUBLIC ${OPENGL_INCLUDE_DIR})
target_include_directories(debugger PUBLIC ${OPENGL_INCLUDE_DIR})
target_link_libraries(debugger PRIVATE strike_commander engine realspace commons pacific_strike)
target_link_libraries(debugger PUBLIC ${SDL2_LIBRARIES})
target_link_libraries(debugger PRIVATE imgui::imgui )
if(nfd_FOUND)
    target_link_libraries(debugger PRIVATE nfd::nfd)
    target_compile_definitions(debugger PRIVATE HAS_NFD=1)
endif()
target_link_libraries(debugger PUBLIC ${OPENGL_LIBRARIES})
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_link_options(debugger PRIVATE "-Wl,-no_fixup_chains")
endif()
# Copie des fichiers resources vers assets en mode Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR "${CMAKE_CONFIGURATION_TYPES}" MATCHES "Debug")
    add_custom_command(TARGET debugger POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:debugger>/assets"
        COMMENT "Copie des fichiers resources vers assets (Debug) $<TARGET_FILE_DIR:debugger>/assets"
    )
endif()