#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import <vita3k_ios/GERNativeInstaller.h>
#import <vita3k_ios/GERNativeRenderer.h>
#import <vita3k_ios/GERNativeRuntime.h>

#include <filesystem>
#include <string>

@interface GERNativeViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) MTKView *metalView;
@property(nonatomic, strong) UILabel *titleLabel;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *detailsLabel;
@property(nonatomic, strong) UIButton *installFolderButton;
@property(nonatomic, strong) UIButton *installArchiveButton;
@property(nonatomic, strong) UIButton *settingsButton;
@property(nonatomic, strong) UIProgressView *installProgress;
@property(nonatomic, strong) UIActivityIndicatorView *installSpinner;
@property(nonatomic, strong) GERNativeRenderer *renderer;
@property(nonatomic, assign) NSInteger pickerKind;
@end

@implementation GERNativeViewController {
    ger::ios::NativeRuntime _runtime;
    BOOL _installing;
}

- (void)loadView {
    self.view = [[UIView alloc] initWithFrame:CGRectZero];
    self.view.backgroundColor = UIColor.blackColor;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    std::string storageError;
    ger::ios::NativeInstaller::ensure_storage(storageError);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        UILabel *label = [[UILabel alloc] init];
        label.translatesAutoresizingMaskIntoConstraints = NO;
        label.text = @"GOD EATER RESURRECTION\n\nMetal is not available on this device.";
        label.textColor = UIColor.whiteColor;
        label.textAlignment = NSTextAlignmentCenter;
        label.numberOfLines = 0;
        [self.view addSubview:label];
        [NSLayoutConstraint activateConstraints:@[
            [label.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:24.0],
            [label.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-24.0],
            [label.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor]
        ]];
        return;
    }

    self.metalView = [[MTKView alloc] initWithFrame:CGRectZero device:device];
    self.metalView.translatesAutoresizingMaskIntoConstraints = NO;
    self.metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalView.framebufferOnly = YES;
    self.metalView.enableSetNeedsDisplay = NO;
    self.metalView.paused = YES;
    [self.view addSubview:self.metalView];

    UIView *panel = [[UIView alloc] init];
    panel.translatesAutoresizingMaskIntoConstraints = NO;
    panel.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.96];
    panel.layer.cornerRadius = 18.0;
    [self.view addSubview:panel];

    self.titleLabel = [[UILabel alloc] init];
    self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.titleLabel.text = @"GOD EATER RESURRECTION";
    self.titleLabel.textColor = UIColor.whiteColor;
    self.titleLabel.font = [UIFont systemFontOfSize:22.0 weight:UIFontWeightBold];
    self.titleLabel.textAlignment = NSTextAlignmentCenter;
    self.titleLabel.numberOfLines = 0;

    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.textColor = UIColor.whiteColor;
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:15.0 weight:UIFontWeightRegular];
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.textAlignment = NSTextAlignmentCenter;

    self.detailsLabel = [[UILabel alloc] init];
    self.detailsLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.detailsLabel.textColor = [UIColor colorWithWhite:0.75 alpha:1.0];
    self.detailsLabel.font = [UIFont monospacedSystemFontOfSize:12.0 weight:UIFontWeightRegular];
    self.detailsLabel.numberOfLines = 0;
    self.detailsLabel.textAlignment = NSTextAlignmentLeft;

    self.installFolderButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.installFolderButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.installFolderButton setTitle:@"Install GE:R Folder" forState:UIControlStateNormal];
    self.installFolderButton.titleLabel.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];
    [self.installFolderButton addTarget:self action:@selector(selectGameFolder) forControlEvents:UIControlEventTouchUpInside];

    self.installArchiveButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.installArchiveButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.installArchiveButton setTitle:@"Install GE:R ZIP / VPK" forState:UIControlStateNormal];
    self.installArchiveButton.titleLabel.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];
    [self.installArchiveButton addTarget:self action:@selector(selectGameArchive) forControlEvents:UIControlEventTouchUpInside];

    self.installProgress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    self.installProgress.translatesAutoresizingMaskIntoConstraints = NO;
    self.installProgress.progress = 0.0;
    self.installProgress.hidden = YES;

    self.installSpinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    self.installSpinner.translatesAutoresizingMaskIntoConstraints = NO;
    self.installSpinner.hidesWhenStopped = YES;

    self.settingsButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.settingsButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.settingsButton setTitle:@"Settings" forState:UIControlStateNormal];
    self.settingsButton.titleLabel.font = [UIFont systemFontOfSize:16.0 weight:UIFontWeightSemibold];
    [self.settingsButton addTarget:self action:@selector(showSettings) forControlEvents:UIControlEventTouchUpInside];

    [panel addSubview:self.titleLabel];
    [panel addSubview:self.statusLabel];
    [panel addSubview:self.detailsLabel];
    [panel addSubview:self.installFolderButton];
    [panel addSubview:self.installArchiveButton];
    [panel addSubview:self.installProgress];
    [panel addSubview:self.installSpinner];
    [panel addSubview:self.settingsButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.metalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.metalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.metalView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.metalView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        [panel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:20.0],
        [panel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-20.0],
        [panel.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor],
        [panel.topAnchor constraintGreaterThanOrEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:20.0],
        [panel.bottomAnchor constraintLessThanOrEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-20.0],

        [self.titleLabel.topAnchor constraintEqualToAnchor:panel.topAnchor constant:24.0],
        [self.titleLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:18.0],
        [self.titleLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-18.0],

        [self.statusLabel.topAnchor constraintEqualToAnchor:self.titleLabel.bottomAnchor constant:16.0],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:18.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-18.0],

        [self.detailsLabel.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:14.0],
        [self.detailsLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:22.0],
        [self.detailsLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-22.0],

        [self.installFolderButton.topAnchor constraintEqualToAnchor:self.detailsLabel.bottomAnchor constant:18.0],
        [self.installFolderButton.centerXAnchor constraintEqualToAnchor:panel.centerXAnchor],

        [self.installArchiveButton.topAnchor constraintEqualToAnchor:self.installFolderButton.bottomAnchor constant:8.0],
        [self.installArchiveButton.centerXAnchor constraintEqualToAnchor:panel.centerXAnchor],

        [self.installProgress.topAnchor constraintEqualToAnchor:self.installArchiveButton.bottomAnchor constant:18.0],
        [self.installProgress.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:28.0],
        [self.installProgress.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-28.0],

        [self.installSpinner.centerYAnchor constraintEqualToAnchor:self.installProgress.centerYAnchor],
        [self.installSpinner.trailingAnchor constraintEqualToAnchor:self.installProgress.leadingAnchor constant:-10.0],

        [self.settingsButton.topAnchor constraintEqualToAnchor:self.installProgress.bottomAnchor constant:18.0],
        [self.settingsButton.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-20.0],
        [self.settingsButton.centerXAnchor constraintEqualToAnchor:panel.centerXAnchor]
    ]];

    [self refreshGameStatus];
    self.navigationItem.title = @"GER Native 1.1";
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];

    if (!self.metalView || self.renderer)
        return;

    self.renderer = [[GERNativeRenderer alloc] initWithView:self.metalView];
    [self.renderer start];
}

