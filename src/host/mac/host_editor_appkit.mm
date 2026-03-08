#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/editor_status.h"
#include "croft/editor_text_model.h"
#include "croft/host_editor_appkit.h"

#import <AppKit/AppKit.h>

#include <float.h>
#include <cstdio>
#include <cstdlib>

static uint32_t croft_editor_appkit_count_utf8_codepoints(const uint8_t* utf8, size_t utf8_len) {
    size_t offset = 0u;
    uint32_t count = 0u;

    while (utf8 && offset < utf8_len) {
        unsigned char ch = utf8[offset];
        if ((ch & 0x80u) == 0u) {
            offset += 1u;
        } else if ((ch & 0xE0u) == 0xC0u) {
            offset += 2u;
        } else if ((ch & 0xF0u) == 0xE0u) {
            offset += 3u;
        } else if ((ch & 0xF8u) == 0xF0u) {
            offset += 4u;
        } else {
            break;
        }
        if (offset > utf8_len) {
            break;
        }
        count++;
    }

    return count;
}

@interface CroftEditorTextView : NSTextView
- (NSUInteger)croft_currentLineNumber;
@end

@implementation CroftEditorTextView

- (NSRect)croft_currentLineRect {
    NSLayoutManager* layoutManager = self.layoutManager;
    NSString* text = self.string ?: @"";
    NSUInteger selectionLocation = MIN(self.selectedRange.location, text.length);
    NSUInteger lookupLocation = selectionLocation;
    NSUInteger lineStart = 0;
    NSUInteger lineEnd = 0;
    NSRect lineRect = NSZeroRect;

    if (!layoutManager) {
        return NSZeroRect;
    }

    if (lookupLocation > 0
            && lookupLocation == text.length
            && [text characterAtIndex:text.length - 1] != '\n') {
        lookupLocation--;
    }

    if (text.length > 0) {
        [text getLineStart:&lineStart end:&lineEnd contentsEnd:NULL forRange:NSMakeRange(lookupLocation, 0)];
    }

    if (selectionLocation == text.length
            && text.length > 0
            && [text characterAtIndex:text.length - 1] == '\n') {
        lineRect = layoutManager.extraLineFragmentRect;
    } else if (text.length > 0) {
        NSRange glyphRange =
            [layoutManager glyphRangeForCharacterRange:NSMakeRange(lineStart, 0)
                                  actualCharacterRange:NULL];
        if (glyphRange.location < layoutManager.numberOfGlyphs) {
            lineRect = [layoutManager lineFragmentRectForGlyphAtIndex:glyphRange.location effectiveRange:NULL];
        }
    } else {
        lineRect = layoutManager.extraLineFragmentRect;
    }

    if (NSIsEmptyRect(lineRect)) {
        CGFloat fallbackHeight = self.font ? self.font.ascender - self.font.descender + 6.0 : 18.0;
        lineRect = NSMakeRect(0.0,
                              self.textContainerInset.height,
                              self.bounds.size.width,
                              fallbackHeight);
    }

    lineRect.origin.x = 0.0;
    lineRect.size.width = self.bounds.size.width;
    return lineRect;
}

- (NSUInteger)croft_currentLineNumber {
    NSString* text = self.string ?: @"";
    NSUInteger selectionLocation = MIN(self.selectedRange.location, text.length);
    NSUInteger lineNumber = 1;

    for (NSUInteger index = 0; index < selectionLocation; index++) {
        if ([text characterAtIndex:index] == '\n') {
            lineNumber++;
        }
    }

    return lineNumber;
}

- (void)drawViewBackgroundInRect:(NSRect)rect {
    [super drawViewBackgroundInRect:rect];

    NSRect lineRect = [self croft_currentLineRect];
    NSRect visibleRect = NSIntersectionRect(rect, lineRect);
    if (!NSIsEmptyRect(visibleRect)) {
        [[NSColor colorWithCalibratedRed:0.86 green:0.91 blue:0.97 alpha:1.0] setFill];
        NSRectFill(visibleRect);
    }
}

@end

@interface CroftLineNumberRulerView : NSRulerView
- (instancetype)initWithTextView:(CroftEditorTextView*)textView;
- (void)invalidateMetrics;
@end

@implementation CroftLineNumberRulerView {
    __weak CroftEditorTextView* _textView;
}

