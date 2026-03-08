#include "croft/editor_brackets.h"
#include "croft/editor_commands.h"
#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/editor_folding.h"
#include "croft/editor_status.h"
#include "croft/editor_text_model.h"
#include "croft/editor_whitespace.h"
#include "croft/host_editor_appkit.h"
#include "croft/editor_typography.h"

#import <AppKit/AppKit.h>

#include <float.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

static uint32_t croft_editor_appkit_codepoint_offset_for_utf16_index(NSString* text,
                                                                     NSUInteger utf16_index) {
    NSData* prefixUtf8;
    NSString* prefix;

    if (!text) {
        return 0u;
    }

    utf16_index = MIN(utf16_index, text.length);
    prefix = [text substringToIndex:utf16_index];
    prefixUtf8 = [prefix dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    return croft_editor_appkit_count_utf8_codepoints((const uint8_t*)prefixUtf8.bytes,
                                                     prefixUtf8 ? (size_t)prefixUtf8.length : 0u);
}

static NSUInteger croft_editor_appkit_utf16_index_for_codepoint_offset(NSString* text,
                                                                       const croft_editor_text_model* model,
                                                                       uint32_t codepoint_offset) {
    uint32_t byte_offset;
    NSString* prefix;

    if (!text || !model) {
        return 0u;
    }

    byte_offset = croft_editor_text_model_byte_offset_at(model, codepoint_offset);
    prefix = [[NSString alloc] initWithBytes:model->utf8
                                      length:(NSUInteger)byte_offset
                                    encoding:NSUTF8StringEncoding];
    if (!prefix) {
        return MIN((NSUInteger)codepoint_offset, text.length);
    }
    return MIN(prefix.length, text.length);
}

static NSString* croft_editor_appkit_string_from_utf8(const char* utf8, size_t utf8_len) {
    NSString* string;

    if (!utf8 && utf8_len > 0u) {
        return nil;
    }

    string = [[NSString alloc] initWithBytes:utf8 ? utf8 : ""
                                     length:utf8_len
                                   encoding:NSUTF8StringEncoding];
    return string;
}

static NSFont* croft_editor_appkit_monospace_font(CGFloat size) {
    NSFont* font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_REGULAR size:size];

    if (!font) {
        font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_FAMILY size:size];
    }
    if (!font) {
        font = [NSFont userFixedPitchFontOfSize:size];
    }
    if (!font) {
        font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    }
    if (!font) {
        font = [NSFont systemFontOfSize:size];
    }
    return font;
}

static NSFont* croft_editor_appkit_monospace_bold_font(CGFloat size) {
    NSFont* font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_BOLD size:size];

    if (!font) {
        font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightSemibold];
    }
    if (!font) {
        font = croft_editor_appkit_monospace_font(size);
    }
    return font;
}

static BOOL croft_editor_appkit_build_text_model(NSString* text, croft_editor_text_model* out_model) {
    NSData* utf8;
    BOOL ok;

    if (!out_model) {
        return NO;
    }

    croft_editor_text_model_init(out_model);
    text = text ?: @"";
    utf8 = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    ok = croft_editor_text_model_set_text(out_model,
                                          (const char*)utf8.bytes,
                                          utf8 ? (size_t)utf8.length : 0u) == CROFT_EDITOR_OK;
    if (!ok) {
        croft_editor_text_model_dispose(out_model);
    }
    return ok;
}

static NSRange croft_editor_appkit_fold_body_character_range(NSString* text,
                                                             const croft_editor_text_model* model,
                                                             NSRange foldedLineRange) {
    uint32_t line_count;
    uint32_t start_line_number;
    uint32_t end_line_number;
    uint32_t start_offset;
    uint32_t end_offset;
    NSUInteger start_index;
    NSUInteger end_index;

    if (!text || !model || foldedLineRange.location == NSNotFound || foldedLineRange.length < 2u) {
        return NSMakeRange(NSNotFound, 0u);
    }

    line_count = croft_editor_text_model_line_count(model);
    start_line_number = (uint32_t)foldedLineRange.location;
    end_line_number = (uint32_t)(NSMaxRange(foldedLineRange) - 1u);
    if (line_count == 0u
            || start_line_number == 0u
            || start_line_number >= end_line_number
            || end_line_number > line_count) {
        return NSMakeRange(NSNotFound, 0u);
    }

    start_offset = croft_editor_text_model_line_start_offset(model, start_line_number + 1u);
    end_offset = end_line_number < line_count
        ? croft_editor_text_model_line_start_offset(model, end_line_number + 1u)
        : croft_editor_text_model_codepoint_length(model);
    start_index = croft_editor_appkit_utf16_index_for_codepoint_offset(text, model, start_offset);
    end_index = croft_editor_appkit_utf16_index_for_codepoint_offset(text, model, end_offset);
    if (end_index < start_index) {
        end_index = start_index;
    }

    return NSMakeRange(start_index, end_index - start_index);
}

