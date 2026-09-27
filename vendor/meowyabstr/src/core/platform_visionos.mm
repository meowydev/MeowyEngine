#include "core/platform.hpp"
#include "core/mr_state.hpp"
#include "meowyrender/meowyrender.hpp"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace meowyrender;
namespace {
std::function<void()> frameCallback,initializeCallback,cleanupCallback;
int requestedWidth=800,requestedHeight=600;
std::string requestedTitle,clipboard;
detail::InputState pending;
UIView* renderView=nil;
UIWindow* appWindow=nil;
CADisplayLink* displayLink=nil;
bool initialized=false,closed=false;
int KeyCode(UIKeyboardHIDUsage usage) {
    int value=static_cast<int>(usage);
    if(value>=4&&value<=29)return 'A'+value-4;
    if(value>=30&&value<=38)return '1'+value-30;
    if(value==39)return '0';
    switch(value){case 40:return 257;case 41:return 256;case 42:return 259;case 43:return 258;case 44:return 32;
        case 79:return 262;case 80:return 263;case 81:return 264;case 82:return 265;
        case 224:return 341;case 225:return 340;case 226:return 342;case 227:return 343;
        case 228:return 345;case 229:return 344;case 230:return 346;case 231:return 347;default:return -1;}
}
void Finish() {
    [displayLink invalidate];displayLink=nil;
    if(initialized){initialized=false;try{if(cleanupCallback)cleanupCallback();}catch(const std::exception& e){std::fprintf(stderr,"[visionOS] cleanup: %s\n",e.what());}}
    if(IsWindowReady())CloseWindow();closed=true;
}
}

@interface MRRenderView : UIView
@end
@implementation MRRenderView
+ (Class)layerClass {return [CAMetalLayer class];}
- (BOOL)canBecomeFirstResponder {return YES;}
- (void)layoutSubviews {
    [super layoutSubviews];
    const CGFloat scale=std::max<CGFloat>(1,self.traitCollection.displayScale);
    self.contentScaleFactor=scale;
    auto& s=detail::State();s.screenWidth=static_cast<int>(self.bounds.size.width);s.screenHeight=static_cast<int>(self.bounds.size.height);
    if(s.backend&&s.screenWidth>0&&s.screenHeight>0){s.backend->Resize(static_cast<int>(s.screenWidth*scale),static_cast<int>(s.screenHeight*scale));s.windowResized=true;}
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    CGPoint point=[[touches anyObject] locationInView:self];pending.mousePosition={static_cast<float>(point.x),static_cast<float>(point.y)};pending.mouseCurrent[0]=true;
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    CGPoint point=[[touches anyObject] locationInView:self];pending.mousePosition={static_cast<float>(point.x),static_cast<float>(point.y)};
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {pending.mouseCurrent[0]=false;}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {pending.mouseCurrent[0]=false;}
- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    for(UIPress* press in presses) {
        if(!press.key)continue;int key=KeyCode(press.key.keyCode);
        if(key>=0&&key<512){pending.keysCurrent[key]=true;pending.lastKeyPressed=key;}
        const char* text=press.key.characters.UTF8String;int length=0;if(text&&*text)pending.lastCharPressed=GetCodepointNext(text,&length);
    }
}
- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    for(UIPress* press in presses){if(!press.key)continue;int key=KeyCode(press.key.keyCode);if(key>=0&&key<512)pending.keysCurrent[key]=false;}
}
- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {[self pressesEnded:presses withEvent:event];}
@end

@interface MRSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow* window;
@end
@implementation MRSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    if(![scene isKindOfClass:[UIWindowScene class]]||appWindow)return;
    self.window=[[UIWindow alloc] initWithWindowScene:static_cast<UIWindowScene*>(scene)];appWindow=self.window;
    auto controller=[[UIViewController alloc] init];controller.preferredContentSize=CGSizeMake(requestedWidth,requestedHeight);
    auto view=[[MRRenderView alloc] initWithFrame:CGRectMake(0,0,requestedWidth,requestedHeight)];view.backgroundColor=UIColor.blackColor;
    controller.view=view;self.window.rootViewController=controller;renderView=view;[self.window makeKeyAndVisible];[view becomeFirstResponder];[view layoutIfNeeded];
    scene.title=[NSString stringWithUTF8String:requestedTitle.c_str()];
    try {
        InitWindow(requestedWidth,requestedHeight,requestedTitle);initialized=true;
        if(initializeCallback)initializeCallback();
        displayLink=[CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame:)];
        [displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        SetWindowSize(requestedWidth,requestedHeight);
    }catch(const std::exception& e){std::fprintf(stderr,"[visionOS] initialization: %s\n",e.what());Finish();}
}
- (void)drawFrame:(CADisplayLink*)link {
    if(closed||!IsWindowReady())return;
    try {
        float preferred=detail::State().timing.targetFrameTime>0?static_cast<float>(1/detail::State().timing.targetFrameTime):90;
        link.preferredFrameRateRange=CAFrameRateRangeMake(1,90,std::clamp(preferred,1.0f,90.0f));
        BeginDrawing();frameCallback();EndDrawing();
        if(detail::State().closeRequested) {
            Finish();[UIApplication.sharedApplication requestSceneSessionDestruction:appWindow.windowScene.session options:nil errorHandler:nil];
        }
    }catch(const std::exception& e){std::fprintf(stderr,"[visionOS] frame: %s\n",e.what());Finish();}
}
- (void)sceneWillResignActive:(UIScene*)scene {displayLink.paused=YES;pending={};}
- (void)sceneDidBecomeActive:(UIScene*)scene {displayLink.paused=NO;}
- (void)sceneDidDisconnect:(UIScene*)scene {Finish();renderView=nil;appWindow=nil;}
@end