- (instancetype)initWithTextView:(CroftEditorTextView*)textView {
    self = [super initWithScrollView:textView.enclosingScrollView orientation:NSVerticalRuler];
    if (self) {
        _textView = textView;
        self.clientView = textView;
        [self invalidateMetrics];
    }
    return self;
}

- (NSUInteger)lineCount {
    NSString* text = _textView.string ?: @"";
    NSUInteger count = 1;

    for (NSUInteger index = 0; index < text.length; index++) {
        if ([text characterAtIndex:index] == '\n') {
            count++;
        }
    }

    return count;
}

- (NSUInteger)lineNumberForCharacterIndex:(NSUInteger)characterIndex {
    NSString* text = _textView.string ?: @"";
    NSUInteger limit = MIN(characterIndex, text.length);
    NSUInteger lineNumber = 1;

    for (NSUInteger index = 0; index < limit; index++) {
        if ([text characterAtIndex:index] == '\n') {
            lineNumber++;
        }
    }

    return lineNumber;
}

- (CGFloat)requiredThickness {
    NSUInteger digits = croft_editor_line_number_digits((uint32_t)[self lineCount]);
    NSMutableString* sample = [NSMutableString stringWithCapacity:digits];
    NSDictionary* attributes;
    NSSize sampleSize;

    for (NSUInteger index = 0; index < digits; index++) {
        [sample appendString:@"8"];
    }

    attributes = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular]
    };
    sampleSize = [sample sizeWithAttributes:attributes];
    return ceil(sampleSize.width + 20.0);
}

- (void)invalidateMetrics {
    self.ruleThickness = [self requiredThickness];
    [self setNeedsDisplay:YES];
}

- (void)drawHashMarksAndLabelsInRect:(NSRect)rect {
    NSLayoutManager* layoutManager = _textView.layoutManager;
    NSTextContainer* textContainer = _textView.textContainer;
    NSClipView* clipView = self.scrollView.contentView;
    NSRect visibleRect = clipView.documentVisibleRect;
    NSUInteger currentLine = [_textView croft_currentLineNumber];
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    NSDictionary* inactiveAttributes;
    NSDictionary* activeAttributes;
    NSRect borderRect;

    (void)rect;

    [[NSColor colorWithCalibratedWhite:0.93 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    borderRect = NSMakeRect(NSMaxX(self.bounds) - 1.0, 0.0, 1.0, NSHeight(self.bounds));
    [[NSColor colorWithCalibratedWhite:0.80 alpha:1.0] setFill];
    NSRectFill(borderRect);

    if (!layoutManager || !textContainer) {
        return;
    }

    style.alignment = NSTextAlignmentRight;
    inactiveAttributes = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.40 alpha:1.0],
        NSParagraphStyleAttributeName: style
    };
    activeAttributes = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.10 green:0.22 blue:0.38 alpha:1.0],
        NSParagraphStyleAttributeName: style
    };

    NSRange visibleGlyphRange =
        [layoutManager glyphRangeForBoundingRect:visibleRect inTextContainer:textContainer];
    NSUInteger glyphIndex = visibleGlyphRange.location;
    while (glyphIndex < NSMaxRange(visibleGlyphRange) && glyphIndex < layoutManager.numberOfGlyphs) {
        NSRange lineGlyphRange;
        NSRect lineRect = [layoutManager lineFragmentRectForGlyphAtIndex:glyphIndex
                                                          effectiveRange:&lineGlyphRange];
        NSUInteger characterIndex = [layoutManager characterIndexForGlyphAtIndex:glyphIndex];
        NSUInteger lineNumber = [self lineNumberForCharacterIndex:characterIndex];
        NSString* lineLabel = [NSString stringWithFormat:@"%lu", (unsigned long)lineNumber];
        NSDictionary* attributes = (lineNumber == currentLine) ? activeAttributes : inactiveAttributes;
        NSRect labelRect = NSMakeRect(0.0,
                                      NSMinY(lineRect) + 1.0,
                                      self.ruleThickness - 8.0,
                                      NSHeight(lineRect));

        if (NSIntersectsRect(lineRect, visibleRect)) {
            [lineLabel drawInRect:labelRect withAttributes:attributes];
        }

        glyphIndex = NSMaxRange(lineGlyphRange);
    }

    if (NSMaxRange(visibleGlyphRange) >= layoutManager.numberOfGlyphs
            && !NSIsEmptyRect(layoutManager.extraLineFragmentRect)) {
        NSString* lineLabel = [NSString stringWithFormat:@"%lu", (unsigned long)[self lineCount]];
        NSDictionary* attributes = ([self lineCount] == currentLine) ? activeAttributes : inactiveAttributes;
        NSRect labelRect = NSMakeRect(0.0,
                                      NSMinY(layoutManager.extraLineFragmentRect) + 1.0,
                                      self.ruleThickness - 8.0,
                                      NSHeight(layoutManager.extraLineFragmentRect));
        [lineLabel drawInRect:labelRect withAttributes:attributes];
    }
}