static NSRect croft_editor_appkit_marker_rect(NSLayoutManager* layoutManager,
                                              NSTextContainer* textContainer,
                                              NSRange characterRange) {
    NSRange glyphRange;

    if (!layoutManager || !textContainer || characterRange.length == 0u) {
        return NSZeroRect;
    }

    glyphRange = [layoutManager glyphRangeForCharacterRange:characterRange actualCharacterRange:NULL];
    if (glyphRange.location == NSNotFound || glyphRange.length == 0u) {
        return NSZeroRect;
    }
    return [layoutManager boundingRectForGlyphRange:glyphRange inTextContainer:textContainer];
}

static void croft_editor_appkit_draw_whitespace_marker(NSRect markerRect,
                                                       croft_editor_visible_whitespace_kind kind) {
    NSColor* color = [NSColor colorWithCalibratedWhite:0.63 alpha:0.78];
    CGFloat midY;

    if (NSIsEmptyRect(markerRect) || NSWidth(markerRect) <= 0.0) {
        return;
    }

    midY = NSMidY(markerRect);
    [color setFill];
    [color setStroke];
    if (kind == CROFT_EDITOR_VISIBLE_WHITESPACE_SPACE) {
        NSRectFill(NSMakeRect(NSMidX(markerRect) - 1.0, midY - 1.0, 2.0, 2.0));
        return;
    }

    {
        NSBezierPath* path = [NSBezierPath bezierPath];
        CGFloat startX = NSMinX(markerRect) + 2.0;
        CGFloat endX = NSMaxX(markerRect) - 3.0;

        if (endX <= startX) {
            startX = NSMinX(markerRect) + 1.0;
            endX = NSMaxX(markerRect) - 1.0;
        }
        if (endX <= startX) {
            return;
        }

        [path setLineWidth:1.0];
        [path moveToPoint:NSMakePoint(startX, midY)];
        [path lineToPoint:NSMakePoint(endX, midY)];
        [path moveToPoint:NSMakePoint(startX, midY - 2.0)];
        [path lineToPoint:NSMakePoint(startX, midY + 2.0)];
        [path moveToPoint:NSMakePoint(endX - 3.0, midY - 2.0)];
        [path lineToPoint:NSMakePoint(endX, midY)];
        [path lineToPoint:NSMakePoint(endX - 3.0, midY + 2.0)];
        [path stroke];
    }
}

@protocol CroftEditorIndentHandling <NSObject>
- (BOOL)croftIndentSelection;
- (BOOL)croftOutdentSelection;
@end

@protocol CroftEditorFoldingHandling <NSObject>
- (BOOL)croftLineNumberIsFolded:(NSUInteger)lineNumber;
- (BOOL)croftToggleFoldAtLineNumber:(NSUInteger)lineNumber;
@end

@interface CroftEditorTextView : NSTextView
@property(nonatomic, weak) id<CroftEditorIndentHandling> croftIndentHandler;
- (NSUInteger)croft_lineNumberForCharacterIndex:(NSUInteger)characterIndex;
- (NSUInteger)croft_currentLineNumber;
@end

@implementation CroftEditorTextView

