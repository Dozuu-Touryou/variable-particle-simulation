cmake_minimum_required(VERSION 3.24)
project(VariableParticleSimulation)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk)
find_package(glm REQUIRED)

# GPU code files
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS "src/shaders/*.vert" "src/shaders/*.frag" "src/shaders/*.comp")

# Name of folder for shaders compiled to SPIR-V
set(COMPILED_SHADERS_DIR shaders_spv)

# Compile shaders and make a list of paths to the compiled spv files
foreach(SHADER_PATH ${SHADER_SOURCES})
  cmake_path(GET SHADER_PATH STEM SHADER_NAME)
  string(REGEX REPLACE ".*\\." "" SHADER_TYPE ${SHADER_PATH})
  set(SHADER_FILENAME "${SHADER_NAME}_${SHADER_TYPE}.spv")
  set(SHADER_OUTPUT_PATH "${CMAKE_BINARY_DIR}/${COMPILED_SHADERS_DIR}/${SHADER_FILENAME}")
  add_custom_command(OUTPUT "${SHADER_OUTPUT_PATH}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${COMPILED_SHADERS_DIR}"
    COMMAND Vulkan::glslc 
      ${SHADER_PATH} 
      --target-env=vulkan1.4
      -o "${SHADER_OUTPUT_PATH}"
    DEPENDS "${SHADER_PATH}"
    COMMENT "Compiling shader: ${SHADER_PATH}")
  list(APPEND COMPILED_SHADERS_LIST ${SHADER_OUTPUT_PATH})
endforeach()

add_custom_target(compile_shaders DEPENDS ${COMPILED_SHADERS_LIST})

# CPU code files
file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")

# Name the executable target
set(EXE_NAME variable_particle_simulation)

# Add source to this project's executable
add_executable(${EXE_NAME} ${EXE_SOURCES})

# Make sure shaders compile before code is linked
add_dependencies(${EXE_NAME} compile_shaders)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC often requires setting a version flag additionally
if (MSVC)
  target_compile_options(${EXE_NAME} PRIVATE /std:c++20)
endif()

target_link_libraries(${EXE_NAME} PRIVATE Vulkan::Vulkan Vulkan::volk SDL3::SDL3 glm::glm)