@end

@interface CroftStatusBarView : NSView
@end

@implementation CroftStatusBarView

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedWhite:0.90 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    [[NSColor colorWithCalibratedWhite:0.78 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0.0, NSHeight(self.bounds) - 1.0, NSWidth(self.bounds), 1.0));
}

@end

@interface CroftEditorController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextViewDelegate>
- (instancetype)initWithDocument:(croft_editor_document*)document
                           title:(NSString*)title
                 autoCloseMillis:(NSInteger)autoCloseMillis;
@end

@implementation CroftEditorController {
    croft_editor_document* _document;
    NSString* _windowTitle;
    NSWindow* _window;
    CroftEditorTextView* _textView;
    CroftLineNumberRulerView* _lineNumberRuler;
    NSTextField* _statusLabel;
    NSMutableArray<NSValue*>* _selectionOccurrenceRanges;
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
        _selectionOccurrenceRanges = [[NSMutableArray alloc] init];
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

    NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    [menuBar addItem:editMenuItem];

    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    NSMenuItem* undoItem = [[NSMenuItem alloc] initWithTitle:@"Undo"
                                                      action:@selector(undo:)
                                               keyEquivalent:@"z"];
    [undoItem setTarget:_textView];
    [editMenu addItem:undoItem];

    NSMenuItem* redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo"
                                                      action:@selector(redo:)
                                               keyEquivalent:@"Z"];
    [redoItem setTarget:_textView];
    [editMenu addItem:redoItem];

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* cutItem = [[NSMenuItem alloc] initWithTitle:@"Cut"
                                                     action:@selector(cut:)
                                              keyEquivalent:@"x"];
    [cutItem setTarget:_textView];
    [editMenu addItem:cutItem];

    NSMenuItem* copyItem = [[NSMenuItem alloc] initWithTitle:@"Copy"
                                                      action:@selector(copy:)
                                               keyEquivalent:@"c"];
    [copyItem setTarget:_textView];
    [editMenu addItem:copyItem];

    NSMenuItem* pasteItem = [[NSMenuItem alloc] initWithTitle:@"Paste"
                                                       action:@selector(paste:)
                                                keyEquivalent:@"v"];
    [pasteItem setTarget:_textView];
    [editMenu addItem:pasteItem];

    NSMenuItem* selectAllItem = [[NSMenuItem alloc] initWithTitle:@"Select All"
                                                           action:@selector(selectAll:)
                                                    keyEquivalent:@"a"];
    [selectAllItem setTarget:_textView];
    [editMenu addItem:selectAllItem];

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* findItem = [[NSMenuItem alloc] initWithTitle:@"Find"
                                                      action:nil
                                               keyEquivalent:@""];
    NSMenu* findMenu = [[NSMenu alloc] initWithTitle:@"Find"];
    NSMenuItem* showFindItem = [[NSMenuItem alloc] initWithTitle:@"Find..."
                                                          action:@selector(performTextFinderAction:)
                                                   keyEquivalent:@"f"];
    [showFindItem setTarget:_textView];
    [showFindItem setTag:NSTextFinderActionShowFindInterface];
    [findMenu addItem:showFindItem];

    NSMenuItem* findNextItem = [[NSMenuItem alloc] initWithTitle:@"Find Next"
                                                          action:@selector(performTextFinderAction:)
                                                   keyEquivalent:@"g"];
    [findNextItem setTarget:_textView];
    [findNextItem setTag:NSTextFinderActionNextMatch];
    [findMenu addItem:findNextItem];

    NSMenuItem* findPreviousItem = [[NSMenuItem alloc] initWithTitle:@"Find Previous"
                                                              action:@selector(performTextFinderAction:)
                                                       keyEquivalent:@"G"];
    [findPreviousItem setTarget:_textView];
    [findPreviousItem setTag:NSTextFinderActionPreviousMatch];
    [findMenu addItem:findPreviousItem];

    [findItem setSubmenu:findMenu];
    [editMenu addItem:findItem];
    [editMenuItem setSubmenu:editMenu];

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