- (void)croft_drawWhitespaceInRect:(NSRect)rect {
    NSLayoutManager* layoutManager = self.layoutManager;
    NSTextContainer* textContainer = self.textContainer;
    NSString* text = self.string ?: @"";
    NSClipView* clipView = self.enclosingScrollView.contentView;
    NSRect visibleRect;
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    NSRange visibleGlyphRange;
    NSUInteger glyphIndex;

    (void)rect;

    if (!layoutManager || !textContainer || !clipView) {
        return;
    }

    if (!croft_editor_appkit_build_text_model(text, &model)) {
        return;
    }

    croft_editor_tab_settings_default(&settings);
    visibleRect = clipView.documentVisibleRect;
    visibleGlyphRange =
        [layoutManager glyphRangeForBoundingRect:visibleRect inTextContainer:textContainer];
    glyphIndex = visibleGlyphRange.location;
    while (glyphIndex < NSMaxRange(visibleGlyphRange) && glyphIndex < layoutManager.numberOfGlyphs) {
        NSRange lineGlyphRange;
        NSRect lineRect = [layoutManager lineFragmentRectForGlyphAtIndex:glyphIndex
                                                          effectiveRange:&lineGlyphRange];
        NSUInteger characterIndex = [layoutManager characterIndexForGlyphAtIndex:glyphIndex];
        uint32_t codepointOffset =
            croft_editor_appkit_codepoint_offset_for_utf16_index(text, characterIndex);
        uint32_t lineNumber = croft_editor_text_model_get_position_at(&model, codepointOffset).line_number;
        croft_editor_whitespace_line line = {0};
        croft_editor_visible_whitespace marker = {0};
        uint32_t searchOffset;

        if (croft_editor_whitespace_describe_line(&model, lineNumber, &settings, &line)
                != CROFT_EDITOR_OK) {
            glyphIndex = NSMaxRange(lineGlyphRange);
            continue;
        }

        searchOffset = line.line_start_offset;
        while (croft_editor_whitespace_find_in_line(&model,
                                                    lineNumber,
                                                    &settings,
                                                    searchOffset,
                                                    &marker) == CROFT_EDITOR_OK) {
            NSUInteger start;
            NSUInteger end;
            NSRect markerRect;
            uint32_t visualEnd = marker.visual_column + marker.visual_width - 1u;

            if (marker.offset >= line.line_end_offset) {
                break;
            }

            start = croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, marker.offset);
            end = croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, marker.offset + 1u);
            markerRect = croft_editor_appkit_marker_rect(layoutManager,
                                                         textContainer,
                                                         NSMakeRange(start, end - start));
            if (!NSIntersectsRect(markerRect, visibleRect)) {
                searchOffset = marker.offset + 1u;
                continue;
            }

            if (visualEnd <= line.leading_indent_columns && (visualEnd % settings.tab_size) == 0u) {
                CGFloat unitWidth = NSWidth(markerRect) / (CGFloat)marker.visual_width;
                CGFloat guideX = NSMaxX(markerRect) - (unitWidth * 0.5);
                [[NSColor colorWithCalibratedWhite:0.82 alpha:0.85] setFill];
                NSRectFill(NSMakeRect(guideX,
                                      NSMinY(lineRect) + 1.0,
                                      1.0,
                                      NSHeight(lineRect) - 2.0));
            }

            croft_editor_appkit_draw_whitespace_marker(markerRect, marker.kind);
            searchOffset = marker.offset + 1u;
        }

        glyphIndex = NSMaxRange(lineGlyphRange);
    }

    croft_editor_text_model_dispose(&model);
}

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
    return [self croft_lineNumberForCharacterIndex:MIN(self.selectedRange.location, text.length)];
}

- (NSUInteger)croft_lineNumberForCharacterIndex:(NSUInteger)characterIndex {
    NSString* text = self.string ?: @"";
    NSUInteger limit = MIN(characterIndex, text.length);
    NSUInteger lineNumber = 1;

    for (NSUInteger index = 0; index < limit; index++) {
        if ([text characterAtIndex:index] == '\n') {
            lineNumber++;
        }
    }

    return lineNumber;
}

- (void)insertTab:(id)sender {
    if (self.croftIndentHandler && [self.croftIndentHandler croftIndentSelection]) {
        return;
    }
    [super insertTab:sender];
}

- (void)insertBacktab:(id)sender {
    if (self.croftIndentHandler && [self.croftIndentHandler croftOutdentSelection]) {
        return;
    }
    [super insertBacktab:sender];
}

- (void)drawViewBackgroundInRect:(NSRect)rect {
    [super drawViewBackgroundInRect:rect];

    NSRect lineRect = [self croft_currentLineRect];
    NSRect visibleRect = NSIntersectionRect(rect, lineRect);
    if (!NSIsEmptyRect(visibleRect)) {
        [[NSColor colorWithCalibratedRed:0.86 green:0.91 blue:0.97 alpha:1.0] setFill];
        NSRectFill(visibleRect);
    }

    [self croft_drawWhitespaceInRect:rect];
}

@end

@interface CroftLineNumberRulerView : NSRulerView
@property(nonatomic, weak) id<CroftEditorFoldingHandling> croftFoldingHandler;
- (instancetype)initWithTextView:(CroftEditorTextView*)textView
                  foldingHandler:(id<CroftEditorFoldingHandling>)foldingHandler;
- (void)invalidateMetrics;
@end

@implementation CroftLineNumberRulerView {
    __weak CroftEditorTextView* _textView;
}

