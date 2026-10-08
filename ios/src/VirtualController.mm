// Vita3K emulator project
// Copyright (C) 2026 Vita3K team

#include <vita3k_ios/VirtualController.h>
#include <vita3k_ios/NativeFrontend.h>

#include <SDL3/SDL.h>
#include <ctrl/virtual_pad.h>

#import <UIKit/UIKit.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// The on-screen controller, its layout editor, the in-game menu, the controller
// options and the performance readout are all SwiftUI now; this file is the
// SDL side of the virtual joystick plus the plumbing that presents them.
#import "Tsubomi-Swift.h"

static SDL_JoystickID g_virtual_joystick_id = 0;
static SDL_Joystick *g_virtual_joystick = nullptr;
static std::atomic_bool g_physical_controller_connected = false;
static std::atomic_bool g_vita_touchscreen_enabled = true;
static std::atomic<float> g_safe_area_top_pixels = 0.0f;
// The SwiftUI on-screen controller (TsubomiControlsHost). Its configuration -
// positions, sizes, visibility, opacity - lives in ControlsModel on the Swift
// side, which owns ios_controls.json; nothing about the layout is tracked here
// any more.
static UIViewController *g_overlay_controller = nil;
// Set when the overlay was spun up purely to reposition controls from the
// library, with no game running. Torn down when editing finishes.
static BOOL g_overlay_preview_only = NO;
// Whichever in-game sheet is currently presented (menu, controller options,
// performance toggles), so a second request replaces rather than stacks.
static UIViewController *g_presented_sheet = nil;
// Set when a sub-screen (controller options, trophies, performance HUD) was
// opened from the in-game menu, so closing it returns to the menu instead of
// dropping straight back to the game.
static BOOL g_return_to_game_menu = NO;

static UITapGestureRecognizer *g_three_finger_tap = nil;

namespace {
std::atomic_bool g_input_worker_running{false};
std::thread g_input_worker;
std::mutex g_input_mutex;
std::condition_variable g_input_cv;
std::array<std::atomic<short>, SDL_GAMEPAD_AXIS_COUNT> g_pending_axes{};
std::atomic<uint64_t> g_pending_axis_mask{0};
std::atomic<uint64_t> g_pending_button_press{0};
std::atomic<uint64_t> g_pending_button_release{0};

void input_worker_loop() {
    while (g_input_worker_running.load(std::memory_order_acquire)) {
        std::unique_lock lock(g_input_mutex);
        g_input_cv.wait_for(lock, std::chrono::milliseconds(16));
        lock.unlock();
        if (!g_input_worker_running.load(std::memory_order_acquire))
            break;
        if (!g_virtual_joystick)
            continue;

        const uint64_t press = g_pending_button_press.exchange(0, std::memory_order_acq_rel);
        const uint64_t release = g_pending_button_release.exchange(0, std::memory_order_acq_rel);
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && button < 64; ++button) {
            const uint64_t bit = 1ull << button;
            if (press & bit) {
                ctrl::virtual_pad.button(button, true);
                SDL_SetJoystickVirtualButton(g_virtual_joystick, button, true);
            }
            if (release & bit) {
                ctrl::virtual_pad.button(button, false);
                SDL_SetJoystickVirtualButton(g_virtual_joystick, button, false);
            }
        }

        const uint64_t axes = g_pending_axis_mask.exchange(0, std::memory_order_acq_rel);
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT && axis < 64; ++axis) {
            if ((axes & (1ull << axis)) == 0)
                continue;
            const short value = g_pending_axes[axis].load(std::memory_order_acquire);
            ctrl::virtual_pad.axis(axis, value);
            SDL_SetJoystickVirtualAxis(g_virtual_joystick, axis, value);
        }
    }
}

void start_input_worker() {
    bool expected = false;
    if (!g_input_worker_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    g_input_worker = std::thread(input_worker_loop);
}

void stop_input_worker() {
    if (!g_input_worker_running.exchange(false, std::memory_order_acq_rel))
        return;
    g_input_cv.notify_all();
    if (g_input_worker.joinable())
        g_input_worker.join();
    g_pending_axis_mask.store(0, std::memory_order_release);
    g_pending_button_press.store(0, std::memory_order_release);
    g_pending_button_release.store(0, std::memory_order_release);
}
}


