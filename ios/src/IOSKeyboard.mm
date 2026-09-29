// Native iOS counterpart of Vita3K-Plus's Android IME bridge.
#include <dialog/state.h>
#include <emuenv/state.h>
#include <ime/state.h>
#include <ime/text.h>
#include <util/string_utils.h>
#include <vita3k_ios/IOSKeyboard.h>
#include <vita3k_ios/VirtualController.h>

#define Ptr MacTypesPtr
#import <UIKit/UIKit.h>
#undef Ptr

#include <cstdio>
#include <cstring>

namespace {
bool dialog_active(const EmuEnvState &env) {
    return env.common_dialog.type == IME_DIALOG
        && env.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING;
}
std::u16string utf16(NSString *value) {
    std::u16string result(value.length, u'\0');
    [value getCharacters:reinterpret_cast<unichar *>(result.data()) range:NSMakeRange(0, value.length)];
    return result;
}
NSString *native_text(const std::u16string &value) {
    return [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(value.data()) length:value.size()];
}
} // namespace

@interface Vita3KKeyboard : NSObject <UITextViewDelegate>
@property (nonatomic) EmuEnvState *environment;
@property (nonatomic) uint64_t generation;
@property (nonatomic) BOOL dialog;
@property (nonatomic) BOOL multiline;
@property (nonatomic) BOOL cancelable;
@property (nonatomic) BOOL updating;
@property (nonatomic) BOOL finished;
@property (nonatomic, strong) UIView *panel;
@property (nonatomic, strong) UITextView *editor;
@property (nonatomic, strong) UILabel *heading;
@property (nonatomic, strong) UIButton *cancelButton;
- (void)publishText;
- (void)submit;
- (void)cancel;
@end

@implementation Vita3KKeyboard
- (BOOL)matchesSession {
    return self.environment && self.environment->ime.generation == self.generation
        && (self.dialog ? dialog_active(*self.environment) : self.environment->ime.state);
}
- (void)publishText {
    if (self.updating || self.finished || self.editor.markedTextRange || !self.environment)
        return;
    auto &env = *self.environment;
    std::lock_guard dialogLock(env.common_dialog.mutex);
    std::lock_guard imeLock(env.ime.mutex);
    if (![self matchesSession])
        return;
    auto text = utf16(self.editor.text);
    text.resize(ime::text_length(text, env.ime.param.maxTextLength));
    const auto caret = static_cast<uint32_t>(ime::text_length(text, self.editor.selectedRange.location));
    if (text != env.ime.str || caret != env.ime.edit_text.caretIndex) {
        env.ime.event_id = text != env.ime.str || env.ime.event_id == SCE_IME_EVENT_UPDATE_TEXT
            ? SCE_IME_EVENT_UPDATE_TEXT
            : SCE_IME_EVENT_UPDATE_CARET;
        env.ime.str = text;
        env.ime.edit_text.editIndex = 0;
        env.ime.edit_text.caretIndex = env.ime.caretIndex = caret;
        env.ime.edit_text.preeditIndex = caret;
        env.ime.edit_text.preeditLength = 0;
        env.ime.edit_text.editLengthChange = 0;
    }
    if (self.editor.text.length != text.size()) {
        self.updating = YES;
        self.editor.text = native_text(text);
        self.editor.selectedRange = NSMakeRange(caret, 0);
        self.updating = NO;
    }
}
- (void)textViewDidChange:(UITextView *)textView {
    [self publishText];
}
- (void)textViewDidChangeSelection:(UITextView *)textView {
    [self publishText];
}
- (BOOL)textView:(UITextView *)textView shouldChangeTextInRange:(NSRange)range replacementText:(NSString *)text {
    if (ime::is_submit_key(utf16(text), textView.markedTextRange != nil)) {
        [self submit];
        return NO;
    }
    return YES;
}
- (void)submit {
    if (self.finished || !self.environment)
        return;
    [self.editor unmarkText];
    [self publishText];
    {
        auto &env = *self.environment;
        std::lock_guard dialogLock(env.common_dialog.mutex);
        std::lock_guard imeLock(env.ime.mutex);
        if (![self matchesSession])
            return;
        if (self.dialog) {
            auto &dialog = env.common_dialog;
            const auto length = ime::text_length(env.ime.str, dialog.ime.max_length);
            if (dialog.ime.result) {
                std::memcpy(dialog.ime.result, env.ime.str.data(), length * sizeof(char16_t));
                dialog.ime.result[length] = 0;
            }
            const auto text = string_utils::utf16_to_utf8(env.ime.str.substr(0, length));
            std::snprintf(dialog.ime.text, sizeof(dialog.ime.text), "%s", text.c_str());
            dialog.ime.status = SCE_IME_DIALOG_BUTTON_ENTER;
            dialog.result = SCE_COMMON_DIALOG_RESULT_OK;
            dialog.status = SCE_COMMON_DIALOG_STATUS_FINISHED;
        } else {
            // The guest owns delivery, independently of UIKit frame polling.
            env.ime.native_input.submit(SCE_IME_EVENT_PRESS_ENTER);
        }
    }
    self.finished = YES;
    self.editor.delegate = nil;
    [self.editor resignFirstResponder];
    self.panel.hidden = YES;
    vita3k_ios_show_virtual_controller();
}
- (void)cancel {
    if (!self.cancelable || self.finished || !self.environment)
        return;
    {
        auto &env = *self.environment;
        std::lock_guard dialogLock(env.common_dialog.mutex);
        std::lock_guard imeLock(env.ime.mutex);
        if (![self matchesSession])
            return;
        if (self.dialog) {
            env.common_dialog.ime.status = SCE_IME_DIALOG_BUTTON_CLOSE;
            env.common_dialog.result = SCE_COMMON_DIALOG_RESULT_USER_CANCELED;
            env.common_dialog.status = SCE_COMMON_DIALOG_STATUS_FINISHED;
        } else {
            env.ime.native_input.submit(SCE_IME_EVENT_PRESS_CLOSE);
        }
    }
    self.finished = YES;
    self.editor.delegate = nil;
    [self.editor resignFirstResponder];
    self.panel.hidden = YES;
    vita3k_ios_show_virtual_controller();
}
@end

