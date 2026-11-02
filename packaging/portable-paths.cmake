# Map compiler-generated file names and debug paths to portable project paths.
get_filename_component(TPV_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(MSVC)
  add_compile_options("/pathmap:${TPV_SOURCE_ROOT}=." "/pathmap:${CMAKE_BINARY_DIR}=./build")
else()
  add_compile_options("-ffile-prefix-map=${TPV_SOURCE_ROOT}=."
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
endif()
