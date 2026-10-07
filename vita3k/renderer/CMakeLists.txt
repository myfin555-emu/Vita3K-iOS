option(VITA3K_IOS_GER_METAL_FOUNDATION "Build the isolated GE:R native Metal renderer foundation" OFF)

set(RENDERER_VULKAN_SOURCES
	src/vulkan/allocator.cpp
	src/vulkan/context.cpp
	src/vulkan/creation.cpp
	src/vulkan/gxm_to_vulkan.cpp
	src/vulkan/pipeline_cache.cpp
	src/vulkan/renderer.cpp
	src/vulkan/scene.cpp
	src/vulkan/overlay_renderer.cpp
	src/vulkan/screen_filters.cpp
	src/vulkan/screen_renderer.cpp
	src/vulkan/surface_cache.cpp
	src/vulkan/sync_state.cpp
	src/vulkan/texture.cpp
)

if(NOT (VITA3K_BUILD_IOS_UPSTREAM_CORE AND VITA3K_IOS_GER_METAL_FOUNDATION))
	set(RENDERER_VULKAN_SOURCES_ACTIVE ${RENDERER_VULKAN_SOURCES})
else()
	set(RENDERER_VULKAN_SOURCES_ACTIVE)
endif()

add_library(
	renderer
	STATIC

	src/texture/cache.cpp
	src/texture/format.cpp
	src/texture/palette.cpp
	src/texture/pvrt-dec.cpp
	src/texture/replacement.cpp
	src/texture/yuv.cpp

	src/batch.cpp
	src/creation.cpp
	src/renderer.cpp
	src/scene.cpp
	src/shaders.cpp
	src/state_set.cpp
	src/sync.cpp
	src/transfer.cpp
	${RENDERER_VULKAN_SOURCES_ACTIVE}
)

target_include_directories(renderer PUBLIC include)
if(VITA3K_BUILD_IOS_UPSTREAM_CORE AND VITA3K_IOS_GER_METAL_FOUNDATION)
	target_link_libraries(renderer PUBLIC display mem stb shader glutil threads config util overlay)
else()
	target_link_libraries(renderer PUBLIC display mem stb shader glutil threads config util vkutil overlay)
endif()
target_link_libraries(renderer PRIVATE dialog ddspp SDL3::SDL3 stb ffmpeg xxHash::xxhash concurrentqueue)

if(ANDROID)
	target_link_libraries(renderer PRIVATE android adrenotools)
endif()

if(VITA3K_BUILD_IOS_UPSTREAM_CORE AND VITA3K_IOS_GER_METAL_FOUNDATION)
    # The Metal state header contains Objective-C protocol types (id<MTL...>).
    # These renderer translation units are normally C++, so compile them as
    # Objective-C++ for the isolated iOS GE:R target.
    set_source_files_properties(
        src/creation.cpp
        src/renderer.cpp
        src/scene.cpp
        src/state_set.cpp
        PROPERTIES LANGUAGE OBJCXX
    )
    target_sources(renderer PRIVATE src/metal/renderer.mm)
    target_include_directories(renderer PRIVATE include)
    target_link_libraries(renderer PRIVATE spirv-cross-msl "-framework Metal" "-framework QuartzCore")
    target_compile_definitions(renderer PRIVATE
        VITA3K_IOS_GER_METAL_FOUNDATION=1
        VITA3K_IOS_GER_ONLY=1
    )
endif()

if(APPLE AND NOT VITA3K_BUILD_IOS_UPSTREAM_CORE)
	target_sources(renderer PRIVATE src/vulkan/metal_layer.mm)
	target_link_libraries(renderer PRIVATE "-framework QuartzCore" "-framework Cocoa")
elseif(VITA3K_BUILD_IOS_UPSTREAM_CORE)
	if(VITA3K_IOS_GER_ONLY)
		# GE:R uses the native Metal backend and must not pull the Vulkan/MoltenVK
		# runtime into the iOS appliance.
		target_link_libraries(renderer PRIVATE "-framework QuartzCore")
	else()
		target_link_libraries(renderer PRIVATE vulkan "-framework QuartzCore")
	endif()
endif()

if(X11_FOUND)
	target_link_libraries(renderer PRIVATE ${X11_LIBRARIES})
	target_include_directories(renderer PRIVATE ${X11_INCLUDE_DIR})
endif()

if(WAYLAND_FOUND)
	target_link_libraries(renderer PRIVATE ${WAYLAND_LIBRARIES})
	target_include_directories(renderer PRIVATE ${WAYLAND_INCLUDE_DIRS})
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	# silence all vma warnings
	target_compile_options(renderer PRIVATE "-Wno-nullability-completeness")
endif()

# Marshmallow Tracy linking
if(TRACY_ENABLE_ON_CORE_COMPONENTS)
	target_link_libraries(renderer PRIVATE tracy)
endif()
