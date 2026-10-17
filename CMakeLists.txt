cmake_minimum_required(VERSION 3.16)
project(ThePerfectV VERSION 1.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 3.4 CONFIG QUIET)
if(NOT TARGET SDL3::SDL3)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET GLOBAL sdl3>=3.4.0)
  add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
endif()
if(MSVC)
  add_compile_options(/W4 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(APPLE)
    # Apple Clang contracts a += b*c into FMA on arm64 by default. The
    # different rounding adds a snow-grid row at fractional hill scales.
    # Match the separate operations used by the original Linux/Windows build.
    add_compile_options(-ffp-contract=off)
  endif()
endif()
set(TPV_PRECOMPILED_SHADER_DIR "" CACHE PATH "Directory containing scene.vert.inc and scene.frag.inc")
if(NOT TPV_PRECOMPILED_SHADER_DIR)
 find_program(GLSLC glslc)
 if(NOT GLSLC)
  find_program(GLSLANG_VALIDATOR glslangValidator)
  if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "Install glslc (shaderc) or glslangValidator to compile the Vulkan shaders.")
  endif()
 endif()
endif()
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)
if(TPV_PRECOMPILED_SHADER_DIR)
  set(TPV_SHADER_DIR "${TPV_PRECOMPILED_SHADER_DIR}")
  foreach(stage vert frag)
    if(NOT EXISTS "${TPV_SHADER_DIR}/scene.${stage}.inc")
      message(FATAL_ERROR "Missing precompiled shader: scene.${stage}.inc")
    endif()
  endforeach()
