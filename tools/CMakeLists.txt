# host-side content tools (not shipped)
add_executable(scoot_assetgen
    assetgen/main.cpp
    assetgen/gltf_writer.cpp
    assetgen/rider_gen.cpp
    assetgen/audio_gen.cpp
    assetgen/texture_gen.cpp)
target_link_libraries(scoot_assetgen PRIVATE scoot_runtime)
target_include_directories(scoot_assetgen PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/assetgen)