- (croft_editor_status_snapshot)statusSnapshot {
    croft_editor_status_snapshot snapshot = {1u, 1u, 1u, 0};
    croft_editor_text_model model;
    NSString* text = _textView.string ?: @"";
    NSData* utf8 = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    NSUInteger selectionLocation = MIN(_textView.selectedRange.location, text.length);
    NSString* prefix = [text substringToIndex:selectionLocation];
    NSData* prefixUtf8 = [prefix dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];

    snapshot.is_dirty = croft_editor_document_is_dirty(_document);
    croft_editor_text_model_init(&model);

    if (utf8
            && croft_editor_text_model_set_text(&model,
                                                (const char*)utf8.bytes,
                                                (size_t)utf8.length) == CROFT_EDITOR_OK) {
        uint32_t offset =
            croft_editor_appkit_count_utf8_codepoints((const uint8_t*)prefixUtf8.bytes,
                                                      prefixUtf8 ? (size_t)prefixUtf8.length : 0u);
        croft_editor_position position = croft_editor_text_model_get_position_at(&model, offset);
        snapshot.line_number = position.line_number;
        snapshot.column = position.column;
        snapshot.line_count = croft_editor_text_model_line_count(&model);
        if (snapshot.line_count == 0u) {
            snapshot.line_count = 1u;
        }
    }

    croft_editor_text_model_dispose(&model);
    return snapshot;
}

- (void)updateStatusBar {
    char buffer[96];
    croft_editor_status_snapshot snapshot = [self statusSnapshot];

    if (!_statusLabel) {
        return;
    }

    if (croft_editor_status_format(&snapshot, buffer, sizeof(buffer)) != 0) {
        buffer[0] = '\0';
    }

    [_statusLabel setStringValue:(buffer[0] != '\0') ? [NSString stringWithUTF8String:buffer] : @""];
}

- (void)clearSelectionOccurrences {
    NSLayoutManager* layoutManager = _textView.layoutManager;

    if (!layoutManager) {
        return;
    }

    for (NSValue* value in _selectionOccurrenceRanges) {
        NSRange range = value.rangeValue;
        [layoutManager removeTemporaryAttribute:NSBackgroundColorAttributeName
                              forCharacterRange:range];
    }
    [_selectionOccurrenceRanges removeAllObjects];
}

- (void)updateSelectionOccurrences {
    NSString* text;
    NSRange selection;
    NSString* selectedText;
    NSRange searchRange;
    NSLayoutManager* layoutManager = _textView.layoutManager;

    [self clearSelectionOccurrences];

    if (!_textView || !layoutManager) {
        return;
    }

    text = _textView.string ?: @"";
    selection = _textView.selectedRange;
    if (selection.length == 0u || selection.length > 128u || NSMaxRange(selection) > text.length) {
        return;
    }

    selectedText = [text substringWithRange:selection];
    if ([selectedText rangeOfCharacterFromSet:[NSCharacterSet newlineCharacterSet]].location != NSNotFound) {
        return;
    }

    searchRange = NSMakeRange(0u, text.length);
    while (searchRange.location < text.length) {
        NSRange foundRange = [text rangeOfString:selectedText
                                         options:NSLiteralSearch
                                           range:searchRange];
        if (foundRange.location == NSNotFound) {
            break;
        }
        if (!NSEqualRanges(foundRange, selection)) {
            [layoutManager addTemporaryAttribute:NSBackgroundColorAttributeName
                                           value:[NSColor colorWithCalibratedRed:0.95
                                                                           green:0.89
                                                                            blue:0.72
                                                                           alpha:0.60]
                               forCharacterRange:foundRange];
            [_selectionOccurrenceRanges addObject:[NSValue valueWithRange:foundRange]];
        }
        if (NSMaxRange(foundRange) >= text.length) {
            break;
        }
        searchRange = NSMakeRange(NSMaxRange(foundRange), text.length - NSMaxRange(foundRange));
    }
}

- (void)refreshEditorChrome {
    [self updateWindowTitle];
    [self updateStatusBar];
    [self updateSelectionOccurrences];
    [_textView setNeedsDisplay:YES];
    [_lineNumberRuler invalidateMetrics];
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
    [self refreshEditorChrome];
}