else()
 set(TPV_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
 foreach(stage vert frag)
  if(GLSLC)
    add_custom_command(OUTPUT "${TPV_SHADER_DIR}/scene.${stage}.inc"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${TPV_SHADER_DIR}"
      COMMAND "${GLSLC}" -mfmt=c --target-env=vulkan1.0
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.${stage}"
        -o "${TPV_SHADER_DIR}/scene.${stage}.inc"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.${stage}"
      VERBATIM)
  else()
    add_custom_command(OUTPUT "${TPV_SHADER_DIR}/scene.${stage}.inc"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${TPV_SHADER_DIR}"
      COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.${stage}"
        -o "${TPV_SHADER_DIR}/scene.${stage}.spv"
      COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${TPV_SHADER_DIR}/scene.${stage}.spv"
        "-DOUTPUT=${TPV_SHADER_DIR}/scene.${stage}.inc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.cmake"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.${stage}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.cmake"
      VERBATIM)
  endif()
 endforeach()
endif()
add_custom_target(tpv-shaders DEPENDS
  "${TPV_SHADER_DIR}/scene.vert.inc" "${TPV_SHADER_DIR}/scene.frag.inc")
add_library(tpv_vulkan src/draw_commands.cpp src/vulkan_backend.cpp src/mesh_geometry.cpp)
add_dependencies(tpv_vulkan tpv-shaders)
target_include_directories(tpv_vulkan PUBLIC src PRIVATE "${TPV_SHADER_DIR}")
target_link_libraries(tpv_vulkan PUBLIC SDL3::SDL3 Threads::Threads)
add_library(ski_physics src/nationality.cpp src/physics.cpp src/original_physics.cpp src/original_pose.cpp src/original_scoring.cpp src/original_controls.cpp src/original_wind.cpp src/original_ai.cpp src/original_outrun.cpp src/competition.cpp src/competition_storage.cpp src/original_replay.cpp src/original_skeleton.cpp)
target_include_directories(ski_physics PUBLIC src)
add_library(tpv_audio src/audio.cpp src/crowd_audio.cpp)
target_link_libraries(tpv_audio PUBLIC ski_physics SDL3::SDL3)
add_executable(the-perfect-v src/main.cpp src/renderer.cpp src/renderer_environment.cpp src/renderer_inrun.cpp src/renderer_athlete.cpp src/renderer_effects.cpp src/renderer_ui.cpp src/storage.cpp)
target_link_libraries(the-perfect-v PRIVATE ski_physics tpv_vulkan tpv_audio Freetype::Freetype)
target_compile_definitions(the-perfect-v PRIVATE SJP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
include(CTest)
option(TPV_BUILD_RENDER_PROBE "Build explicitly invoked graphics diagnostics" OFF)
if(TPV_BUILD_RENDER_PROBE)
  add_executable(render-probe tools/render_probe.cpp src/storage.cpp
    src/renderer.cpp src/renderer_environment.cpp src/renderer_inrun.cpp src/renderer_athlete.cpp
    src/renderer_effects.cpp src/renderer_ui.cpp)
  target_link_libraries(render-probe PRIVATE ski_physics tpv_vulkan Freetype::Freetype)
  add_executable(snow-glitter-probe tools/snow_glitter_probe.cpp
    src/renderer.cpp src/renderer_environment.cpp src/renderer_inrun.cpp src/renderer_athlete.cpp
    src/renderer_effects.cpp src/renderer_ui.cpp)
  target_link_libraries(snow-glitter-probe PRIVATE ski_physics tpv_vulkan Freetype::Freetype)
endif()
# Reference recordings are local development data and are not distributed.
# Keep the complete existing suite available when those fixtures are present.
set(SJP_REFERENCE_TESTS_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reference/EXAMPLE.RPL" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/captures/hill-extraction/static/curve-samples.csv")
  set(SJP_REFERENCE_TESTS_DEFAULT ON)
endif()
option(SJP_REFERENCE_TESTS "Enable tests requiring local reference files and captures" ${SJP_REFERENCE_TESTS_DEFAULT})
if(BUILD_TESTING)
  add_executable(cheat-tests tests/cheat_tests.cpp src/storage.cpp)
  target_link_libraries(cheat-tests PRIVATE ski_physics)
  add_test(NAME cheats COMMAND cheat-tests)
  add_executable(platform-file-tests tests/platform_file_tests.cpp)
  target_include_directories(platform-file-tests PRIVATE src)
  add_test(NAME platform-files COMMAND platform-file-tests)
  add_executable(hill-record-tests tests/hill_record_tests.cpp src/storage.cpp)
  target_link_libraries(hill-record-tests PRIVATE ski_physics)
  add_test(NAME hill-records COMMAND hill-record-tests)
  add_executable(nationality-tests tests/nationality_tests.cpp)
  target_link_libraries(nationality-tests PRIVATE ski_physics SDL3::SDL3)
  target_compile_definitions(nationality-tests PRIVATE SJP_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME nationality COMMAND nationality-tests)
  add_executable(mesh-geometry-tests tests/mesh_geometry_tests.cpp)
  target_link_libraries(mesh-geometry-tests PRIVATE tpv_vulkan)
  add_test(NAME mesh-geometry COMMAND mesh-geometry-tests)
  add_executable(gpu-frame-tests tests/gpu_frame_tests.cpp
    src/renderer.cpp src/renderer_environment.cpp src/renderer_inrun.cpp src/renderer_athlete.cpp
    src/renderer_effects.cpp src/renderer_ui.cpp)
  target_link_libraries(gpu-frame-tests PRIVATE tpv_vulkan ski_physics Freetype::Freetype)
  target_compile_definitions(gpu-frame-tests PRIVATE SJP_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME gpu-frames COMMAND gpu-frame-tests)
  add_executable(crowd-audio-tests tests/crowd_audio_tests.cpp)
  target_link_libraries(crowd-audio-tests PRIVATE tpv_audio)
  target_compile_definitions(crowd-audio-tests PRIVATE SJP_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME crowd-audio COMMAND crowd-audio-tests)
  set_tests_properties(crowd-audio PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  add_executable(environment-tests tests/environment_tests.cpp src/storage.cpp)
  target_link_libraries(environment-tests PRIVATE ski_physics SDL3::SDL3)
  add_test(NAME environment COMMAND environment-tests)
  add_executable(camera-path-tests tests/camera_path_tests.cpp)
  target_link_libraries(camera-path-tests PRIVATE ski_physics)
  add_test(NAME camera-paths COMMAND camera-path-tests)
  add_executable(presentation-timing-tests tests/presentation_timing_tests.cpp)
  target_include_directories(presentation-timing-tests PRIVATE src)
  add_test(NAME presentation-timing COMMAND presentation-timing-tests)
  # CPU command model supplies drawing/SDL entry points; it does not use a GPU.
  add_executable(renderer-command-tests tests/renderer_command_tests.cpp
    src/renderer.cpp src/renderer_environment.cpp src/renderer_inrun.cpp src/renderer_athlete.cpp
    src/renderer_effects.cpp src/renderer_ui.cpp)
  target_link_libraries(renderer-command-tests PRIVATE ski_physics Freetype::Freetype)
  target_link_libraries(renderer-command-tests PRIVATE SDL3::SDL3)
  target_compile_definitions(renderer-command-tests PRIVATE SJP_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME renderer-commands COMMAND renderer-command-tests)
  add_executable(render-interpolation-tests tests/render_interpolation_tests.cpp)
  target_link_libraries(render-interpolation-tests PRIVATE ski_physics)
  add_test(NAME render-interpolation COMMAND render-interpolation-tests)
  add_executable(snow-mesh-tests tests/snow_mesh_tests.cpp)
  target_link_libraries(snow-mesh-tests PRIVATE ski_physics)
  add_test(NAME snow-mesh COMMAND snow-mesh-tests)
  add_executable(jump-feedback-tests tests/jump_feedback_tests.cpp)
  target_link_libraries(jump-feedback-tests PRIVATE ski_physics)
  add_test(NAME jump-feedback COMMAND jump-feedback-tests)
  add_executable(fall-presentation-tests tests/fall_presentation_tests.cpp src/renderer_athlete.cpp)
  target_link_libraries(fall-presentation-tests PRIVATE ski_physics SDL3::SDL3)
  add_test(NAME fall-presentation COMMAND fall-presentation-tests)
  add_executable(physics-tests tests/physics_tests.cpp src/storage.cpp)
  target_link_libraries(physics-tests PRIVATE ski_physics)
  add_test(NAME physics-and-storage COMMAND physics-tests)
  add_executable(storage-migration-tests tests/storage_migration_tests.cpp src/storage.cpp)
  target_link_libraries(storage-migration-tests PRIVATE ski_physics)
  add_test(NAME storage-migration COMMAND storage-migration-tests)
  if(SJP_REFERENCE_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reference/EXAMPLE.RPL" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/captures/hill-extraction/static/curve-samples.csv")
      message(FATAL_ERROR "Reference fixtures are not distributed. Supply the local reference/captures directories or configure with -DSJP_REFERENCE_TESTS=OFF.")
    endif()
    add_executable(competition-tests tests/competition_tests.cpp)
    target_link_libraries(competition-tests PRIVATE ski_physics)
    add_test(NAME original-competition COMMAND competition-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-competition/rank-golden.csv")
    add_executable(original-replay-probe tools/original_replay_probe.cpp)
    target_link_libraries(original-replay-probe PRIVATE ski_physics)
    add_executable(original-replay-tests tests/original_replay_tests.cpp)
    target_link_libraries(original-replay-tests PRIVATE ski_physics)
    add_test(NAME original-replay COMMAND original-replay-tests "${CMAKE_CURRENT_SOURCE_DIR}/reference/EXAMPLE.RPL" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-replay/example-runtime/original.csv")
    add_executable(original-skeleton-tests tests/original_skeleton_tests.cpp)
    target_link_libraries(original-skeleton-tests PRIVATE ski_physics)
    add_test(NAME original-skeleton COMMAND original-skeleton-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-skeleton/skeleton-golden.csv")
    add_test(NAME original-skeleton-runtime COMMAND original-skeleton-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-skeleton/actual-runtime-golden.csv")
    add_executable(original-run-probe tools/original_run_probe.cpp)
    target_link_libraries(original-run-probe PRIVATE ski_physics)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME original-complete-runtime
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/original_runtime_tests.py"
        "$<TARGET_FILE:original-run-probe>" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-full-runtime/manifest.json")
    add_test(NAME original-replay-runtime
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/original_replay_runtime_tests.py"
        "$<TARGET_FILE:original-replay-probe>" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-replay/all32/manifest.json")
    add_test(NAME original-expanded-runtime
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/original_runtime_tests.py"
        "$<TARGET_FILE:original-run-probe>" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-expanded-runtime/manifest.json")
    add_executable(hill-profiles-tests tests/hill_profiles_tests.cpp)
    target_link_libraries(hill-profiles-tests PRIVATE ski_physics)
    target_compile_definitions(hill-profiles-tests PRIVATE SJP_PROFILE_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/captures/hill-extraction/static/curve-samples.csv")
    target_compile_definitions(hill-profiles-tests PRIVATE SJP_DISTANCE_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/captures/hill-extraction/static/curve-distances.csv")
    add_test(NAME original-hill-profiles COMMAND hill-profiles-tests)
    add_executable(original-physics-tests tests/original_physics_tests.cpp)
    target_link_libraries(original-physics-tests PRIVATE ski_physics)
    target_compile_definitions(original-physics-tests PRIVATE SJP_FLIGHT_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-physics/golden-flight.csv")
    target_compile_definitions(original-physics-tests PRIVATE SJP_INRUN_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-physics/golden-inrun.csv")
    add_test(NAME original-flight-physics COMMAND original-physics-tests)
    add_executable(original-pose-tests tests/original_pose_tests.cpp)
    target_link_libraries(original-pose-tests PRIVATE ski_physics)
    target_compile_definitions(original-pose-tests PRIVATE SJP_POSE_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-controls/pose-golden.csv")
    add_test(NAME original-pose COMMAND original-pose-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-controls/pose-golden.csv")
    add_executable(original-scoring-tests tests/original_scoring_tests.cpp)
    target_link_libraries(original-scoring-tests PRIVATE ski_physics)
    add_test(NAME original-scoring COMMAND original-scoring-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-controls/scoring-golden.csv")
    add_executable(original-controls-tests tests/original_controls_tests.cpp)
    target_link_libraries(original-controls-tests PRIVATE ski_physics)
    add_test(NAME original-controls COMMAND original-controls-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-controls/controls-golden.csv")
    add_executable(original-wind-tests tests/original_wind_tests.cpp)
    target_link_libraries(original-wind-tests PRIVATE ski_physics)
    add_test(NAME original-wind COMMAND original-wind-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-physics/wind")
    add_executable(original-ai-tests tests/original_ai_tests.cpp)
    target_link_libraries(original-ai-tests PRIVATE ski_physics)
    add_test(NAME original-ai COMMAND original-ai-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-ai/ai-skills-golden.csv" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-ai/ai-initializer-golden.csv" "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-ai/ai-poll-golden.csv")
    add_executable(original-outrun-tests tests/original_outrun_tests.cpp)
    target_link_libraries(original-outrun-tests PRIVATE ski_physics)
    add_test(NAME original-outrun COMMAND original-outrun-tests "${CMAKE_CURRENT_SOURCE_DIR}/captures/fidelity-outrun")
  else()
    message(STATUS "External reference fixtures disabled; building the standalone test suites")
  endif()
endif()

if(APPLE)
  set(TPV_APP_CONTENTS "The Perfect V.app/Contents")
  set(TPV_INSTALL_BIN "${TPV_APP_CONTENTS}/MacOS")
  set(TPV_INSTALL_DATA "${TPV_APP_CONTENTS}/Resources")
  set_target_properties(the-perfect-v PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "The Perfect V"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.chl84.the-perfect-v"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    OUTPUT_NAME "The Perfect V"
    INSTALL_RPATH "@executable_path/../Frameworks")
elseif(WIN32)
  set(TPV_INSTALL_BIN .)
  set(TPV_INSTALL_DATA .)
  if(MSVC)
    set_target_properties(the-perfect-v PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(the-perfect-v PRIVATE /ENTRY:mainCRTStartup /MANIFEST:EMBED
      "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/app.manifest")
  endif()
else()
  set(TPV_INSTALL_BIN bin)
  set(TPV_INSTALL_DATA share/the-perfect-v)
endif()
install(TARGETS the-perfect-v
  BUNDLE DESTINATION . RUNTIME DESTINATION "${TPV_INSTALL_BIN}")
# Runtime owns share/the-perfect-v/saves; installation updates assets only.
install(DIRECTORY assets DESTINATION "${TPV_INSTALL_DATA}")

install(FILES LICENSE README.md DESTINATION "${TPV_INSTALL_DATA}")
install(FILES docs/DEVELOPMENT.md docs/vulkan-sdl3-migration.md DESTINATION "${TPV_INSTALL_DATA}/docs")
install(DIRECTORY docs/screenshots DESTINATION "${TPV_INSTALL_DATA}/docs")