- (instancetype)initWithTextView:(CroftEditorTextView*)textView
                  foldingHandler:(id<CroftEditorFoldingHandling>)foldingHandler {
    self = [super initWithScrollView:textView.enclosingScrollView orientation:NSVerticalRuler];
    if (self) {
        _textView = textView;
        _croftFoldingHandler = foldingHandler;
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
    return [_textView croft_lineNumberForCharacterIndex:characterIndex];
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
        NSFontAttributeName: croft_editor_appkit_monospace_font(12.0)
    };
    sampleSize = [sample sizeWithAttributes:attributes];
    return ceil(sampleSize.width + 34.0);
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
    NSString* text = _textView.string ?: @"";
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    BOOL haveModel = NO;
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    NSMutableParagraphStyle* markerStyle = [[NSMutableParagraphStyle alloc] init];
    NSDictionary* inactiveAttributes;
    NSDictionary* activeAttributes;
    NSDictionary* markerAttributes;
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
    markerStyle.alignment = NSTextAlignmentCenter;
    inactiveAttributes = @{
        NSFontAttributeName: croft_editor_appkit_monospace_font(12.0),
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.40 alpha:1.0],
        NSParagraphStyleAttributeName: style
    };
    activeAttributes = @{
        NSFontAttributeName: croft_editor_appkit_monospace_bold_font(12.0),
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.10 green:0.22 blue:0.38 alpha:1.0],
        NSParagraphStyleAttributeName: style
    };
    markerAttributes = @{
        NSFontAttributeName: croft_editor_appkit_monospace_bold_font(11.0),
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.45 alpha:1.0],
        NSParagraphStyleAttributeName: markerStyle
    };

    haveModel = croft_editor_appkit_build_text_model(text, &model);
    if (haveModel) {
        croft_editor_tab_settings_default(&settings);
    }

    NSRange visibleGlyphRange =
        [layoutManager glyphRangeForBoundingRect:visibleRect inTextContainer:textContainer];
    NSUInteger glyphIndex = visibleGlyphRange.location;
    while (glyphIndex < NSMaxRange(visibleGlyphRange) && glyphIndex < layoutManager.numberOfGlyphs) {
        NSRange lineGlyphRange;
        NSRect lineRect = [layoutManager lineFragmentRectForGlyphAtIndex:glyphIndex
                                                          effectiveRange:&lineGlyphRange];
        NSUInteger characterIndex = [layoutManager characterIndexForGlyphAtIndex:glyphIndex];
        NSUInteger lineNumber = [self lineNumberForCharacterIndex:characterIndex];
        BOOL isFolded =
            self.croftFoldingHandler ? [self.croftFoldingHandler croftLineNumberIsFolded:lineNumber] : NO;
        BOOL isFoldable = isFolded;
        NSString* lineLabel = [NSString stringWithFormat:@"%lu", (unsigned long)lineNumber];
        NSDictionary* attributes = (lineNumber == currentLine) ? activeAttributes : inactiveAttributes;
        NSRect markerRect = NSMakeRect(2.0,
                                       NSMinY(lineRect) + 1.0,
                                       16.0,
                                       NSHeight(lineRect));
        NSRect labelRect = NSMakeRect(18.0,
                                      NSMinY(lineRect) + 1.0,
                                      self.ruleThickness - 24.0,
                                      NSHeight(lineRect));

        if (!isFoldable && haveModel) {
            croft_editor_fold_region region = {0};
            isFoldable = croft_editor_fold_region_for_line(&model,
                                                           (uint32_t)lineNumber,
                                                           &settings,
                                                           &region) == CROFT_EDITOR_OK;
        }

        if (NSIntersectsRect(lineRect, visibleRect)) {
            if (isFoldable) {
                [(isFolded ? @">" : @"v") drawInRect:markerRect withAttributes:markerAttributes];
            }
            [lineLabel drawInRect:labelRect withAttributes:attributes];
        }

        glyphIndex = NSMaxRange(lineGlyphRange);
    }

    if (NSMaxRange(visibleGlyphRange) >= layoutManager.numberOfGlyphs
            && !NSIsEmptyRect(layoutManager.extraLineFragmentRect)) {
        NSString* lineLabel = [NSString stringWithFormat:@"%lu", (unsigned long)[self lineCount]];
        NSDictionary* attributes = ([self lineCount] == currentLine) ? activeAttributes : inactiveAttributes;
        NSRect labelRect = NSMakeRect(18.0,
                                      NSMinY(layoutManager.extraLineFragmentRect) + 1.0,
                                      self.ruleThickness - 24.0,
                                      NSHeight(layoutManager.extraLineFragmentRect));
        [lineLabel drawInRect:labelRect withAttributes:attributes];
    }

    if (haveModel) {
        croft_editor_text_model_dispose(&model);
    }
}

