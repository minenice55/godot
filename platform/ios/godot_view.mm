/*************************************************************************/
/*  godot_view.mm                                                        */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#import "godot_view.h"

#include "core/config/project_settings.h"
#include "core/os/keyboard.h"
#include "core/string/ustring.h"
#import "display_layer.h"
#include "display_server_ios.h"
#import "godot_view_gesture_recognizer.h"
#import "godot_view_renderer.h"

#import <CoreMotion/CoreMotion.h>

static const int max_touches = 8;
static const float earth_gravity = 9.80665;

@interface GodotView () {
	UITouch *godot_touches[max_touches];
}

@property(assign, nonatomic) BOOL isActive;

// CADisplayLink available on 3.1+ synchronizes the animation timer & drawing with the refresh rate of the display, only supports animation intervals of 1/60 1/30 & 1/15
@property(strong, nonatomic) CADisplayLink *displayLink;

// An animation timer that, when animation is started, will periodically call -drawView at the given rate.
// Only used if CADisplayLink is not
@property(strong, nonatomic) NSTimer *animationTimer;

@property(strong, nonatomic) CALayer<DisplayLayer> *renderingLayer;

@property(strong, nonatomic) CMMotionManager *motionManager;

@property(strong, nonatomic) GodotViewGestureRecognizer *delayGestureRecognizer;

@end

@implementation GodotView

- (CALayer<DisplayLayer> *)initializeRenderingForDriver:(NSString *)driverName {
	if (self.renderingLayer) {
		return self.renderingLayer;
	}

	CALayer<DisplayLayer> *layer;

	if ([driverName isEqualToString:@"vulkan"]) {
		layer = [GodotMetalLayer layer];
	} else if ([driverName isEqualToString:@"opengl3"]) {
		if (@available(iOS 13, *)) {
			NSLog(@"OpenGL ES is deprecated on iOS 13");
		}
#if defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
		return nil;
#else
		layer = [GodotOpenGLLayer layer];
#endif
	} else {
		return nil;
	}

	layer.frame = self.bounds;
	layer.contentsScale = self.contentScaleFactor;

	[self.layer addSublayer:layer];
	self.renderingLayer = layer;

	[layer initializeDisplayLayer];

	return self.renderingLayer;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
	self = [super initWithCoder:coder];

	if (self) {
		[self godot_commonInit];
	}

	return self;
}

- (instancetype)initWithFrame:(CGRect)frame {
	self = [super initWithFrame:frame];

	if (self) {
		[self godot_commonInit];
	}

	return self;
}

- (void)dealloc {
	[self stopRendering];

	self.renderer = nil;
	self.delegate = nil;

	if (self.renderingLayer) {
		[self.renderingLayer removeFromSuperlayer];
		self.renderingLayer = nil;
	}

	if (self.motionManager) {
		[self.motionManager stopDeviceMotionUpdates];
		self.motionManager = nil;
	}

	if (self.displayLink) {
		[self.displayLink invalidate];
		self.displayLink = nil;
	}

	if (self.animationTimer) {
		[self.animationTimer invalidate];
		self.animationTimer = nil;
	}

	if (self.delayGestureRecognizer) {
		self.delayGestureRecognizer = nil;
	}
}

- (void)godot_commonInit {
	self.contentScaleFactor = [UIScreen mainScreen].scale;

	[self initTouches];

	self.multipleTouchEnabled = YES;

	// Configure and start accelerometer
	if (!self.motionManager) {
		self.motionManager = [[CMMotionManager alloc] init];
		if (self.motionManager.deviceMotionAvailable) {
			self.motionManager.deviceMotionUpdateInterval = 1.0 / 70.0;
			[self.motionManager startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXMagneticNorthZVertical];
		} else {
			self.motionManager = nil;
		}
	}

	// Initialize delay gesture recognizer
	GodotViewGestureRecognizer *gestureRecognizer = [[GodotViewGestureRecognizer alloc] init];
	self.delayGestureRecognizer = gestureRecognizer;
	[self addGestureRecognizer:self.delayGestureRecognizer];
}

- (void)stopRendering {
	if (!self.isActive) {
		return;
	}

	self.isActive = NO;

	printf("******** stop animation!\n");

	if (self.useCADisplayLink) {
		[self.displayLink invalidate];
		self.displayLink = nil;
	} else {
		[self.animationTimer invalidate];
		self.animationTimer = nil;
	}

	[self clearTouches];
}

- (void)startRendering {
	if (self.isActive) {
		return;
	}

	self.isActive = YES;

	printf("start animation!\n");

	if (self.useCADisplayLink) {
		self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawView)];

		if (GLOBAL_GET("display/window/ios/allow_high_refresh_rate")) {
			self.displayLink.preferredFramesPerSecond = 120;
		} else {
			self.displayLink.preferredFramesPerSecond = 60;
		}

		// Setup DisplayLink in main thread
		[self.displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
	} else {
		self.animationTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60) target:self selector:@selector(drawView) userInfo:nil repeats:YES];
	}
}