- (void)syncTextViewToDocument {
    NSData* utf8 = [[_textView string] dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    const void* bytes = utf8 ? utf8.bytes : "";
    size_t len = utf8 ? (size_t)utf8.length : 0u;

    /*
     * This whole-buffer sync is intentionally blunt. NSTextView is still doing
     * the hard work for IME, selection affinity, undo grouping, and
     * accessibility; keeping the sync obvious helps surface those join-points
     * when we compare this family to the direct-Metal editor.
     */
    int32_t rc = croft_editor_document_replace_utf8(_document, (const uint8_t*)bytes, len);

    if (rc != 0) {
        std::printf("croft_editor_appkit: failed to sync AppKit text into Sapling (%d)\n", rc);
        return;
    }

    [self refreshEditorChrome];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    static const CGFloat kStatusBarHeight = 24.0;

    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 1000, 760)
                                          styleMask:(NSWindowStyleMaskTitled |
                                                     NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskResizable |
                                                     NSWindowStyleMaskMiniaturizable)
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    [_window setDelegate:self];

    NSView* contentView = [_window contentView];
    NSRect contentBounds = [contentView bounds];
    NSScrollView* scrollView =
        [[NSScrollView alloc] initWithFrame:NSMakeRect(0.0,
                                                       kStatusBarHeight,
                                                       contentBounds.size.width,
                                                       contentBounds.size.height - kStatusBarHeight)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:YES];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    CroftStatusBarView* statusBar =
        [[CroftStatusBarView alloc] initWithFrame:NSMakeRect(0.0,
                                                             0.0,
                                                             contentBounds.size.width,
                                                             kStatusBarHeight)];
    [statusBar setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];

    NSTextField* statusLabel =
        [[NSTextField alloc] initWithFrame:NSMakeRect(12.0,
                                                      4.0,
                                                      contentBounds.size.width - 24.0,
                                                      kStatusBarHeight - 8.0)];
    [statusLabel setAutoresizingMask:NSViewWidthSizable];
    [statusLabel setEditable:NO];
    [statusLabel setSelectable:NO];
    [statusLabel setBordered:NO];
    [statusLabel setDrawsBackground:NO];
    [statusLabel setFont:[NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular]];
    [statusLabel setTextColor:[NSColor colorWithCalibratedRed:0.17 green:0.23 blue:0.29 alpha:1.0]];
    [statusLabel setStringValue:@""];
    [statusBar addSubview:statusLabel];

    CroftEditorTextView* textView = [[CroftEditorTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
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
    [textView setTextContainerInset:NSMakeSize(12.0, 8.0)];
    [textView setDelegate:self];

    [scrollView setDocumentView:textView];
    CroftLineNumberRulerView* lineNumberRuler =
        [[CroftLineNumberRulerView alloc] initWithTextView:textView];
    [scrollView setVerticalRulerView:lineNumberRuler];
    [scrollView setHasVerticalRuler:YES];
    [scrollView setRulersVisible:YES];
    [[scrollView contentView] setPostsBoundsChangedNotifications:YES];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(scrollViewBoundsDidChange:)
                                                 name:NSViewBoundsDidChangeNotification
                                               object:[scrollView contentView]];

    [contentView addSubview:scrollView];
    [contentView addSubview:statusBar];

    _textView = textView;
    _lineNumberRuler = lineNumberRuler;
    _statusLabel = statusLabel;
    [self buildMenuBar];
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

- (void)textViewDidChangeSelection:(NSNotification*)notification {
    (void)notification;
    [self updateStatusBar];
    [self updateSelectionOccurrences];
    [_textView setNeedsDisplay:YES];
    [_lineNumberRuler setNeedsDisplay:YES];
}

- (void)scrollViewBoundsDidChange:(NSNotification*)notification {
    (void)notification;
    [_lineNumberRuler setNeedsDisplay:YES];
}

- (IBAction)saveDocument:(id)sender {
    (void)sender;
    int32_t rc = croft_editor_document_save(_document);
    if (rc != 0) {
        std::printf("croft_editor_appkit: save failed (%d)\n", rc);
        NSBeep();
        return;
    }

    [self refreshEditorChrome];
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
    [[NSNotificationCenter defaultCenter] removeObserver:self];
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