- (void)refreshGameStatus {
    const auto status = _runtime.scan();

    self.statusLabel.text = [NSString stringWithUTF8String:status.message.c_str()];

    if (!status.game.root.empty()) {
        self.detailsLabel.text = [NSString stringWithFormat:
            @"Title ID: %s\nInstall: %s\nparam.sfo: %@    executable: %@\n\nNative runtime: Metal / ARM64\nNo Vita CPU or GXM emulator is linked.",
            status.game.title_id.c_str(),
            status.game.root.string().c_str(),
            status.game.has_param_sfo ? @"OK" : @"MISSING",
            status.game.has_eboot ? @"FOUND" : @"MISSING"];
    } else {
        self.detailsLabel.text =
            @"Title ID: PCSE00801\nInstall a GE:R folder, ZIP or VPK from Files.\n\nThe app creates Documents/PCSE00801 and verifies the game before installation.";
    }
}

- (void)setInstalling:(BOOL)value message:(NSString *)message {
    _installing = value;
    self.installFolderButton.enabled = !value;
    self.installArchiveButton.enabled = !value;
    self.settingsButton.enabled = !value;
    self.installProgress.hidden = !value;
    if (value) {
        [self.installSpinner startAnimating];
        self.statusLabel.text = message;
    } else {
        [self.installSpinner stopAnimating];
    }
}

- (void)selectGameFolder {
    self.pickerKind = 1;
    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.folder"]
                                                               inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    picker.shouldShowFileExtensions = YES;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)selectGameArchive {
    self.pickerKind = 2;
    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.zip-archive", @"public.data"]
                                                               inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    picker.shouldShowFileExtensions = YES;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
 didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    NSURL *url = urls.firstObject;
    if (!url)
        return;

    const NSInteger kind = self.pickerKind;
    self.pickerKind = 0;
    [self installURL:url kind:kind];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    (void)controller;
    self.pickerKind = 0;
}