- (void)drawView {
	if (!self.isActive) {
		printf("draw view not active!\n");
		return;
	}

	if (self.useCADisplayLink) {
		// Pause the CADisplayLink to avoid recursion
		[self.displayLink setPaused:YES];

		// Process all input events
		while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, TRUE) == kCFRunLoopRunHandledSource) {
			// Continue.
		}

		// We are good to go, resume the CADisplayLink
		[self.displayLink setPaused:NO];
	}

	[self.renderingLayer renderDisplayLayer];

	if (!self.renderer) {
		return;
	}

	if ([self.renderer setupView:self]) {
		return;
	}

	if (self.delegate) {
		BOOL delegateFinishedSetup = [self.delegate godotViewFinishedSetup:self];

		if (!delegateFinishedSetup) {
			return;
		}
	}

	[self handleMotion];
	[self.renderer renderOnView:self];
}

- (BOOL)canRender {
	if (self.useCADisplayLink) {
		return self.displayLink != nil;
	} else {
		return self.animationTimer != nil;
	}
}

- (void)setRenderingInterval:(NSTimeInterval)renderingInterval {
	_renderingInterval = renderingInterval;

	if (self.canRender) {
		[self stopRendering];
		[self startRendering];
	}
}

- (void)layoutSubviews {
	if (self.renderingLayer) {
		self.renderingLayer.frame = self.bounds;
		[self.renderingLayer layoutDisplayLayer];

		if (DisplayServerIOS::get_singleton()) {
			DisplayServerIOS::get_singleton()->resize_window(self.bounds.size);
		}
	}

	[super layoutSubviews];
}

// MARK: - Input

// MARK: Touches

- (void)initTouches {
	for (int i = 0; i < max_touches; i++) {
		godot_touches[i] = nullptr;
	}
}

- (int)getTouchIDForTouch:(UITouch *)p_touch {
	int first = -1;
	for (int i = 0; i < max_touches; i++) {
		if (first == -1 && godot_touches[i] == nullptr) {
			first = i;
			continue;
		}
		if (godot_touches[i] == p_touch) {
			return i;
		}
	}

	if (first != -1) {
		godot_touches[first] = p_touch;
		return first;
	}

	return -1;
}

- (int)removeTouch:(UITouch *)p_touch {
	int remaining = 0;
	for (int i = 0; i < max_touches; i++) {
		if (godot_touches[i] == nullptr) {
			continue;
		}
		if (godot_touches[i] == p_touch) {
			godot_touches[i] = nullptr;
		} else {
			++remaining;
		}
	}
	return remaining;
}

- (void)clearTouches {
	for (int i = 0; i < max_touches; i++) {
		godot_touches[i] = nullptr;
	}
}

- (void)godotTouchesBegan:(NSSet *)touchesSet withEvent:(UIEvent *)event {
	NSArray *tlist = [event.allTouches allObjects];
	for (unsigned int i = 0; i < [tlist count]; i++) {
		if ([touchesSet containsObject:[tlist objectAtIndex:i]]) {
			UITouch *touch = [tlist objectAtIndex:i];
			int tid = [self getTouchIDForTouch:touch];
			ERR_FAIL_COND(tid == -1);
			CGPoint touchPoint = [touch locationInView:self];
			DisplayServerIOS::get_singleton()->touch_press(tid, touchPoint.x * self.contentScaleFactor, touchPoint.y * self.contentScaleFactor, true, touch.tapCount > 1);
		}
	}
}

- (void)godotTouchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
	NSArray *tlist = [event.allTouches allObjects];
	for (unsigned int i = 0; i < [tlist count]; i++) {
		if ([touches containsObject:[tlist objectAtIndex:i]]) {
			UITouch *touch = [tlist objectAtIndex:i];
			int tid = [self getTouchIDForTouch:touch];
			ERR_FAIL_COND(tid == -1);
			CGPoint touchPoint = [touch locationInView:self];
			CGPoint prev_point = [touch previousLocationInView:self];
			DisplayServerIOS::get_singleton()->touch_drag(tid, prev_point.x * self.contentScaleFactor, prev_point.y * self.contentScaleFactor, touchPoint.x * self.contentScaleFactor, touchPoint.y * self.contentScaleFactor);
		}
	}
}

- (void)godotTouchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
	NSArray *tlist = [event.allTouches allObjects];
	for (unsigned int i = 0; i < [tlist count]; i++) {
		if ([touches containsObject:[tlist objectAtIndex:i]]) {
			UITouch *touch = [tlist objectAtIndex:i];
			int tid = [self getTouchIDForTouch:touch];
			ERR_FAIL_COND(tid == -1);
			[self removeTouch:touch];
			CGPoint touchPoint = [touch locationInView:self];
			DisplayServerIOS::get_singleton()->touch_press(tid, touchPoint.x * self.contentScaleFactor, touchPoint.y * self.contentScaleFactor, false, false);
		}
	}
}

