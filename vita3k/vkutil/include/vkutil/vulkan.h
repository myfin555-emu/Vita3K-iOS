// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// Keep Vulkan-Hpp configuration consistent regardless of which renderer header
// is included first. Include this wrapper before Vulkan extension headers.
#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__APPLE__)
#define VK_ENABLE_BETA_EXTENSIONS
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#else
#if defined(HAVE_X11)
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(HAVE_WAYLAND)
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_SPACESHIP_OPERATOR
#include <vulkan/vulkan.hpp>

#undef VK_USE_PLATFORM_ANDROID_KHR
#undef VK_USE_PLATFORM_METAL_EXT
#undef VK_USE_PLATFORM_WIN32_KHR
#undef VK_USE_PLATFORM_XLIB_KHR
#undef VK_USE_PLATFORM_WAYLAND_KHR
#undef VK_ENABLE_BETA_EXTENSIONS

#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#ifdef Success
#undef Success
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef KeyPress
#undef KeyPress
#endif
#ifdef KeyRelease
#undef KeyRelease
#endif
#ifdef FocusIn
#undef FocusIn
#endif
#ifdef FocusOut
#undef FocusOut
#endif
#ifdef Expose
#undef Expose
#endif
#ifdef GrayScale
#undef GrayScale
#endif
#ifdef CursorShape
#undef CursorShape
#endif
