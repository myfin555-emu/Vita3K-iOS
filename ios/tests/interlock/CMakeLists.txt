cmake_minimum_required(VERSION 3.22)
project(InterlockDiscardTests LANGUAGES CXX)
enable_testing()
# Use the repository-pinned glslang sources; no Apple SDK is needed.
set(GLSLANG_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../../external/glslang" CACHE PATH "glslang source tree")
find_program(SPIRV_VAL spirv-val REQUIRED)
find_program(SPIRV_DIS spirv-dis REQUIRED)
find_program(SPIRV_CROSS spirv-cross REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(interlock_fixture InterlockFixture.cpp
    ${GLSLANG_SOURCE}/SPIRV/SpvBuilder.cpp
    ${GLSLANG_SOURCE}/SPIRV/SpvPostProcess.cpp
    ${GLSLANG_SOURCE}/SPIRV/InReadableOrder.cpp
    ${GLSLANG_SOURCE}/SPIRV/Logger.cpp
    ${GLSLANG_SOURCE}/SPIRV/doc.cpp)
target_compile_features(interlock_fixture PRIVATE cxx_std_17)
target_include_directories(interlock_fixture PRIVATE
    ${GLSLANG_SOURCE} ../../../vita3k/shader/include)
add_test(NAME interlock_discard_tests COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_LIST_DIR}/InterlockDiscardTests.py
    $<TARGET_FILE:interlock_fixture> ${SPIRV_VAL} ${SPIRV_DIS} ${SPIRV_CROSS})