- (void)installURL:(NSURL *)url kind:(NSInteger)kind {
    if (_installing)
        return;

    BOOL scoped = [url startAccessingSecurityScopedResource];
    const auto documentsPath = ger::ios::NativeInstaller::documents_root().string();
    NSString *documentsPrefix = [NSString stringWithUTF8String:documentsPath.c_str()];
    if (!scoped && ![url.path hasPrefix:documentsPrefix]) {
        [self showImportError:@"iOS did not grant GER Native access to the selected file/folder."];
        return;
    }

    [self setInstalling:YES message:@"Preparing GE:R installation…"];
    self.installProgress.progress = 0.02;

    const std::filesystem::path source(url.path.UTF8String ?: "");
    const auto documents = ger::ios::NativeInstaller::documents_root();
    const auto destination = ger::ios::NativeInstaller::game_root();
    const BOOL replacing = std::filesystem::is_directory(destination);

    if (replacing) {
        [self setInstalling:NO message:@"GE:R already installed."];
        if (scoped)
            [url stopAccessingSecurityScopedResource];

        UIAlertController *confirm = [UIAlertController
            alertControllerWithTitle:@"GE:R is already installed"
                             message:@"Replace the existing PCSE00801 installation?"
                      preferredStyle:UIAlertControllerStyleAlert];
        [confirm addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        [confirm addAction:[UIAlertAction actionWithTitle:@"Replace"
                                                    style:UIAlertActionStyleDestructive
                                                  handler:^(__unused UIAlertAction *action) {
            [self installURL:url kind:kind];
        }]];
        [self presentViewController:confirm animated:YES completion:nil];
        return;
    }

    (void)documents;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const auto progress = [weakSelf = self](double fraction, const std::string &message) {
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.installProgress.progress = static_cast<float>(fraction);
                weakSelf.statusLabel.text = [NSString stringWithUTF8String:message.c_str()];
            });
        };

        ger::ios::InstallResult result;
        if (kind == 2)
            result = ger::ios::NativeInstaller::install_archive(source, progress);
        else
            result = ger::ios::NativeInstaller::install_folder(source, source, progress);

        dispatch_async(dispatch_get_main_queue(), ^{
            if (scoped)
                [url stopAccessingSecurityScopedResource];

            [self setInstalling:NO message:@""];
            [self refreshGameStatus];

            if (result.success) {
                self.installProgress.progress = 1.0;
                UIAlertController *done = [UIAlertController
                    alertControllerWithTitle:@"GE:R installed"
                                     message:@"PCSE00801 was copied into GER Native/Documents/PCSE00801 and passed the native install checks."
                              preferredStyle:UIAlertControllerStyleAlert];
                [done addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:done animated:YES completion:nil];
            } else {
                [self showImportError:[NSString stringWithUTF8String:result.message.c_str()]];
            }
        });
    });
}

- (void)showImportError:(NSString *)message {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"GE:R installation failed"
                         message:message
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)showSettings {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"GER Native Settings"
                         message:@"Version 1.1.0\n\nRenderer: Native Metal\nRuntime: Native GE:R foundation\nEmulator core: disabled\n\nGame installation:\n• Folder import\n• ZIP/VPK extraction\n• PCSE00801 validation\n• Transactional replacement\n• Free-space check\n\nGame execution is not implemented yet. This build installs and inspects the user's external GE:R data; it does not embed the game in the IPA."
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end

@interface GERNativeAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) GERNativeViewController *rootController;
@end

@implementation GERNativeAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.rootController = [[GERNativeViewController alloc] init];
    self.window.rootViewController = self.rootController;
    [self.window makeKeyAndVisible];

    NSURL *launchURL = launchOptions[UIApplicationLaunchOptionsURLKey];
    if (launchURL) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.rootController installURL:launchURL kind:2];
        });
    }
    return YES;
}

- (BOOL)application:(UIApplication *)application
            openURL:(NSURL *)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {
    (void)application;
    (void)options;

    NSString *extension = url.pathExtension.lowercaseString;
    if (![extension isEqualToString:@"zip"] && ![extension isEqualToString:@"vpk"])
        return NO;

    dispatch_async(dispatch_get_main_queue(), ^{
        [self.rootController installURL:url kind:2];
    });
    return YES;
}

@end
