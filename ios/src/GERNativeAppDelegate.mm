#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

#import <vita3k_ios/GERNativeRenderer.h>

#include <vita3k_ios/GERNativeRuntime.h>

@interface GERNativeViewController : UIViewController
@property(nonatomic, strong) MTKView *metalView;
@property(nonatomic, strong) UILabel *titleLabel;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *detailsLabel;
@property(nonatomic, strong) UIButton *settingsButton;
@property(nonatomic, strong) GERNativeRenderer *renderer;
@end

@implementation GERNativeViewController {
    ger::ios::NativeRuntime _runtime;
}

- (void)loadView {
    self.view = [[UIView alloc] initWithFrame:CGRectZero];
    self.view.backgroundColor = UIColor.blackColor;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        self.titleLabel = [[UILabel alloc] init];
        self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
        self.titleLabel.text = @"GOD EATER RESURRECTION";
        self.titleLabel.textColor = UIColor.whiteColor;
        self.titleLabel.textAlignment = NSTextAlignmentCenter;
        self.titleLabel.numberOfLines = 0;
        self.titleLabel.text = @"GOD EATER RESURRECTION\n\nMetal is not available on this device.";
        [self.view addSubview:self.titleLabel];
        [NSLayoutConstraint activateConstraints:@[
            [self.titleLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:24.0],
            [self.titleLabel.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-24.0],
            [self.titleLabel.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor]
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
    panel.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.94];
    panel.layer.cornerRadius = 18.0;
    [self.view addSubview:panel];

    self.titleLabel = [[UILabel alloc] init];
    self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.titleLabel.text = @"GOD EATER RESURRECTION";
    self.titleLabel.textColor = UIColor.whiteColor;
    self.titleLabel.font = [UIFont systemFontOfSize:22.0 weight:UIFontWeightBold];
    self.titleLabel.textAlignment = NSTextAlignmentCenter;

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

    self.settingsButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.settingsButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.settingsButton setTitle:@"Settings" forState:UIControlStateNormal];
    self.settingsButton.titleLabel.font = [UIFont systemFontOfSize:16.0 weight:UIFontWeightSemibold];
    [self.settingsButton addTarget:self action:@selector(showSettings) forControlEvents:UIControlEventTouchUpInside];

    [panel addSubview:self.titleLabel];
    [panel addSubview:self.statusLabel];
    [panel addSubview:self.detailsLabel];
    [panel addSubview:self.settingsButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.metalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.metalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.metalView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.metalView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [panel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:20.0],
        [panel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-20.0],
        [panel.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor],
        [self.titleLabel.topAnchor constraintEqualToAnchor:panel.topAnchor constant:24.0],
        [self.titleLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:18.0],
        [self.titleLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-18.0],
        [self.statusLabel.topAnchor constraintEqualToAnchor:self.titleLabel.bottomAnchor constant:18.0],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:18.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-18.0],
        [self.detailsLabel.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:16.0],
        [self.detailsLabel.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:22.0],
        [self.detailsLabel.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-22.0],
        [self.settingsButton.topAnchor constraintEqualToAnchor:self.detailsLabel.bottomAnchor constant:18.0],
        [self.settingsButton.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-20.0],
        [self.settingsButton.centerXAnchor constraintEqualToAnchor:panel.centerXAnchor]
    ]];

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
            @"Title ID: PCSE00801\nGame files are loaded from the user's install directory.\n\nNative runtime: Metal / ARM64";
    }

    self.navigationItem.title = @"GER Native 1.0";
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];

    if (!self.metalView || self.renderer)
        return;

    self.renderer = [[GERNativeRenderer alloc] initWithView:self.metalView];
    [self.renderer start];
}

- (void)showSettings {
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:@"GER Native Settings"
                         message:@"Version 1.0.0\n\nRenderer: Native Metal\nRuntime: Native GE:R foundation\nEmulator core: disabled\nGame data: external install only\n\nGame execution and recovered GE:R systems will be added to this runtime as they are implemented."
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end

@interface GERNativeAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation GERNativeAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[GERNativeViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

@end
