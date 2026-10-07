cmake_minimum_required(VERSION 3.22)

# Build with the iOS 26 SDK, with runtime fallbacks back to iOS 16.
set(VITA3K_IOS_DEPLOYMENT_TARGET "16.0" CACHE STRING "Minimum supported iOS version")
set(VITA3K_IOS_BUNDLE_ID "org.vita3k.experimental.ios" CACHE STRING "Application bundle identifier")
option(VITA3K_IOS_LINK_CORE "Link the first upstream Vita3K core slice" ON)
option(VITA3K_IOS_GER_ONLY "Build the iOS frontend as a God Eater Resurrection-only appliance" OFF)
option(VITA3K_IOS_GER_METAL_FOUNDATION "Compile the isolated GE:R native Metal renderer foundation" OFF)
if(VITA3K_IOS_GER_ONLY)
    set(VITA3K_IOS_GER_METAL_FOUNDATION ON CACHE BOOL "Enable GE:R native Metal renderer" FORCE)
endif()

if(VITA3K_IOS_GER_METAL_FOUNDATION AND NOT VITA3K_BUILD_IOS_UPSTREAM_CORE)
    message(FATAL_ERROR "VITA3K_IOS_GER_METAL_FOUNDATION requires VITA3K_BUILD_IOS_UPSTREAM_CORE=ON.")
endif()

if(VITA3K_IOS_GER_ONLY AND NOT VITA3K_BUILD_IOS_UPSTREAM_CORE)
    message(FATAL_ERROR "VITA3K_IOS_GER_ONLY requires VITA3K_BUILD_IOS_UPSTREAM_CORE=ON.")
endif()

if(NOT VITA3K_IOS_LINK_CORE)
    message(FATAL_ERROR
        "Bootstrap-only builds are no longer supported. Set VITA3K_IOS_LINK_CORE=ON.")
endif()

if(VITA3K_BUILD_IOS_UPSTREAM_CORE)
    # Stage-2 upstream-core application: SDL3 owns the UIKit application
    # lifecycle and the frontend boots titles through the real emulator.
    # src/Swift/*.swift is the SwiftUI frontend; TsubomiBridge.mm is the only
    # thing it talks to (a plain Objective-C facade - see the bridging header
    # below for why Swift never sees the C++ core directly).
    file(GLOB VITA3K_IOS_SWIFT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Swift/*.swift")
    set(VITA3K_IOS_CUELUME_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cuelume-LICENSE.txt")
    add_executable(Vita3KiOS MACOSX_BUNDLE
        src/NativeFrontend.mm
        src/IOSKeyboard.mm
        src/IOSJIT.mm
        src/TsubomiBridge.mm
        src/UpstreamMain.cpp
        src/PerformanceOptimizations.mm
        src/VirtualController.mm
        ${VITA3K_IOS_SWIFT_SOURCES}
        ${VITA3K_IOS_CUELUME_LICENSE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets
    )
    # Compile the app-icon asset catalog (Xcode generator runs actool) so the
    # Tsubomi flower is the home-screen icon.
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(${VITA3K_IOS_CUELUME_LICENSE}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
    target_include_directories(Vita3KiOS PRIVATE include)
    target_compile_features(Vita3KiOS PRIVATE cxx_std_23)
    # -Wall/-Wextra are C-family flags; passing them to swiftc is an error, so
    # scope them to the languages that understand them.
    target_compile_options(Vita3KiOS PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wall>
        $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wextra>)
    target_compile_definitions(Vita3KiOS PRIVATE VITA3K_IOS_UPSTREAM_CORE=1)
    if(VITA3K_IOS_GER_ONLY)
        # The option is a CMake variable; these sources consume it as a
        # preprocessor macro to select the Metal-only GE:R path.
        target_compile_definitions(Vita3KiOS PRIVATE VITA3K_IOS_GER_ONLY=1)
        target_compile_definitions(app PRIVATE VITA3K_IOS_GER_ONLY=1)
    endif()
    # Swift <-> Objective-C bridging.
    #   * The bridging header is what Swift sees: it exposes ONLY
    #     TsubomiBridge.h, a pure Objective-C facade. Swift deliberately does
    #     not get C++ interop here - the core's headers are heavy C++23 and
    #     importing them into every Swift file is both slow and fragile.
    #   * PRODUCT_MODULE_NAME is pinned so the generated header the
    #     Objective-C++ side imports has a stable name ("Tsubomi-Swift.h").
    #     Left to default it would be derived from OUTPUT_NAME ("Vita3K-iOS")
    #     with the hyphen mangled to an underscore.
    set_target_properties(Vita3KiOS PROPERTIES
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
        XCODE_ATTRIBUTE_PRODUCT_MODULE_NAME "Tsubomi"
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Swift/Tsubomi-Bridging-Header.h"
        XCODE_ATTRIBUTE_SWIFT_OPTIMIZATION_LEVEL[variant=Release] "-O"
        XCODE_ATTRIBUTE_SWIFT_OPTIMIZATION_LEVEL[variant=Debug] "-Onone"
        # Whole-module in Release lets the optimizer specialize across files,
        # which matters for SwiftUI view bodies.
        XCODE_ATTRIBUTE_SWIFT_COMPILATION_MODE[variant=Release] "wholemodule")
    target_link_libraries(Vita3KiOS PRIVATE
        vita3k
        audio
        # IOSKeyboard.mm reads the common dialog and IME state directly.
        dialog
        ime
        io
        miniz
        np
        packages
        SDL3::SDL3
        "-framework SwiftUI"
        "-framework AVFoundation"
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework GameController"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework UIKit"
        "-framework UniformTypeIdentifiers"
    )
    # The renderer loads its screen-filter SPIR-V from
    # <static assets>/shaders-builtin at runtime; ship it inside the bundle.
    add_custom_command(TARGET Vita3KiOS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/vita3k/shaders-builtin"
            "$<TARGET_BUNDLE_CONTENT_DIR:Vita3KiOS>/shaders-builtin")
else()
    add_subdirectory(core)

    add_executable(Vita3KiOS MACOSX_BUNDLE
        src/AppDelegate.mm
        src/IOSInputAdapter.mm
        src/IOSLogger.mm
        src/IOSMetalRenderer.mm
        src/main.mm
    )

    target_include_directories(Vita3KiOS PRIVATE include)
    target_compile_features(Vita3KiOS PRIVATE cxx_std_23)
    target_compile_options(Vita3KiOS PRIVATE
        $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>
        -Wall
        -Wextra
    )
    target_compile_definitions(Vita3KiOS PRIVATE VITA3K_PLATFORM_IOS=1)
    target_link_libraries(Vita3KiOS PRIVATE
        vita3k_ios_core
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework GameController"
        "-framework Metal"
        "-framework MetalKit"
        "-framework UIKit"
        "-framework UniformTypeIdentifiers"
    )
endif()

if(VITA3K_BUILD_IOS_UPSTREAM_CORE)
    set_target_properties(Vita3KiOS PROPERTIES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
endif()

if(VITA3K_IOS_GER_ONLY)
    set_target_properties(Vita3KiOS PROPERTIES
        OUTPUT_NAME "GodEaterResurrection"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.vita3k.ger.a11")
else()
    set_target_properties(Vita3KiOS PROPERTIES
        OUTPUT_NAME "Vita3K-iOS")
endif()

set_target_properties(Vita3KiOS PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${VITA3K_IOS_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VITA3K_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "NO"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)
