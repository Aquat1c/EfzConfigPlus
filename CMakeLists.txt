cmake_minimum_required(VERSION 3.20)
project(efz_config C)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WINVER=0x0501 _WIN32_WINNT=0x0501
                        _CRT_SECURE_NO_WARNINGS)
if(MSVC AND NOT CMAKE_GENERATOR_TOOLSET MATCHES "v141_xp")
    message(WARNING "efz_config targets Windows XP: configure with -T v141_xp. "
                    "A newer toolset produces an exe that does not start on XP.")
endif()

add_executable(efz_config WIN32
    src/efz_config.c
    src/efz_config_ini.c
    src/efz_config_mods.c
    src/efz_config_resources.rc
    ControllsFix/padmap.c
    third_party/miniz/miniz.c
)

target_link_options(efz_config PRIVATE /MANIFEST:NO)
set_target_properties(efz_config PROPERTIES OUTPUT_NAME "EfzConfig+")

set_source_files_properties(third_party/miniz/miniz.c PROPERTIES
    COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_TIME;MINIZ_NO_ARCHIVE_WRITING_APIS;MINIZ_NO_ZLIB_COMPATIBLE_NAMES"
    COMPILE_OPTIONS "/w")
target_compile_definitions(efz_config PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

target_link_libraries(efz_config PRIVATE dinput8 shell32 user32 gdi32 comctl32 comdlg32
                      advapi32 ole32 wininet)

enable_testing()
add_executable(test_ini tools/test_ini.c src/efz_config_ini.c)
add_test(NAME ini COMMAND test_ini)

add_executable(test_mods tools/test_mods.c src/efz_config_mods.c third_party/miniz/miniz.c)
target_compile_definitions(test_mods PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_link_libraries(test_mods PRIVATE shell32 wininet advapi32)
add_library(test_schema_dll SHARED tools/test_schema_dll.c)
add_test(NAME mods COMMAND test_mods $<TARGET_FILE:test_schema_dll>)

add_subdirectory(ControllsFix)
