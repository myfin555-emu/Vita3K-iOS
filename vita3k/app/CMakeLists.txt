add_library(
	app
	STATIC
	include/app/functions.h
	include/app/session_controller.h
	include/app/discord.h
	include/app/state.h
	src/app_init.cpp
	src/app.cpp
	src/apps_list.cpp
	src/discord.cpp
	src/guest_dump.cpp
	src/session_controller.cpp
	src/user.cpp
)

target_include_directories(app PUBLIC include)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(app PUBLIC emuenv mem)

if(VITA3K_BUILD_IOS_UPSTREAM_CORE AND VITA3K_IOS_GER_ONLY)
    # app_init.cpp owns the upstream renderer selection. Keep the GE:R-only
    # Metal decision on the app target as well as the renderer/frontend.
    target_compile_definitions(app PRIVATE VITA3K_IOS_GER_ONLY=1)
endif()
if(USE_DISCORD_RICH_PRESENCE)
  target_link_libraries(app PUBLIC discord-rpc)
endif()
target_link_libraries(app PRIVATE audio camera config ctrl dialog display gdbstub gxm ime io mem modules motion net ngs np patch regmgr renderer touch SDL3::SDL3 pugixml::pugixml compat packages http)
if(WIN32)
	target_link_libraries(app PRIVATE dwmapi)
endif()

if (LINUX)
    find_package(X11 REQUIRED)
    target_link_libraries(app PRIVATE ${X11_LIBRARIES})
endif()