static Vita3KKeyboard *keyboard;

void vita3k_ios_close_keyboard() {
    if (!keyboard)
        return;
    keyboard.environment = nullptr;
    keyboard.editor.delegate = nil;
    // Dismiss immediately; let UIKit deliver hide notifications on the normal run loop.
    [keyboard.editor resignFirstResponder];
    [keyboard.panel removeFromSuperview];
    keyboard = nil;
    vita3k_ios_show_virtual_controller();
}

void vita3k_ios_update_keyboard(EmuEnvState &env) {
    bool active, isDialog, multiline, cancelable, dismissed;
    uint64_t generation;
    std::u16string text;
    uint32_t caret, type, enterLabel;
    NSString *title;
    {
        std::lock_guard dialogLock(env.common_dialog.mutex);
        std::lock_guard imeLock(env.ime.mutex);
        isDialog = dialog_active(env);
        active = isDialog || env.ime.state;
        generation = env.ime.generation;
        text = env.ime.str;
        caret = env.ime.edit_text.caretIndex;
        multiline = isDialog ? env.common_dialog.ime.multiline : (env.ime.param.option & SCE_IME_OPTION_MULTILINE) != 0;
        cancelable = !isDialog || env.common_dialog.ime.cancelable;
        type = env.ime.param.type;
        enterLabel = env.ime.param.enterLabel;
        title = isDialog ? [NSString stringWithUTF8String:env.common_dialog.ime.title] : @"Game text input";
        dismissed = !isDialog && env.ime.native_input.dismissed();
    }
    if (!active) {
        vita3k_ios_close_keyboard();
        return;
    }
    if (keyboard && (keyboard.generation != generation || keyboard.dialog != isDialog))
        vita3k_ios_close_keyboard();
    // Keep the submitted session hidden even while the callback runs or the
    // game keeps SceIme open. A later close/open increments generation.
    if (dismissed)
        return;
    if (keyboard) {
        if (keyboard.finished) {
            [keyboard.editor resignFirstResponder];
            keyboard.panel.hidden = YES;
        } else if (!keyboard.editor.markedTextRange
            && (![keyboard.editor.text isEqualToString:native_text(text)] || keyboard.editor.selectedRange.location != caret)) {
            keyboard.updating = YES;
            keyboard.editor.text = native_text(text);
            keyboard.editor.selectedRange = NSMakeRange(std::min<size_t>(caret, text.size()), 0);
            keyboard.updating = NO;
        }
        if (!keyboard.finished) {
            keyboard.panel.hidden = NO;
            [keyboard.panel.superview bringSubviewToFront:keyboard.panel];
            if (!keyboard.editor.isFirstResponder && UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
                [keyboard.editor becomeFirstResponder];
        }
        return;
    }
    UIWindow *window = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState != UISceneActivationStateForegroundActive || ![scene isKindOfClass:UIWindowScene.class])
            continue;
        for (UIWindow *candidate in ((UIWindowScene *)scene).windows)
            if (candidate.isKeyWindow) {
                window = candidate;
                break;
            }
    }
    if (!window)
        return;
    keyboard = [Vita3KKeyboard new];
    keyboard.environment = &env;
    keyboard.generation = generation;
    keyboard.dialog = isDialog;
    keyboard.multiline = multiline;
    keyboard.cancelable = cancelable;
    keyboard.panel = [UIView new];
    // Opaque UIKit text replaces the GPU-rendered IME card on iOS.
    keyboard.panel.backgroundColor = UIColor.whiteColor;
    keyboard.panel.tintColor = UIColor.systemBlueColor;
    keyboard.panel.layer.borderColor = UIColor.lightGrayColor.CGColor;
    keyboard.panel.layer.borderWidth = 1;
    keyboard.panel.layer.cornerRadius = 14;
    keyboard.panel.translatesAutoresizingMaskIntoConstraints = NO;
    keyboard.editor = [UITextView new];
    keyboard.editor.backgroundColor = UIColor.whiteColor;
    keyboard.editor.textColor = UIColor.blackColor;
    keyboard.editor.tintColor = UIColor.systemBlueColor;
    keyboard.editor.accessibilityIdentifier = @"vita3k.ime.text";
    keyboard.editor.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    keyboard.editor.adjustsFontForContentSizeCategory = YES;
    keyboard.editor.text = native_text(text);
    keyboard.editor.selectedRange = NSMakeRange(std::min<size_t>(caret, text.size()), 0);
    keyboard.editor.delegate = keyboard;
    keyboard.editor.accessibilityLabel = title;
    keyboard.editor.autocorrectionType = UITextAutocorrectionTypeNo;
    switch (type) {
    case SCE_IME_TYPE_BASIC_LATIN: keyboard.editor.keyboardType = UIKeyboardTypeASCIICapable; break;
    case SCE_IME_TYPE_NUMBER: keyboard.editor.keyboardType = UIKeyboardTypeNumberPad; break;
    case SCE_IME_TYPE_EXTENDED_NUMBER: keyboard.editor.keyboardType = UIKeyboardTypeNumbersAndPunctuation; break;
    case SCE_IME_TYPE_URL: keyboard.editor.keyboardType = UIKeyboardTypeURL; break;
    case SCE_IME_TYPE_MAIL: keyboard.editor.keyboardType = UIKeyboardTypeEmailAddress; break;
    default: keyboard.editor.keyboardType = UIKeyboardTypeDefault; break;
    }
    switch (enterLabel) {
    case SCE_IME_ENTER_LABEL_SEND: keyboard.editor.returnKeyType = UIReturnKeySend; break;
    case SCE_IME_ENTER_LABEL_SEARCH: keyboard.editor.returnKeyType = UIReturnKeySearch; break;
    case SCE_IME_ENTER_LABEL_GO: keyboard.editor.returnKeyType = UIReturnKeyGo; break;
    default: keyboard.editor.returnKeyType = UIReturnKeyDone; break;
    }
    keyboard.heading = [UILabel new];
    keyboard.heading.text = title;
    keyboard.heading.textColor = UIColor.blackColor;
    keyboard.heading.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
    keyboard.heading.adjustsFontForContentSizeCategory = YES;
    keyboard.cancelButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [keyboard.cancelButton setTitle:@"Cancel" forState:UIControlStateNormal];
    keyboard.cancelButton.hidden = !cancelable;
    [keyboard.cancelButton addTarget:keyboard action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    UIButton *done = [UIButton buttonWithType:UIButtonTypeSystem];
    done.accessibilityIdentifier = @"vita3k.ime.confirm";
    [done setTitle:@"Confirm" forState:UIControlStateNormal];
    [done addTarget:keyboard action:@selector(submit) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *bar = [[UIStackView alloc] initWithArrangedSubviews:@[ keyboard.cancelButton, keyboard.heading, done ]];
    bar.spacing = 12;
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[ bar, keyboard.editor ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 8;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [keyboard.panel addSubview:stack];
    [window addSubview:keyboard.panel];
    [window bringSubviewToFront:keyboard.panel];
    [NSLayoutConstraint activateConstraints:@[
        [keyboard.panel.leadingAnchor constraintEqualToAnchor:window.safeAreaLayoutGuide.leadingAnchor
                                                     constant:12],
        [keyboard.panel.trailingAnchor constraintEqualToAnchor:window.safeAreaLayoutGuide.trailingAnchor
                                                      constant:-12],
        [keyboard.panel.bottomAnchor constraintEqualToAnchor:window.keyboardLayoutGuide.topAnchor
                                                    constant:-8],
        [stack.leadingAnchor constraintEqualToAnchor:keyboard.panel.leadingAnchor
                                            constant:12],
        [stack.trailingAnchor constraintEqualToAnchor:keyboard.panel.trailingAnchor
                                             constant:-12],
        [stack.topAnchor constraintEqualToAnchor:keyboard.panel.topAnchor
                                        constant:8],
        [stack.bottomAnchor constraintEqualToAnchor:keyboard.panel.bottomAnchor
                                           constant:-8],
        [keyboard.editor.heightAnchor constraintEqualToConstant:multiline ? 100 : 48]
    ]];
    vita3k_ios_hide_virtual_controller();
    [keyboard.editor becomeFirstResponder];
}
