# scoot would runtime + game executable
file(GLOB_RECURSE SCOOT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
list(FILTER SCOOT_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# engine + game code as a static library so tests can link against it
add_library(scoot_runtime STATIC ${SCOOT_SOURCES})
target_include_directories(scoot_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scoot_runtime PUBLIC
    SDL3::SDL3-static
    Jolt
    glslang glslang-default-resource-limits
    scoot_shadercross
    scoot_imgui scoot_miniaudio scoot_stb scoot_cgltf scoot_json
    meshoptimizer)
target_compile_definitions(scoot_runtime PUBLIC
    SCOOT_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${SCOOT_DEV_FEATURES}>:SCOOT_DEV_FEATURES=1>
    $<$<CONFIG:Debug>:SCOOT_DEBUG=1>)

if(MSVC)
    target_compile_options(scoot_runtime PRIVATE /W3 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(scoot_runtime PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
                                                  -Wno-deprecated-declarations)
endif()

if(WIN32)
    target_link_libraries(scoot_runtime PUBLIC dbghelp)
endif()

set(SCOOT_EXE_SOURCES main.cpp)
if(WIN32)
    list(APPEND SCOOT_EXE_SOURCES ${CMAKE_SOURCE_DIR}/cmake/scoot_would.rc)
endif()
add_executable(scoot_would WIN32 ${SCOOT_EXE_SOURCES})
target_link_libraries(scoot_would PRIVATE scoot_runtime)
set_target_properties(scoot_would PROPERTIES OUTPUT_NAME "scoot would")

if(MINGW)
    # fully static: no libstdc++ / libgcc / winpthread DLLs next to the exe
    target_link_options(scoot_would PRIVATE -static -static-libgcc -static-libstdc++)
endif()
