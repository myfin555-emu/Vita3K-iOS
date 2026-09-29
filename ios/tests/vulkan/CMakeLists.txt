cmake_minimum_required(VERSION 3.22)
project(Vita3KVulkanSurfaceTests LANGUAGES CXX)
enable_testing()
find_package(Vulkan REQUIRED)
get_filename_component(REPO_ROOT "../../.." ABSOLUTE)

add_library(vulkan_test_headers INTERFACE)
target_compile_features(vulkan_test_headers INTERFACE cxx_std_20)
target_include_directories(vulkan_test_headers INTERFACE
    ${REPO_ROOT}/vita3k/renderer/include
    ${REPO_ROOT}/vita3k/vkutil/include
    ${Vulkan_INCLUDE_DIRS})

# Each header must configure Vulkan-Hpp correctly when it is the first include.
# Do not supply VULKAN_HPP_* definitions here: doing so masked an iOS build failure.
foreach(header IN ITEMS renderer/vulkan/render_pass_dependencies.h
                        renderer/vulkan/texture_descriptor_cache.h vkutil/vkutil.h)
    string(MAKE_C_IDENTIFIER "${header}" target)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${target}.cpp")
    file(WRITE "${source}" "#include <${header}>\n#include <vkutil/vkutil.h>\n#include <type_traits>\nstatic_assert(std::is_aggregate_v<vk::DescriptorImageInfo>);\nstatic_assert(std::is_aggregate_v<vma::AllocationCreateInfo>);\nconst vk::CommandBufferAllocateInfo command_info{.commandBufferCount = 1};\nconst vma::AllocationCreateInfo allocation_info{.usage = vma::MemoryUsage::eAuto};\n")
    add_library(${target} OBJECT "${source}")
    target_link_libraries(${target} PRIVATE vulkan_test_headers)
    target_include_directories(${target} PRIVATE
        ${REPO_ROOT}/vita3k/util/include
        ${REPO_ROOT}/external/boost
        ${REPO_ROOT}/external/fmt/include
        ${REPO_ROOT}/external/VulkanMemoryAllocator-Hpp/include
        ${REPO_ROOT}/external/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include)
endforeach()

add_executable(vulkan_surface_tests VulkanSurfaceTests.cpp)
target_link_libraries(vulkan_surface_tests PRIVATE vulkan_test_headers Vulkan::Vulkan ${CMAKE_DL_LIBS})
add_test(NAME vulkan_surface_tests COMMAND vulkan_surface_tests)

# Prefer a deterministic headless driver for this suite. Mesa packages use both
# manifest names; a filename copied from another distribution may not exist.
# A cache override also lets developers select a specific installed ICD.
find_file(VITA3K_VULKAN_TEST_ICD
    NAMES lvp_icd.json lvp_icd.${CMAKE_SYSTEM_PROCESSOR}.json
    PATHS /usr/share/vulkan/icd.d /usr/local/share/vulkan/icd.d
    DOC "Vulkan ICD manifest used by the renderer host tests")
if(VITA3K_VULKAN_TEST_ICD)
    if(NOT EXISTS "${VITA3K_VULKAN_TEST_ICD}")
        message(FATAL_ERROR "Vulkan test ICD does not exist: ${VITA3K_VULKAN_TEST_ICD}")
    endif()
    message(STATUS "Vulkan test ICD: ${VITA3K_VULKAN_TEST_ICD}")
    # Set both names for old/new loaders, overriding stale parent/CI settings
    # only for this test process. Other tests and the emulator are unaffected.
    set_tests_properties(vulkan_surface_tests PROPERTIES ENVIRONMENT
        "VK_DRIVER_FILES=${VITA3K_VULKAN_TEST_ICD};VK_ICD_FILENAMES=${VITA3K_VULKAN_TEST_ICD}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME surface_cache_production_tests COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/SurfaceCacheProductionTests.py ${CMAKE_CXX_COMPILER} ${Vulkan_INCLUDE_DIR})
set_tests_properties(vulkan_surface_tests surface_cache_production_tests PROPERTIES TIMEOUT 60)
