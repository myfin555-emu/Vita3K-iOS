#import <UIKit/UIKit.h>

@interface GERNativeAppDelegate : UIResponder <UIApplicationDelegate>
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(GERNativeAppDelegate.class));
    }
}
