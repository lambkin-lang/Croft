#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#import "croft/host_a11y.h"

//
// CroftAccessibilityElement
// Custom bridging class mapping to NSAccessibility protocol.
//
@interface CroftAccessibilityElement : NSAccessibilityElement {
@public
    host_a11y_role _croftRole;
    NSRect _frameInScreenCoordinates;
    NSString* _label;
    NSString* _value;
    NSMutableArray<CroftAccessibilityElement*>* _children;
    __weak id _parent;
}

- (instancetype)initWithRole:(host_a11y_role)role config:(const host_a11y_node_config*)config;
- (void)addChild:(CroftAccessibilityElement*)child;
- (void)updateFrame:(NSRect)frame;
- (void)updateLabel:(const char*)label;
- (void)updateValue:(const char*)value;

@end

@implementation CroftAccessibilityElement

- (instancetype)initWithRole:(host_a11y_role)role config:(const host_a11y_node_config*)config {
    self = [super init];
    if (self) {
        _croftRole = role;
        _frameInScreenCoordinates = NSMakeRect(config->x, config->y, config->width, config->height);
        _children = [[NSMutableArray alloc] init];
        
        if (config->label) {
            _label = [NSString stringWithUTF8String:config->label];
        } else {
            _label = @"";
        }
        _value = _label;
        
        // Map our agnostic role to the Apple-specific NSAccessibilityRole string mix-in
        switch (role) {
            case ROLE_WINDOW:
                [self setAccessibilityRole:NSAccessibilityWindowRole];
                break;
            case ROLE_GROUP:
                [self setAccessibilityRole:NSAccessibilityGroupRole];
                break;
            case ROLE_TEXT:
                // Apple specifically wants StaticText for read-only blocks
                [self setAccessibilityRole:NSAccessibilityStaticTextRole];
                break;
            case ROLE_BUTTON:
                [self setAccessibilityRole:NSAccessibilityButtonRole];
                break;
            case ROLE_TEXT_AREA:
                [self setAccessibilityRole:NSAccessibilityTextAreaRole];
                break;
            default:
                [self setAccessibilityRole:NSAccessibilityUnknownRole];
                break;
        }
        
        [self setAccessibilityLabel:_label];
        [self setAccessibilityValue:_value];
    }
    return self;
}

- (void)addChild:(CroftAccessibilityElement*)child {
    [_children addObject:child];
    child->_parent = self;
}

- (void)updateFrame:(NSRect)frame {
    _frameInScreenCoordinates = frame;
}

- (void)updateLabel:(const char*)label {
    if (label) {
        _label = [NSString stringWithUTF8String:label];
    } else {
        _label = @"";
    }
    [self setAccessibilityLabel:_label];
}

- (void)updateValue:(const char*)value {
    if (value) {
        _value = [NSString stringWithUTF8String:value];
    } else {
        _value = @"";
    }
    [self setAccessibilityValue:_value];
}

// OS queries this to build the geometry boundaries dynamically
- (NSRect)accessibilityFrame {
    return _frameInScreenCoordinates;
}

- (NSString*)accessibilityLabel {
    return _label;
}

- (id)accessibilityValue {
    return _value;
}

- (id)accessibilityParent {
    return _parent;
}

- (NSArray *)accessibilityChildren {
    return _children;
}

@end

//
// Global Root State Object
//
static NSWindow* g_nativeWindow = nil;
static NSMutableArray<CroftAccessibilityElement*>* g_rootChildren = nil;

//
// C-API Implementation
//

int32_t host_a11y_init(void *native_window_handle) {
    if (!native_window_handle) return -1;
    
    // We expect the glfwGetCocoaWindow() handle
    g_nativeWindow = (__bridge NSWindow*)native_window_handle;
    g_rootChildren = [[NSMutableArray alloc] init];
    
    // Wire up the root children array to the actual NSWindow's contentView
    // so Apple's VoiceOver scanner will enter it seamlessly.
    NSView* contentView = [g_nativeWindow contentView];
    if (contentView) {
        // Technically, replacing accessibilityChildren outright can conflict with standard NSView subviews.
        // For a true custom drawn tree, we set the view's accessibility children to our proxy array.
        [contentView setAccessibilityChildren:g_rootChildren];
    }
    
    return 0;
}

void host_a11y_terminate(void) {
    g_nativeWindow = nil;
    [g_rootChildren removeAllObjects];
    g_rootChildren = nil;
}

void* host_a11y_create_node(host_a11y_role role, const host_a11y_node_config* config) {
    CroftAccessibilityElement* element = [[CroftAccessibilityElement alloc] initWithRole:role config:config];
    // Return an opaque pointer retained manually or relying on ARC if returned to unmanaged code.
    // ARC transfers +1 when returning to void* with __bridge_retained:
    return (void*)CFBridgingRetain(element);
}

void host_a11y_add_child(void* parent_a11y_node, void* child_a11y_node) {
    if (!child_a11y_node) return;
    
    CroftAccessibilityElement* child = (__bridge CroftAccessibilityElement*)child_a11y_node;
    
    if (parent_a11y_node) {
        CroftAccessibilityElement* parent = (__bridge CroftAccessibilityElement*)parent_a11y_node;
        [parent addChild:child];
    } else {
        // Top level logical element attaches to the Window
        if (g_rootChildren) {
            [g_rootChildren addObject:child];
            child->_parent = [g_nativeWindow contentView];
        }
    }
}

void host_a11y_update_frame(void* a11y_node, float x, float y, float w, float h) {
    if (!a11y_node) return;
    
    CroftAccessibilityElement* node = (__bridge CroftAccessibilityElement*)a11y_node;
    // Note: Mac screen coordinates have origin at bottom-left conventionally,
    // whereas GLFW is top-left. Depending on our Scene graph origins we might need
    // to invert the Y axis using NSScreen here. But for testing, we just pass raw bounds.
    [node updateFrame:NSMakeRect(x, y, w, h)];
}

void host_a11y_update_label(void* a11y_node, const char* label) {
    if (!a11y_node) return;

    CroftAccessibilityElement* node = (__bridge CroftAccessibilityElement*)a11y_node;
    [node updateLabel:label];
}

void host_a11y_update_value(void* a11y_node, const char* value) {
    if (!a11y_node) return;

    CroftAccessibilityElement* node = (__bridge CroftAccessibilityElement*)a11y_node;
    [node updateValue:value];
}

void host_a11y_destroy_node(void* a11y_node) {
    if (!a11y_node) return;
    
    // Release the +1 retain count from CFBridgingRetain
    CFRelease(a11y_node);
}
