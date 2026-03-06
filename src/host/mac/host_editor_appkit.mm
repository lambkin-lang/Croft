#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/host_editor_appkit.h"

#import <AppKit/AppKit.h>

#include <float.h>
#include <cstdio>
#include <cstdlib>

@interface CroftEditorController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextViewDelegate>
- (instancetype)initWithDocument:(croft_editor_document*)document
                           title:(NSString*)title
                 autoCloseMillis:(NSInteger)autoCloseMillis;
@end

@implementation CroftEditorController {
    croft_editor_document* _document;
    NSString* _windowTitle;
    NSWindow* _window;
    NSTextView* _textView;
    BOOL _syncingFromDocument;
    NSInteger _autoCloseMillis;
}

- (instancetype)initWithDocument:(croft_editor_document*)document
                           title:(NSString*)title
                 autoCloseMillis:(NSInteger)autoCloseMillis {
    self = [super init];
    if (self) {
        _document = document;
        _windowTitle = title;
        _autoCloseMillis = autoCloseMillis;
    }
    return self;
}

- (void)buildMenuBar {
    NSMenu* menuBar = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuBar addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Application"];
    NSString* appName = _windowTitle ?: @"Croft";
    NSString* quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                      action:@selector(terminate:)
                                               keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appMenuItem setSubmenu:appMenu];

    NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    [menuBar addItem:fileMenuItem];

    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem* saveItem = [[NSMenuItem alloc] initWithTitle:@"Save"
                                                      action:@selector(saveDocument:)
                                               keyEquivalent:@"s"];
    [saveItem setTarget:self];
    [saveItem setEnabled:(croft_editor_document_path(_document) != NULL)];
    [fileMenu addItem:saveItem];
    [fileMenuItem setSubmenu:fileMenu];

    [NSApp setMainMenu:menuBar];
}

- (void)updateWindowTitle {
    NSString* title = _windowTitle ?: @"Croft";
    const char* path = croft_editor_document_path(_document);
    if (path && path[0] != '\0') {
        NSString* pathString = [NSString stringWithUTF8String:path];
        NSString* leaf = pathString.lastPathComponent;
        if (leaf.length > 0) {
            title = [NSString stringWithFormat:@"%@ - %@", leaf, title];
        }
    } else {
        title = [NSString stringWithFormat:@"Untitled - %@", title];
    }

    [_window setTitle:title];
    [_window setDocumentEdited:croft_editor_document_is_dirty(_document) ? YES : NO];
}

- (void)loadDocumentIntoView {
    char* utf8 = NULL;
    size_t utf8Len = 0;
    int32_t rc = croft_editor_document_export_utf8(_document, &utf8, &utf8Len);
    NSString* text = @"";

    if (rc == 0 && utf8) {
        NSString* decoded = [[NSString alloc] initWithBytes:utf8
                                                     length:utf8Len
                                                   encoding:NSUTF8StringEncoding];
        if (decoded) {
            text = decoded;
        }
    }

    _syncingFromDocument = YES;
    [_textView setString:text];
    _syncingFromDocument = NO;
    free(utf8);
    [self updateWindowTitle];
}

- (void)syncTextViewToDocument {
    NSData* utf8 = [[_textView string] dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    const void* bytes = utf8 ? utf8.bytes : "";
    size_t len = utf8 ? (size_t)utf8.length : 0u;
    int32_t rc = croft_editor_document_replace_utf8(_document, (const uint8_t*)bytes, len);

    if (rc != 0) {
        std::printf("croft_editor_appkit: failed to sync AppKit text into Sapling (%d)\n", rc);
        return;
    }

    [self updateWindowTitle];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    [self buildMenuBar];

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 1000, 760)
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskResizable |
                                                     NSWindowStyleMaskMiniaturizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setDelegate:self];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:[[_window contentView] bounds]];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:YES];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    NSTextView* textView = [[NSTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
    [textView setMinSize:NSMakeSize(0.0, 0.0)];
    [textView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [textView setVerticallyResizable:YES];
    [textView setHorizontallyResizable:YES];
    [textView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [textView setRichText:NO];
    [textView setUsesFindBar:YES];
    [textView setAutomaticQuoteSubstitutionEnabled:NO];
    [textView setAutomaticDashSubstitutionEnabled:NO];
    [textView setAutomaticTextReplacementEnabled:NO];
    [textView setAutomaticDataDetectionEnabled:NO];
    [textView setContinuousSpellCheckingEnabled:NO];
    [textView setGrammarCheckingEnabled:NO];
    [textView setFont:[NSFont monospacedSystemFontOfSize:15.0 weight:NSFontWeightRegular]];
    [textView setDelegate:self];

    [scrollView setDocumentView:textView];
    [[_window contentView] addSubview:scrollView];

    _textView = textView;
    [self loadDocumentIntoView];

    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    if (_autoCloseMillis > 0) {
        [self performSelector:@selector(terminateLater)
                   withObject:nil
                   afterDelay:((NSTimeInterval)_autoCloseMillis / 1000.0)];
    }
}

- (void)textDidChange:(NSNotification*)notification {
    (void)notification;
    if (_syncingFromDocument) {
        return;
    }
    [self syncTextViewToDocument];
}

- (IBAction)saveDocument:(id)sender {
    (void)sender;
    int32_t rc = croft_editor_document_save(_document);
    if (rc != 0) {
        std::printf("croft_editor_appkit: save failed (%d)\n", rc);
        NSBeep();
        return;
    }

    [self updateWindowTitle];
}

- (void)terminateLater {
    [NSApp terminate:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [NSApp terminate:nil];
}

@end

extern "C" int32_t croft_editor_appkit_run(croft_editor_document* document,
                                           const croft_editor_appkit_options* options) {
    @autoreleasepool {
        NSString* title = @"Croft AppKit Editor";
        NSInteger autoCloseMillis = 0;

        if (!document) {
            return -1;
        }

        if (options) {
            if (options->window_title) {
                title = [NSString stringWithUTF8String:options->window_title];
            }
            autoCloseMillis = options->auto_close_millis;
        }

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        CroftEditorController* controller =
            [[CroftEditorController alloc] initWithDocument:document
                                                      title:title
                                            autoCloseMillis:autoCloseMillis];
        [NSApp setDelegate:controller];
        [NSApp run];
    }

    return 0;
}
