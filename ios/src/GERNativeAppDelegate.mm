#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

#import <vita3k_ios/GERNativeRenderer.h>

#include <vita3k_ios/GERNativeRuntime.h>

@interface GERNativeViewController : UIViewController
@property(nonatomic, strong) MTKView *metalView;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) GERNativeRenderer *renderer;
@end

@implementation GERNativeViewController {
    ger::ios::NativeRuntime _runtime;
}

- (void)loadView {
    self.metalView = [[MTKView alloc] initWithFrame:CGRectZero
                                             device:MTLCreateSystemDefaultDevice()];
    self.metalView.translatesAutoresizingMaskIntoConstraints = NO;
    self.metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.view = self.metalView;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    self.view.backgroundColor = UIColor.blackColor;

    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.textColor = UIColor.whiteColor;
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:14.0 weight:UIFontWeightRegular];
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.textAlignment = NSTextAlignmentCenter;

    [self.view addSubview:self.statusLabel];
    [NSLayoutConstraint activateConstraints:@[
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:16.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-16.0],
        [self.statusLabel.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor]
    ]];

    const auto status = _runtime.scan();
    self.statusLabel.text = [NSString stringWithUTF8String:status.message.c_str()];

    self.renderer = [[GERNativeRenderer alloc] initWithView:self.metalView];
    [self.renderer start];

    self.navigationItem.title = @"GER-iOS Native Runtime";
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