@interface MRApplicationDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation MRApplicationDelegate
- (UISceneConfiguration*)application:(UIApplication*)application configurationForConnectingSceneSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    auto configuration=[[UISceneConfiguration alloc] initWithName:@"MeowyRender" sessionRole:session.role];configuration.delegateClass=[MRSceneDelegate class];return configuration;
}
@end

namespace meowyrender::detail {
void* PlatformCreateWindow(int,int,const char*,int /*backend*/) {
    if(!renderView)throw std::logic_error("visionOS requires RunApplication; InitWindow cannot start UIKit itself");
    closed=false;pending={};State().screenWidth=static_cast<int>(renderView.bounds.size.width);State().screenHeight=static_cast<int>(renderView.bounds.size.height);
    return (__bridge void*)renderView;
}
void PlatformDestroyWindow(void*) {closed=true;}
void PlatformInstallCallbacks(void*) {} // UIKit view overrides own input delivery.
void ShutdownGamepadRumble() {} // visionOS uses GameController haptics, not SDL.
void PlatformFramebufferSize(int* width,int* height) {
    CGFloat scale=std::max<CGFloat>(1,renderView.traitCollection.displayScale);
    *width=static_cast<int>(renderView.bounds.size.width*scale);*height=static_cast<int>(renderView.bounds.size.height*scale);
}
void PlatformPollInput() {
    auto& input=State().input;
    input.keysPrevious=input.keysCurrent;input.mousePrevious=input.mouseCurrent;input.previousGamepads=input.gamepads;
    input.mousePrevPosition=input.mousePosition;input.keysCurrent=pending.keysCurrent;input.mouseCurrent=pending.mouseCurrent;input.mousePosition=pending.mousePosition;
    input.mouseDelta=Vector2Subtract(input.mousePosition,input.mousePrevPosition);input.mouseWheel=pending.mouseWheel;pending.mouseWheel=0;
    input.lastKeyPressed=pending.lastKeyPressed;input.lastCharPressed=pending.lastCharPressed;pending.lastKeyPressed=pending.lastCharPressed=0;
    input.gamepads={};NSArray<GCController*>* controllers=GCController.controllers;
    for(NSUInteger i=0;i<std::min<NSUInteger>(16,controllers.count);++i) {
        auto pad=controllers[i].extendedGamepad;if(!pad)continue;auto& state=input.gamepads[i];state.available=true;
        GCControllerButtonInput* buttons[]={nil,pad.dpad.up,pad.dpad.right,pad.dpad.down,pad.dpad.left,pad.buttonY,pad.buttonB,pad.buttonA,pad.buttonX,pad.leftShoulder,pad.leftTrigger,pad.rightShoulder,pad.rightTrigger,pad.buttonOptions,pad.buttonHome,pad.buttonMenu,pad.leftThumbstickButton,pad.rightThumbstickButton};
        for(int button=1;button<18;++button)state.buttons[button]=buttons[button].isPressed;
        state.axes={pad.leftThumbstick.xAxis.value,-pad.leftThumbstick.yAxis.value,pad.rightThumbstick.xAxis.value,-pad.rightThumbstick.yAxis.value,pad.leftTrigger.value*2-1,pad.rightTrigger.value*2-1};
    }
}
}
namespace meowyrender {
void RunApplication(int width,int height,const std::string& title,std::function<void()> frame,std::function<void()> initialize,std::function<void()> cleanup) {
    if(!NSThread.isMainThread||!frame||width<=0||height<=0)throw std::invalid_argument("RunApplication requires the main thread, a frame callback and positive dimensions");
    frameCallback=std::move(frame);initializeCallback=std::move(initialize);cleanupCallback=std::move(cleanup);requestedWidth=width;requestedHeight=height;requestedTitle=title;
    char name[]="MeowyRender";char* arguments[]={name,nullptr};
    @autoreleasepool {UIApplicationMain(1,arguments,nil,NSStringFromClass([MRApplicationDelegate class]));}
}
bool WindowShouldClose(){return closed||detail::State().closeRequested;}
void ToggleFullscreen(){throw std::logic_error("visionOS windowed apps cannot toggle desktop fullscreen");}
void SetWindowTitle(const std::string& title){appWindow.windowScene.title=[NSString stringWithUTF8String:title.c_str()];}
void SetWindowSize(int width,int height) {
    if(width<=0||height<=0)return;
    auto preferences=[[UIWindowSceneGeometryPreferencesVision alloc] initWithSize:CGSizeMake(width,height)];
    [appWindow.windowScene requestGeometryUpdateWithPreferences:preferences errorHandler:^(NSError* error){std::fprintf(stderr,"[visionOS] window geometry: %s\n",error.localizedDescription.UTF8String);}];
}
void SetWindowPosition(int,int){throw std::logic_error("visionOS window placement is controlled by the user");}
int GetScreenWidth(){return detail::State().screenWidth;}
int GetScreenHeight(){return detail::State().screenHeight;}
Vector2 GetWindowPosition(){return {};}
int GetMonitorCount(){return appWindow?1:0;}
int GetCurrentMonitor(){return 0;}
Vector2 GetMonitorPosition(int){return {};}
int GetMonitorWidth(int){int width=0,height=0;detail::PlatformFramebufferSize(&width,&height);return width;}
int GetMonitorHeight(int){int width=0,height=0;detail::PlatformFramebufferSize(&width,&height);return height;}
int GetMonitorRefreshRate(int){return displayLink.duration>0?static_cast<int>(std::round(1/displayLink.duration)):0;}
const char* GetMonitorName(int){return "visionOS application window";}
void SetWindowMonitor(int monitor){if(monitor!=0)throw std::invalid_argument("No such visionOS window surface");}
void SetClipboardText(const std::string& text){UIPasteboard.generalPasteboard.string=[NSString stringWithUTF8String:text.c_str()];}
const char* GetClipboardText(){const char* text=UIPasteboard.generalPasteboard.string.UTF8String;clipboard=text?text:"";return clipboard.c_str();}
void SetMouseCursor(MouseCursor){/* Gaze/pointer appearance is managed by the system. */}
void OpenURL(const std::string& url) {
    if(!url.starts_with("https://")&&!url.starts_with("http://")&&!url.starts_with("mailto:"))throw std::invalid_argument("Unsupported URL scheme");
    NSURL* native=[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if(native)[UIApplication.sharedApplication openURL:native options:@{} completionHandler:nil];
}

// --- rcore parity additions on visionOS ---
// The window surface is system-managed; state toggles that don't map to a
// windowed desktop concept are no-ops or report their fixed value.
int GetMonitorPhysicalWidth(int){return 0;}
int GetMonitorPhysicalHeight(int){return 0;}
int GetRenderWidth(){int w=0,h=0;detail::PlatformFramebufferSize(&w,&h);return w;}
int GetRenderHeight(){int w=0,h=0;detail::PlatformFramebufferSize(&w,&h);return h;}
Vector2 GetWindowScaleDPI(){CGFloat s=renderView?std::max<CGFloat>(1,renderView.traitCollection.displayScale):1;return {static_cast<float>(s),static_cast<float>(s)};}
void* GetWindowHandle(){return detail::State().window;}

bool IsWindowState(unsigned int flag){return (detail::State().configFlags&flag)!=0;}
void SetWindowState(unsigned int flags){detail::State().configFlags|=flags;}
void ClearWindowState(unsigned int flags){detail::State().configFlags&=~flags;}
bool IsWindowHidden(){return false;}
bool IsWindowMinimized(){return false;}
bool IsWindowMaximized(){return true;}   // the app fills its scene
bool IsWindowFocused(){return !closed;}
void MinimizeWindow(){}
void MaximizeWindow(){}
void RestoreWindow(){}
void SetWindowMinSize(int,int){}
void SetWindowMaxSize(int,int){}
void SetWindowOpacity(float){}
void SetWindowFocused(){}
void SetWindowIcon(Image){}
void SetWindowIcons(Image*,int){}

void ShowCursor(){}
void HideCursor(){}
bool IsCursorHidden(){return false;}
void EnableCursor(){}
void DisableCursor(){}
bool IsCursorOnScreen(){return true;}

void SetMousePosition(int x,int y){detail::State().input.mousePosition={static_cast<float>(x),static_cast<float>(y)};}
const char* GetKeyName(KeyboardKey){return "";}
int SetGamepadMappings(const std::string&){return 0;}
void SetGamepadVibration(int,float,float,float){}
}
