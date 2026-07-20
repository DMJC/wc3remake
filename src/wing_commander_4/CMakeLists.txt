file(GLOB LIBRARY_SOURCES "*.cpp")
file(GLOB LIBRARY_HEADERS "*.h")

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL  REQUIRED libavutil)

add_library(wing_commander_4 STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

target_include_directories(wing_commander_4 PUBLIC ${SDL2_INCLUDE_DIRS})
target_include_directories(wing_commander_4 PUBLIC ${OPENGL_INCLUDE_DIR})
target_include_directories(wing_commander_4 PUBLIC ${AVCODEC_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS})
target_link_libraries(wing_commander_4 PRIVATE $<IF:$<TARGET_EXISTS:SDL2_mixer_ext::SDL2_mixer_ext>,SDL2_mixer_ext::SDL2_mixer_ext,SDL2_mixer_ext::SDL2_mixer_ext_Static>)
target_link_libraries(wing_commander_4 PUBLIC ${AVCODEC_LINK_LIBRARIES} ${AVUTIL_LINK_LIBRARIES})
target_include_directories(wing_commander_4 PUBLIC ${LIBRARY_INCLUDES})
target_link_libraries(wing_commander_4 PUBLIC ${LIBRARY_LINKS})
