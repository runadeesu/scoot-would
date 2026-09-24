# ---------------------------------------------------------------------------
# scoot would - third party dependencies
#
# Every dependency is pinned to an exact tag / commit so builds are reproducible.
# Sources are fetched with FetchContent. For offline builds (or to share one
# checkout between several build trees) point SCOOT_DEPS_DIR at a folder that
# contains pre-cloned repositories named exactly like the FetchContent names
# below (SDL3, JoltPhysics, glslang, ...).
#
# All dependencies use permissive licenses (zlib / MIT / BSD / Apache-2.0 /
# public domain). See docs/THIRD_PARTY.md and dist/windows/licenses/.
# ---------------------------------------------------------------------------
include(FetchContent)

set(SCOOT_DEPS_DIR "" CACHE PATH "Optional folder with pre-cloned dependency sources")

function(scoot_dep name repo tag)
    if(SCOOT_DEPS_DIR AND EXISTS "${SCOOT_DEPS_DIR}/${name}")
        string(TOUPPER "${name}" upper)
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${SCOOT_DEPS_DIR}/${name}" CACHE PATH "" FORCE)
    endif()
    # shallow clones only work for branch / tag names, not raw commit hashes
    set(shallow TRUE)
    if(tag MATCHES "^[0-9a-f]+$")
        set(shallow FALSE)
    endif()
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repo}
        GIT_TAG ${tag}
        GIT_SHALLOW ${shallow}
        GIT_PROGRESS FALSE
        ${ARGN})
endfunction()

# versions -------------------------------------------------------------------
scoot_dep(SDL3          https://github.com/libsdl-org/SDL.git               release-3.4.16)
scoot_dep(JoltPhysics   https://github.com/jrouwe/JoltPhysics.git           v5.6.0 SOURCE_SUBDIR Build)
scoot_dep(glslang       https://github.com/KhronosGroup/glslang.git         vulkan-sdk-1.4.357.0)
scoot_dep(SPIRV-Cross   https://github.com/KhronosGroup/SPIRV-Cross.git     vulkan-sdk-1.4.357.0)
scoot_dep(SDL_shadercross https://github.com/libsdl-org/SDL_shadercross.git 1ff05bec573988a98ef9e0260b4da44f512b8367 SOURCE_SUBDIR _none_)
scoot_dep(imgui         https://github.com/ocornut/imgui.git                v1.92.9-docking SOURCE_SUBDIR _none_)
scoot_dep(ImGuizmo      https://github.com/CedricGuillemet/ImGuizmo.git     18cef5e031d8c6973d80284c67f60549fafd78c1 SOURCE_SUBDIR _none_)
scoot_dep(miniaudio     https://github.com/mackron/miniaudio.git            0.11.25 SOURCE_SUBDIR _none_)
scoot_dep(cgltf         https://github.com/jkuhlmann/cgltf.git              v1.15 SOURCE_SUBDIR _none_)
scoot_dep(stb           https://github.com/nothings/stb.git                 2c980bb59875b0d32144a71867fbdebb2f77cd20 SOURCE_SUBDIR _none_)
scoot_dep(json          https://github.com/nlohmann/json.git                v3.12.0 SOURCE_SUBDIR _none_)
scoot_dep(meshoptimizer https://github.com/zeux/meshoptimizer.git           v1.2)

# SDL3 -----------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU ON CACHE BOOL "" FORCE)
set(SDL_RENDER_D3D OFF CACHE BOOL "" FORCE)

# Jolt -----------------------------------------------------------------------
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DISTRIBUTION ON CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
# AVX2 is available on every x64 gaming CPU since 2013; keep SSE4.2 + AVX2.
set(USE_AVX512 OFF CACHE BOOL "" FORCE)

# glslang ----------------------------------------------------------------------
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)

# SPIRV-Cross ------------------------------------------------------------------
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)

# meshoptimizer ----------------------------------------------------------------
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 JoltPhysics glslang SPIRV-Cross SDL_shadercross imgui ImGuizmo
                           miniaudio cgltf stb json meshoptimizer)

# SDL_shadercross (SPIR-V -> HLSL -> DXBC for the D3D12 backend) -------------------
add_library(scoot_shadercross STATIC ${sdl_shadercross_SOURCE_DIR}/src/SDL_shadercross.c)
target_include_directories(scoot_shadercross PUBLIC ${sdl_shadercross_SOURCE_DIR}/include)
target_link_libraries(scoot_shadercross PUBLIC SDL3::SDL3-static PRIVATE spirv-cross-c)
set_target_properties(scoot_shadercross PROPERTIES C_STANDARD 99 LINKER_LANGUAGE CXX)

# Dear ImGui + ImGuizmo --------------------------------------------------------
add_library(scoot_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(scoot_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(scoot_imgui PUBLIC SDL3::SDL3-static)

# single header libraries --------------------------------------------------------
add_library(scoot_miniaudio STATIC ${CMAKE_SOURCE_DIR}/external/miniaudio_impl.c)
target_include_directories(scoot_miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
if(UNIX)
    target_link_libraries(scoot_miniaudio PUBLIC dl pthread m)
endif()

add_library(scoot_stb STATIC ${CMAKE_SOURCE_DIR}/external/stb_impl.c)
target_include_directories(scoot_stb PUBLIC ${stb_SOURCE_DIR})

add_library(scoot_cgltf STATIC ${CMAKE_SOURCE_DIR}/external/cgltf_impl.c)
target_include_directories(scoot_cgltf PUBLIC ${cgltf_SOURCE_DIR})

add_library(scoot_json INTERFACE)
target_include_directories(scoot_json INTERFACE ${json_SOURCE_DIR}/single_include)