- (void)mouseDown:(NSEvent*)event {
    NSLayoutManager* layoutManager = _textView.layoutManager;
    NSTextContainer* textContainer = _textView.textContainer;
    NSClipView* clipView = self.scrollView.contentView;
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect visibleRect;

    if (!self.croftFoldingHandler
            || point.x > 18.0
            || !layoutManager
            || !textContainer
            || !clipView) {
        [super mouseDown:event];
        return;
    }

    visibleRect = clipView.documentVisibleRect;
    {
        NSRange visibleGlyphRange =
            [layoutManager glyphRangeForBoundingRect:visibleRect inTextContainer:textContainer];
        NSUInteger glyphIndex = visibleGlyphRange.location;

        while (glyphIndex < NSMaxRange(visibleGlyphRange) && glyphIndex < layoutManager.numberOfGlyphs) {
            NSRange lineGlyphRange;
            NSRect lineRect = [layoutManager lineFragmentRectForGlyphAtIndex:glyphIndex
                                                              effectiveRange:&lineGlyphRange];
            NSUInteger characterIndex = [layoutManager characterIndexForGlyphAtIndex:glyphIndex];
            NSUInteger lineNumber = [self lineNumberForCharacterIndex:characterIndex];

            if (point.y >= NSMinY(lineRect) && point.y <= NSMaxY(lineRect)) {
                if ([self.croftFoldingHandler croftToggleFoldAtLineNumber:lineNumber]) {
                    return;
                }
                break;
            }

            glyphIndex = NSMaxRange(lineGlyphRange);
        }
    }

    if (!NSIsEmptyRect(layoutManager.extraLineFragmentRect)
            && point.y >= NSMinY(layoutManager.extraLineFragmentRect)
            && point.y <= NSMaxY(layoutManager.extraLineFragmentRect)
            && [self.croftFoldingHandler croftToggleFoldAtLineNumber:[self lineCount]]) {
        return;
    }

    [super mouseDown:event];
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

@interface CroftEditorController
    : NSObject <NSApplicationDelegate,
                NSWindowDelegate,
                NSTextViewDelegate,
                NSLayoutManagerDelegate,
                CroftEditorIndentHandling,
                CroftEditorFoldingHandling>
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
    NSMutableArray<NSValue*>* _bracketMatchRanges;
    NSMutableArray<NSValue*>* _foldedLineRanges;
    NSMutableArray<NSValue*>* _foldedBodyCharacterRanges;
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
        _bracketMatchRanges = [[NSMutableArray alloc] init];
        _foldedLineRanges = [[NSMutableArray alloc] init];
        _foldedBodyCharacterRanges = [[NSMutableArray alloc] init];
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

    NSMenuItem* indentItem = [[NSMenuItem alloc] initWithTitle:@"Indent Line"
                                                        action:@selector(indentSelection:)
                                                 keyEquivalent:@"]"];
    [indentItem setTarget:self];
    [editMenu addItem:indentItem];

    NSMenuItem* outdentItem = [[NSMenuItem alloc] initWithTitle:@"Outdent Line"
                                                         action:@selector(outdentSelection:)
                                                  keyEquivalent:@"["];
    [outdentItem setTarget:self];
    [editMenu addItem:outdentItem];

    [editMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* foldItem = [[NSMenuItem alloc] initWithTitle:@"Fold Region"
                                                      action:@selector(foldSelection:)
                                               keyEquivalent:@"["];
    [foldItem setTarget:self];
    [foldItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [editMenu addItem:foldItem];

    NSMenuItem* unfoldItem = [[NSMenuItem alloc] initWithTitle:@"Unfold Region"
                                                        action:@selector(unfoldSelection:)
                                                 keyEquivalent:@"]"];
    [unfoldItem setTarget:self];
    [unfoldItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [editMenu addItem:unfoldItem];

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

- (void)clearBracketMatches {
    NSLayoutManager* layoutManager = _textView.layoutManager;

    if (!layoutManager) {
        return;
    }

    for (NSValue* value in _bracketMatchRanges) {
        NSRange range = value.rangeValue;
        [layoutManager removeTemporaryAttribute:NSBackgroundColorAttributeName
                              forCharacterRange:range];
    }
    [_bracketMatchRanges removeAllObjects];
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

- (void)updateBracketMatches {
    NSString* text;
    NSRange selection;
    NSData* utf8;
    NSLayoutManager* layoutManager = _textView.layoutManager;
    croft_editor_text_model model;
    croft_editor_bracket_match match = {0};

    [self clearBracketMatches];

    if (!_textView || !layoutManager) {
        return;
    }

    text = _textView.string ?: @"";
    selection = _textView.selectedRange;
    if (selection.length != 0u || selection.location > text.length) {
        return;
    }

    utf8 = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    croft_editor_text_model_init(&model);
    if (croft_editor_text_model_set_text(&model,
                                         (const char*)utf8.bytes,
                                         utf8 ? (size_t)utf8.length : 0u) == CROFT_EDITOR_OK) {
        uint32_t cursor_offset =
            croft_editor_appkit_codepoint_offset_for_utf16_index(text, selection.location);
        if (croft_editor_bracket_match_near_offset(&model, cursor_offset, &match) == CROFT_EDITOR_OK) {
            NSUInteger open_start =
                croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, match.open_offset);
            NSUInteger open_end =
                croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, match.open_offset + 1u);
            NSUInteger close_start =
                croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, match.close_offset);
            NSUInteger close_end =
                croft_editor_appkit_utf16_index_for_codepoint_offset(text, &model, match.close_offset + 1u);
            NSColor* color =
                [NSColor colorWithCalibratedRed:0.80 green:0.88 blue:0.97 alpha:0.85];

            [layoutManager addTemporaryAttribute:NSBackgroundColorAttributeName
                                           value:color
                               forCharacterRange:NSMakeRange(open_start, open_end - open_start)];
            [_bracketMatchRanges addObject:[NSValue valueWithRange:NSMakeRange(open_start,
                                                                               open_end - open_start)]];
            [layoutManager addTemporaryAttribute:NSBackgroundColorAttributeName
                                           value:color
                               forCharacterRange:NSMakeRange(close_start, close_end - close_start)];
            [_bracketMatchRanges addObject:[NSValue valueWithRange:NSMakeRange(close_start,
                                                                               close_end - close_start)]];
        }
    }
    croft_editor_text_model_dispose(&model);
}

- (void)refreshEditorChrome {
    [self updateWindowTitle];
    [self updateStatusBar];
    [self updateBracketMatches];
    [self updateSelectionOccurrences];
    [_textView setNeedsDisplay:YES];
    [_lineNumberRuler invalidateMetrics];
}

- (void)clearFolds {
    [_foldedLineRanges removeAllObjects];
    [_foldedBodyCharacterRanges removeAllObjects];
}

- (void)rebuildFoldedBodyCharacterRanges {
    NSString* text = _textView.string ?: @"";
    croft_editor_text_model model;

    [_foldedBodyCharacterRanges removeAllObjects];
    if (_foldedLineRanges.count == 0u || !_textView) {
        return;
    }

    if (!croft_editor_appkit_build_text_model(text, &model)) {
        for (NSUInteger index = 0u; index < _foldedLineRanges.count; index++) {
            [_foldedBodyCharacterRanges addObject:[NSValue valueWithRange:NSMakeRange(NSNotFound, 0u)]];
        }
        return;
    }

    for (NSValue* value in _foldedLineRanges) {
        NSRange bodyRange =
            croft_editor_appkit_fold_body_character_range(text, &model, value.rangeValue);
        [_foldedBodyCharacterRanges addObject:[NSValue valueWithRange:bodyRange]];
    }

    croft_editor_text_model_dispose(&model);
}

- (void)invalidateFolding {
    NSLayoutManager* layoutManager = _textView.layoutManager;
    NSString* text = _textView.string ?: @"";

    [self rebuildFoldedBodyCharacterRanges];
    if (layoutManager) {
        NSRange fullRange = NSMakeRange(0u, text.length);

        [layoutManager invalidateGlyphsForCharacterRange:fullRange
                                          changeInLength:0
                                    actualCharacterRange:NULL];
        [layoutManager invalidateLayoutForCharacterRange:fullRange actualCharacterRange:NULL];
        [layoutManager invalidateDisplayForCharacterRange:fullRange];
    }

    [_textView setNeedsDisplay:YES];
    [_lineNumberRuler invalidateMetrics];
}

- (BOOL)lineIsFoldedHeader:(NSUInteger)lineNumber index:(NSUInteger*)indexOut {
    for (NSUInteger index = 0u; index < _foldedLineRanges.count; index++) {
        if (_foldedLineRanges[index].rangeValue.location == lineNumber) {
            if (indexOut) {
                *indexOut = index;
            }
            return YES;
        }
    }
    return NO;
}

- (BOOL)lineIsInFoldedBody:(NSUInteger)lineNumber index:(NSUInteger*)indexOut {
    for (NSUInteger index = 0u; index < _foldedLineRanges.count; index++) {
        NSRange lineRange = _foldedLineRanges[index].rangeValue;

        if (lineNumber > lineRange.location && lineNumber < NSMaxRange(lineRange)) {
            if (indexOut) {
                *indexOut = index;
            }
            return YES;
        }
    }
    return NO;
}

- (BOOL)revealSelectionIfFolded {
    NSRange selection = _textView.selectedRange;
    BOOL changed = NO;

    while (_foldedBodyCharacterRanges.count > 0u) {
        BOOL removed = NO;

        for (NSUInteger index = 0u; index < _foldedBodyCharacterRanges.count; index++) {
            NSRange bodyRange = _foldedBodyCharacterRanges[index].rangeValue;
            BOOL intersects = NO;

            if (bodyRange.location == NSNotFound || bodyRange.length == 0u) {
                continue;
            }

            if (selection.length == 0u) {
                intersects =
                    selection.location >= bodyRange.location
                    && selection.location < NSMaxRange(bodyRange);
            } else {
                intersects = NSIntersectionRange(selection, bodyRange).length > 0u;
            }

            if (!intersects) {
                continue;
            }

            [_foldedLineRanges removeObjectAtIndex:index];
            [_foldedBodyCharacterRanges removeObjectAtIndex:index];
            changed = YES;
            removed = YES;
            break;
        }

        if (!removed) {
            break;
        }
    }

    if (changed) {
        [self invalidateFolding];
    }
    return changed;
}

- (BOOL)croftLineNumberIsFolded:(NSUInteger)lineNumber {
    return [self lineIsFoldedHeader:lineNumber index:NULL];
}

- (BOOL)foldLineNumber:(NSUInteger)lineNumber {
    if (lineNumber == 0u || !_textView) {
        return NO;
    }

    if ([self lineIsFoldedHeader:lineNumber index:NULL]
            || [self lineIsInFoldedBody:lineNumber index:NULL]) {
        return NO;
    }

    {
        NSString* text = _textView.string ?: @"";
        croft_editor_text_model model;
        croft_editor_tab_settings settings;
        croft_editor_fold_region region = {0};

        if (!croft_editor_appkit_build_text_model(text, &model)) {
            return NO;
        }

        croft_editor_tab_settings_default(&settings);
        if (croft_editor_fold_region_for_line(&model,
                                              (uint32_t)lineNumber,
                                              &settings,
                                              &region) != CROFT_EDITOR_OK) {
            croft_editor_text_model_dispose(&model);
            return NO;
        }
        croft_editor_text_model_dispose(&model);

        {
            NSRange foldedLineRange = NSMakeRange(region.start_line_number,
                                                  region.end_line_number - region.start_line_number + 1u);
            NSUInteger insertIndex = _foldedLineRanges.count;

            while (insertIndex > 0u
                    && _foldedLineRanges[insertIndex - 1u].rangeValue.location > foldedLineRange.location) {
                insertIndex--;
            }
            [_foldedLineRanges insertObject:[NSValue valueWithRange:foldedLineRange] atIndex:insertIndex];
        }
    }

    [self invalidateFolding];
    return YES;
}

- (BOOL)croftToggleFoldAtLineNumber:(NSUInteger)lineNumber {
    NSUInteger existingIndex;

    if (lineNumber == 0u || !_textView) {
        return NO;
    }

    if ([self lineIsFoldedHeader:lineNumber index:&existingIndex]
            || [self lineIsInFoldedBody:lineNumber index:&existingIndex]) {
        [_foldedLineRanges removeObjectAtIndex:existingIndex];
        [self invalidateFolding];
        return YES;
    }

    return [self foldLineNumber:lineNumber];
}

- (BOOL)foldAtCurrentLine {
    [self revealSelectionIfFolded];
    return [self foldLineNumber:[_textView croft_currentLineNumber]];
}

- (BOOL)unfoldAtCurrentLine {
    NSUInteger currentLine = [_textView croft_currentLineNumber];
    NSUInteger existingIndex;

    if ([self revealSelectionIfFolded]) {
        return YES;
    }

    if (![self lineIsFoldedHeader:currentLine index:&existingIndex]
            && ![self lineIsInFoldedBody:currentLine index:&existingIndex]) {
        return NO;
    }

    [_foldedLineRanges removeObjectAtIndex:existingIndex];
    [self invalidateFolding];
    return YES;
}

- (BOOL)applyIndentAction:(BOOL)outdent {
    NSString* text = _textView.string ?: @"";
    NSData* utf8 = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    croft_editor_text_model model;
    croft_editor_tab_settings settings;
    croft_editor_tab_edit edit = {0};
    NSRange selection = _textView.selectedRange;
    uint32_t anchor_offset;
    uint32_t active_offset;
    BOOL handled = NO;

    croft_editor_text_model_init(&model);
    croft_editor_tab_settings_default(&settings);

    selection.location = MIN(selection.location, text.length);
    selection.length = MIN(selection.length, text.length - selection.location);
    if (croft_editor_text_model_set_text(&model,
                                         (const char*)utf8.bytes,
                                         utf8 ? (size_t)utf8.length : 0u) != CROFT_EDITOR_OK) {
        goto cleanup;
    }

    anchor_offset =
        croft_editor_appkit_codepoint_offset_for_utf16_index(text, selection.location);
    active_offset =
        croft_editor_appkit_codepoint_offset_for_utf16_index(text, NSMaxRange(selection));
    if (!croft_editor_command_build_tab_edit(&model,
                                             anchor_offset,
                                             active_offset,
                                             &settings,
                                             outdent ? 1 : 0,
                                             &edit)) {
        handled = YES;
        goto cleanup;
    }

    {
        NSUInteger replace_start =
            croft_editor_appkit_utf16_index_for_codepoint_offset(text,
                                                                 &model,
                                                                 edit.replace_start_offset);
        NSUInteger replace_end =
            croft_editor_appkit_utf16_index_for_codepoint_offset(text,
                                                                 &model,
                                                                 edit.replace_end_offset);
        NSRange replace_range = NSMakeRange(replace_start, replace_end - replace_start);
        NSString* replacement =
            croft_editor_appkit_string_from_utf8(edit.replacement_utf8, edit.replacement_utf8_len);

        if (!replacement) {
            goto cleanup;
        }
        if (![_textView shouldChangeTextInRange:replace_range replacementString:replacement]) {
            goto cleanup;
        }

        [[_textView textStorage] replaceCharactersInRange:replace_range withString:replacement];
        [_textView didChangeText];

        {
            NSString* updated_text = _textView.string ?: @"";
            NSData* updated_utf8 =
                [updated_text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
            croft_editor_text_model updated_model;
            uint32_t selection_start_offset =
                edit.next_anchor_offset < edit.next_active_offset
                    ? edit.next_anchor_offset
                    : edit.next_active_offset;
            uint32_t selection_end_offset =
                edit.next_anchor_offset < edit.next_active_offset
                    ? edit.next_active_offset
                    : edit.next_anchor_offset;

            croft_editor_text_model_init(&updated_model);
            if (croft_editor_text_model_set_text(&updated_model,
                                                 (const char*)updated_utf8.bytes,
                                                 updated_utf8 ? (size_t)updated_utf8.length : 0u)
                    == CROFT_EDITOR_OK) {
                NSUInteger new_start =
                    croft_editor_appkit_utf16_index_for_codepoint_offset(updated_text,
                                                                         &updated_model,
                                                                         selection_start_offset);
                NSUInteger new_end =
                    croft_editor_appkit_utf16_index_for_codepoint_offset(updated_text,
                                                                         &updated_model,
                                                                         selection_end_offset);
                [_textView setSelectedRange:NSMakeRange(new_start, new_end - new_start)];
            }
            croft_editor_text_model_dispose(&updated_model);
        }
    }

    handled = YES;

cleanup:
    croft_editor_tab_edit_dispose(&edit);
    croft_editor_text_model_dispose(&model);
    return handled;
}

- (BOOL)croftIndentSelection {
    return [self applyIndentAction:NO];
}

- (BOOL)croftOutdentSelection {
    return [self applyIndentAction:YES];
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
    [self clearFolds];
    [_textView setString:text];
    _syncingFromDocument = NO;
    [self invalidateFolding];
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
    [statusLabel setFont:croft_editor_appkit_monospace_font(12.0)];
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
    [textView setFont:croft_editor_appkit_monospace_font(15.0)];
    [textView setTextContainerInset:NSMakeSize(12.0, 8.0)];
    [textView setCroftIndentHandler:self];
    [textView setDelegate:self];
    [[textView layoutManager] setDelegate:self];

    [scrollView setDocumentView:textView];
    CroftLineNumberRulerView* lineNumberRuler =
        [[CroftLineNumberRulerView alloc] initWithTextView:textView foldingHandler:self];
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
    if (_foldedLineRanges.count > 0u) {
        [self clearFolds];
        [self invalidateFolding];
    }
    [self syncTextViewToDocument];
}

- (void)textViewDidChangeSelection:(NSNotification*)notification {
    (void)notification;
    [self revealSelectionIfFolded];
    [self updateStatusBar];
    [self updateBracketMatches];
    [self updateSelectionOccurrences];
    [_textView setNeedsDisplay:YES];
    [_lineNumberRuler setNeedsDisplay:YES];
}

- (void)scrollViewBoundsDidChange:(NSNotification*)notification {
    (void)notification;
    [_lineNumberRuler setNeedsDisplay:YES];
}

- (IBAction)indentSelection:(id)sender {
    (void)sender;
    [self croftIndentSelection];
}

- (IBAction)outdentSelection:(id)sender {
    (void)sender;
    [self croftOutdentSelection];
}

- (IBAction)foldSelection:(id)sender {
    (void)sender;
    [self foldAtCurrentLine];
}

- (IBAction)unfoldSelection:(id)sender {
    (void)sender;
    [self unfoldAtCurrentLine];
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

- (NSUInteger)layoutManager:(NSLayoutManager*)layoutManager
       shouldGenerateGlyphs:(const CGGlyph*)glyphs
                 properties:(const NSGlyphProperty*)props
           characterIndexes:(const NSUInteger*)charIndexes
                       font:(NSFont*)aFont
                forGlyphRange:(NSRange)glyphRange {
    std::vector<NSGlyphProperty> properties;
    bool changed = false;

    if (_foldedBodyCharacterRanges.count == 0u
            || glyphRange.length == 0u
            || !glyphs
            || !props
            || !charIndexes) {
        return 0u;
    }

    properties.assign(props, props + glyphRange.length);
    for (NSUInteger index = 0u; index < glyphRange.length; index++) {
        NSUInteger characterIndex = charIndexes[index];

        for (NSValue* value in _foldedBodyCharacterRanges) {
            NSRange range = value.rangeValue;

            if (range.location == NSNotFound || range.length == 0u) {
                continue;
            }
            if (characterIndex >= range.location && characterIndex < NSMaxRange(range)) {
                properties[index] = (NSGlyphProperty)(properties[index] | NSGlyphPropertyNull);
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return 0u;
    }

    [layoutManager setGlyphs:glyphs
                  properties:properties.data()
            characterIndexes:charIndexes
                        font:aFont
               forGlyphRange:glyphRange];
    return glyphRange.length;
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