static void performOnMainThread(dispatch_block_t block);
static void presentGameMenu();
static void dismissPresentedSheet(void (^completion)(void));

static UIWindow *activeWindow() {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]
            || scene.activationState == UISceneActivationStateUnattached)
            continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) {
            if (window.isKeyWindow)
                return window;
        }
    }
    return nil;
}

// The emulator thread calls into here; UIKit and SwiftUI both require the main
// thread. Runs inline when already there so a call from the UI does not get
// deferred a runloop turn.
static void performOnMainThread(dispatch_block_t block) {
    if (NSThread.isMainThread)
        block();
    else
        dispatch_async(dispatch_get_main_queue(), block);
}

// Root view controller to present sheets from. The overlay itself is parented
// to the window, so it is not usable as a presenter.
static UIViewController *sheetPresenter() {
    UIViewController *root = activeWindow().rootViewController;
    while (root.presentedViewController)
        root = root.presentedViewController;
    return root;
}

static void dismissPresentedSheet(void (^completion)(void)) {
    if (!g_presented_sheet) {
        if (completion)
            completion();
        return;
    }
    UIViewController *sheet = g_presented_sheet;
    g_presented_sheet = nil;
    [sheet dismissViewControllerAnimated:YES completion:completion];
}

static void presentSheet(UIViewController *sheet) {
    UIViewController *presenter = sheetPresenter();
    if (!presenter)
        return;
    g_presented_sheet = sheet;
    [presenter presentViewController:sheet animated:YES completion:nil];
}

// Restores the menu button after "Hide Menu Button". Kept as a window gesture
// rather than moving into SwiftUI: there is no multi-finger tap gesture there,
// and this has to fire over the regions where the overlay passes touches
// through to the game.
@interface Vita3KThreeFingerTarget : NSObject
@end
@implementation Vita3KThreeFingerTarget
- (void)tapped {
    if (!TsubomiControlsHost.isMenuButtonHidden)
        return;
    [TsubomiControlsHost setMenuButtonVisible:YES];
}
@end
static Vita3KThreeFingerTarget *g_three_finger_target = nil;

static void presentGameMenu() {
    dismissPresentedSheet(^{
        presentSheet([TsubomiGameOverlayHosts
            gameMenuViewControllerWithResume:^{
                dismissPresentedSheet(nil);
            }
            editLayout:^{
                // Opens the options screen, NOT the editor directly. The
                // editor is reached from a button inside options, matching the
                // home-screen settings flow. Options re-opens the game menu on
                // dismissal, so this returns to it.
                g_return_to_game_menu = YES;
                dismissPresentedSheet(^{ vita3k_ios_present_controller_options(); });
            }
            trophies:^{
                g_return_to_game_menu = YES;
                dismissPresentedSheet(^{ vita3k_ios_request_current_trophies(); });
            }
            performanceHUD:^{
                g_return_to_game_menu = YES;
                dismissPresentedSheet(^{
                    presentSheet([TsubomiGameOverlayHosts
                        performanceHUDPanelViewControllerWithFinish:^{
                            dismissPresentedSheet(^{ vita3k_ios_submenu_dismissed(); });
                        }]);
                });
            }
            hideMenuButton:^{
                [TsubomiControlsHost setMenuButtonVisible:NO];
                dismissPresentedSheet(nil);
            }
            quit:^{
                // Post the quit synchronously and dismiss without animation.
                // Waiting on the sheet's dismissal completion to post the
                // event risked a hang if that animation stalled while teardown
                // was already tearing the window down - the occasional
                // hard-freeze on quit.
                if (g_presented_sheet) {
                    UIViewController *sheet = g_presented_sheet;
                    g_presented_sheet = nil;
                    [sheet dismissViewControllerAnimated:NO completion:nil];
                }
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
            }]);
    });
}