- (void)godotTouchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
	NSArray *tlist = [event.allTouches allObjects];
	for (unsigned int i = 0; i < [tlist count]; i++) {
		if ([touches containsObject:[tlist objectAtIndex:i]]) {
			UITouch *touch = [tlist objectAtIndex:i];
			int tid = [self getTouchIDForTouch:touch];
			ERR_FAIL_COND(tid == -1);
			DisplayServerIOS::get_singleton()->touches_cancelled(tid);
		}
	}
	[self clearTouches];
}

// MARK: Motion

- (void)handleMotion {
	if (!self.motionManager) {
		return;
	}

	// Just using polling approach for now, we can set this up so it sends
	// data to us in intervals, might be better. See Apple reference pages
	// for more details:
	// https://developer.apple.com/reference/coremotion/cmmotionmanager?language=objc

	// Apple splits our accelerometer date into a gravity and user movement
	// component. We add them back together.
	CMAcceleration gravity = self.motionManager.deviceMotion.gravity;
	CMAcceleration acceleration = self.motionManager.deviceMotion.userAcceleration;

	// To be consistent with Android we convert the unit of measurement from g (Earth's gravity)
	// to m/s^2.
	gravity.x *= earth_gravity;
	gravity.y *= earth_gravity;
	gravity.z *= earth_gravity;
	acceleration.x *= earth_gravity;
	acceleration.y *= earth_gravity;
	acceleration.z *= earth_gravity;

	///@TODO We don't seem to be getting data here, is my device broken or
	/// is this code incorrect?
	CMMagneticField magnetic = self.motionManager.deviceMotion.magneticField.field;

	///@TODO we can access rotationRate as a CMRotationRate variable
	///(processed date) or CMGyroData (raw data), have to see what works
	/// best
	CMRotationRate rotation = self.motionManager.deviceMotion.rotationRate;

	// Adjust for screen orientation.
	// [[UIDevice currentDevice] orientation] changes even if we've fixed
	// our orientation which is not a good thing when you're trying to get
	// your user to move the screen in all directions and want consistent
	// output

	///@TODO Using [[UIApplication sharedApplication] statusBarOrientation]
	/// is a bit of a hack. Godot obviously knows the orientation so maybe
	/// we
	// can use that instead? (note that left and right seem swapped)

	UIInterfaceOrientation interfaceOrientation = UIInterfaceOrientationUnknown;

	if (@available(iOS 13, *)) {
		interfaceOrientation = [UIApplication sharedApplication].delegate.window.windowScene.interfaceOrientation;
#if !defined(TARGET_OS_SIMULATOR) || !TARGET_OS_SIMULATOR
	} else {
		interfaceOrientation = [[UIApplication sharedApplication] statusBarOrientation];
#endif
	}

	switch (interfaceOrientation) {
		case UIInterfaceOrientationLandscapeLeft: {
			DisplayServerIOS::get_singleton()->update_gravity(-gravity.y, gravity.x, gravity.z);
			DisplayServerIOS::get_singleton()->update_accelerometer(-(acceleration.y + gravity.y), (acceleration.x + gravity.x), acceleration.z + gravity.z);
			DisplayServerIOS::get_singleton()->update_magnetometer(-magnetic.y, magnetic.x, magnetic.z);
			DisplayServerIOS::get_singleton()->update_gyroscope(-rotation.y, rotation.x, rotation.z);
		} break;
		case UIInterfaceOrientationLandscapeRight: {
			DisplayServerIOS::get_singleton()->update_gravity(gravity.y, -gravity.x, gravity.z);
			DisplayServerIOS::get_singleton()->update_accelerometer((acceleration.y + gravity.y), -(acceleration.x + gravity.x), acceleration.z + gravity.z);
			DisplayServerIOS::get_singleton()->update_magnetometer(magnetic.y, -magnetic.x, magnetic.z);
			DisplayServerIOS::get_singleton()->update_gyroscope(rotation.y, -rotation.x, rotation.z);
		} break;
		case UIInterfaceOrientationPortraitUpsideDown: {
			DisplayServerIOS::get_singleton()->update_gravity(-gravity.x, gravity.y, gravity.z);
			DisplayServerIOS::get_singleton()->update_accelerometer(-(acceleration.x + gravity.x), (acceleration.y + gravity.y), acceleration.z + gravity.z);
			DisplayServerIOS::get_singleton()->update_magnetometer(-magnetic.x, magnetic.y, magnetic.z);
			DisplayServerIOS::get_singleton()->update_gyroscope(-rotation.x, rotation.y, rotation.z);
		} break;
		default: { // assume portrait
			DisplayServerIOS::get_singleton()->update_gravity(gravity.x, gravity.y, gravity.z);
			DisplayServerIOS::get_singleton()->update_accelerometer(acceleration.x + gravity.x, acceleration.y + gravity.y, acceleration.z + gravity.z);
			DisplayServerIOS::get_singleton()->update_magnetometer(magnetic.x, magnetic.y, magnetic.z);
			DisplayServerIOS::get_singleton()->update_gyroscope(rotation.x, rotation.y, rotation.z);
		} break;
	}
}

@end