bool vita3k_ios_attach_virtual_controller() {
    vita3k_ios_detach_virtual_controller();
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        SDL_Log("Vita3K iOS: cannot attach touch controller before SDL gamepad init");
        return false;
    }
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Vita3K iOS Touch Controller";
    g_virtual_joystick_id = SDL_AttachVirtualJoystick(&desc);
    if (g_virtual_joystick_id == 0) {
        SDL_Log("Vita3K iOS: virtual controller attach failed: %s", SDL_GetError());
        return false;
    }
    g_virtual_joystick = SDL_OpenJoystick(g_virtual_joystick_id);
    if (!g_virtual_joystick) {
        SDL_Log("Vita3K iOS: virtual controller open failed: %s", SDL_GetError());
        SDL_DetachVirtualJoystick(g_virtual_joystick_id);
        g_virtual_joystick_id = 0;
        return false;
    }
    ctrl::virtual_pad.attach(g_virtual_joystick_id);
    start_input_worker();
    // Trigger axes rest at minimum on a real pad; virtual axes default to 0
    // (half pressed), so park them explicitly.
    SDL_SetJoystickVirtualAxis(g_virtual_joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(g_virtual_joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_Log("Vita3K iOS: virtual touch controller attached (joystick=%u, dual analog enabled)",
        static_cast<unsigned>(g_virtual_joystick_id));
    return true;
}

void vita3k_ios_show_virtual_controller() {
    performOnMainThread(^{
        if (g_overlay_controller)
            return;
        UIWindow *window = activeWindow();
        if (!window) {
            SDL_Log("Vita3K iOS: no active window for touch controller overlay");
            return;
        }
        g_overlay_preview_only = NO;
        g_overlay_controller = [TsubomiControlsHost controlsViewControllerWithMenuHandler:^{
            presentGameMenu();
        }];
        UIView *overlay = g_overlay_controller.view;
        overlay.frame = window.bounds;
        overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [window addSubview:overlay];
        [window bringSubviewToFront:overlay];
        // The performance readout is drawn inside this same overlay now (so it
        // can be dragged in the layout editor); there is no separate controller.

        if (!g_three_finger_target)
            g_three_finger_target = [[Vita3KThreeFingerTarget alloc] init];
        UITapGestureRecognizer *restore = [[UITapGestureRecognizer alloc]
            initWithTarget:g_three_finger_target action:@selector(tapped)];
        restore.numberOfTouchesRequired = 3;
        restore.cancelsTouchesInView = NO;
        [window addGestureRecognizer:restore];
        g_three_finger_tap = restore;

        [TsubomiControlsHost setPhysicalControllerConnected:
            g_physical_controller_connected.load(std::memory_order_relaxed)];
        SDL_Log("Vita3K iOS: virtual touch controller visible (SwiftUI)");
    });
}

void vita3k_ios_hide_virtual_controller() {
    performOnMainThread(^{
        [g_three_finger_tap.view removeGestureRecognizer:g_three_finger_tap];
        g_three_finger_tap = nil;
        [TsubomiControlsHost releaseAllInputs];
        [TsubomiControlsHost setLayoutEditing:NO];
        // The overlay is going away, so nothing is claiming touches any more.
        // Leaving the guest's touch panel switched off would carry a per-game
        // controller preference into the next session.
        vita3k_ios_set_vita_touchscreen_enabled(true);
        dismissPresentedSheet(nil);
        [g_overlay_controller.view removeFromSuperview];
        g_overlay_controller = nil;
        g_overlay_preview_only = NO;
    });
}

// Enters the drag-to-reposition editor, spinning up a preview overlay first if
// no game is running (the library's Controller Options can reach this).
void vita3k_ios_begin_layout_editing() {
    performOnMainThread(^{
        if (!g_overlay_controller) {
            UIWindow *window = activeWindow();
            if (!window)
                return;
            g_overlay_preview_only = YES;
            g_overlay_controller = [TsubomiControlsHost controlsViewControllerWithMenuHandler:^{}];
            UIView *overlay = g_overlay_controller.view;
            overlay.frame = window.bounds;
            overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [window addSubview:overlay];
            [window bringSubviewToFront:overlay];
        }
        [TsubomiControlsHost setLayoutEditing:YES];
    });
}

// Called by the editor's Done button.
void vita3k_ios_finish_layout_editing() {
    performOnMainThread(^{
        [TsubomiControlsHost setLayoutEditing:NO];
        if (!g_overlay_preview_only) {
            // The editor is only ever reached from Layout Options, so Done
            // belongs back there. Dropping straight into the game discarded
            // the place the user came from and made adjusting a layout - which
            // usually takes a few passes - a trip through the menu each time.
            vita3k_ios_present_controller_options();
            return;
        }
        // A preview overlay exists only for editing; leaving it up would sit
        // an invisible touch surface on top of the library.
        [g_overlay_controller.view removeFromSuperview];
        g_overlay_controller = nil;
        g_overlay_preview_only = NO;
    });
}

void vita3k_ios_present_controller_options() {
    performOnMainThread(^{
        dismissPresentedSheet(^{
            presentSheet([TsubomiGameOverlayHosts
                controllerOptionsViewControllerWithEditLayout:^{
                    g_return_to_game_menu = NO;
                    dismissPresentedSheet(^{ vita3k_ios_begin_layout_editing(); });
                }
                finish:^{
                    dismissPresentedSheet(^{ vita3k_ios_submenu_dismissed(); });
                }]);
        });
    });
}

void vita3k_ios_present_game_menu() {
    performOnMainThread(^{
        if (g_overlay_controller && !g_overlay_preview_only)
            presentGameMenu();
    });
}

void vita3k_ios_submenu_dismissed() {
    performOnMainThread(^{
        if (!g_return_to_game_menu)
            return;
        g_return_to_game_menu = NO;
        if (g_overlay_controller && !g_overlay_preview_only)
            presentGameMenu();
    });
}

float vita3k_ios_safe_area_top_pixels() {
    return g_safe_area_top_pixels.load(std::memory_order_acquire);
}

void vita3k_ios_report_safe_area_top_pixels(const float pixels) {
    g_safe_area_top_pixels.store(pixels, std::memory_order_release);
}

void vita3k_ios_virtual_pad_set_button(const int button, const bool pressed) {
    if (!g_virtual_joystick || button < 0 || button >= 64)
        return;
    const uint64_t bit = 1ull << button;
    if (pressed)
        g_pending_button_press.fetch_or(bit, std::memory_order_release);
    else
        g_pending_button_release.fetch_or(bit, std::memory_order_release);
    g_input_cv.notify_one();
}

void vita3k_ios_virtual_pad_set_axis(const int axis, const short value) {
    if (!g_virtual_joystick || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT || axis >= 64)
        return;
    g_pending_axes[axis].store(value, std::memory_order_release);
    g_pending_axis_mask.fetch_or(1ull << axis, std::memory_order_release);
    g_input_cv.notify_one();
}

void vita3k_ios_virtual_pad_release_all() {
    if (!g_virtual_joystick)
        return;
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && button < 64; ++button)
        g_pending_button_release.fetch_or(1ull << button, std::memory_order_release);
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT && axis < 64; ++axis)
        g_pending_axes[axis].store(0, std::memory_order_release);
    g_pending_axis_mask.fetch_or(SDL_GAMEPAD_AXIS_COUNT >= 64 ? ~0ull : ((1ull << SDL_GAMEPAD_AXIS_COUNT) - 1), std::memory_order_release);
    g_input_cv.notify_one();
    // Every Vita-relevant button and axis, so a control held when a session
    // pauses cannot stay stuck on.
    for (int button = 0; button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++button)
        SDL_SetJoystickVirtualButton(g_virtual_joystick, button, false);
    for (int axis = 0; axis <= SDL_GAMEPAD_AXIS_RIGHTY; ++axis)
        SDL_SetJoystickVirtualAxis(g_virtual_joystick, axis, 0);
    SDL_SetJoystickVirtualAxis(g_virtual_joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(g_virtual_joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
}

void vita3k_ios_set_vita_touchscreen_enabled(const bool enabled) {
    g_vita_touchscreen_enabled.store(enabled, std::memory_order_relaxed);
}

bool vita3k_ios_vita_touchscreen_enabled() {
    return g_vita_touchscreen_enabled.load(std::memory_order_relaxed);
}

void vita3k_ios_set_physical_controller_connected(bool connected) {
    g_physical_controller_connected.store(connected, std::memory_order_relaxed);
    performOnMainThread(^{ [TsubomiControlsHost setPhysicalControllerConnected:connected]; });
}

void vita3k_ios_detach_virtual_controller() {
    stop_input_worker();
    ctrl::virtual_pad.attach(0);
    if (g_virtual_joystick)
        SDL_CloseJoystick(g_virtual_joystick);
    if (g_virtual_joystick_id != 0)
        SDL_DetachVirtualJoystick(g_virtual_joystick_id);
    g_virtual_joystick = nullptr;
    g_virtual_joystick_id = 0;
}
