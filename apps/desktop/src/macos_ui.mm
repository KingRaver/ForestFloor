#if defined(__APPLE__)

#import "macos_ui.hpp"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "runtime.hpp"

// ---------------------------------------------------------------------------
// Custom view classes for proper event handling
// ---------------------------------------------------------------------------

// Labels that don't consume mouse events.
@interface FFLabel : NSTextField
@end

@implementation FFLabel
- (NSView*)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}
@end

// Panel view that resets first responder on click (prevents focus getting stuck).
@interface FFPanelView : NSView
@end

@implementation FFPanelView
- (void)mouseDown:(NSEvent*)event {
  (void)event;
  [[self window] makeFirstResponder:nil];
}
@end

namespace {

using ff::desktop::MidiLearnSlot;
using ff::desktop::Runtime;
using ff::desktop::RuntimeConfig;

static constexpr std::size_t kTrackCount = Runtime::kTrackCount;
static constexpr std::size_t kSteps = Runtime::kSteps;

// ---------------------------------------------------------------------------
// Theme Colors
// ---------------------------------------------------------------------------

static NSColor* kColorBackground = nil;
static NSColor* kColorPanel = nil;
static NSColor* kColorPanelBorder = nil;
static NSColor* kColorTextPrimary = nil;
static NSColor* kColorTextSecondary = nil;
static NSColor* kColorTextDim = nil;
static NSColor* kColorAccent = nil;
static NSColor* kColorGridActive = nil;
static NSColor* kColorGridInactive = nil;
static NSColor* kColorPlayhead = nil;
static NSColor* kColorPlayheadGlow = nil;
static NSColor* kColorButtonBg = nil;
static NSColor* kColorButtonText = nil;
static NSColor* kColorPlayActive = nil;
static NSColor* kColorPadBg = nil;

static void InitTheme() {
  kColorBackground   = [NSColor colorWithRed:0.000 green:0.125 blue:0.761 alpha:1.0];
  kColorPanel        = [NSColor colorWithRed:0.082 green:0.106 blue:0.329 alpha:1.0];
  kColorPanelBorder  = [NSColor colorWithRed:0.839 green:0.878 blue:0.910 alpha:1.0];
  kColorTextPrimary  = [NSColor colorWithRed:0.910 green:0.918 blue:0.930 alpha:1.0];
  kColorTextSecondary = [NSColor colorWithRed:0.824 green:0.992 blue:1.000 alpha:1.0];
  kColorTextDim      = [NSColor colorWithRed:0.851 green:0.992 blue:1.000 alpha:1.0];
  kColorAccent       = [NSColor colorWithRed:0.290 green:0.871 blue:0.502 alpha:1.0];
  kColorGridActive   = [NSColor colorWithRed:0.176 green:0.545 blue:0.322 alpha:1.0];
  kColorGridInactive = [NSColor colorWithRed:0.157 green:0.173 blue:0.192 alpha:1.0];
  kColorPlayhead     = [NSColor colorWithRed:0.961 green:0.620 blue:0.043 alpha:1.0];
  kColorPlayheadGlow = [NSColor colorWithRed:0.961 green:0.620 blue:0.043 alpha:0.20];
  kColorButtonBg     = [NSColor colorWithRed:0.165 green:0.180 blue:0.200 alpha:1.0];
  kColorButtonText   = [NSColor colorWithRed:0.784 green:0.800 blue:0.816 alpha:1.0];
  kColorPlayActive   = [NSColor colorWithRed:0.937 green:0.267 blue:0.267 alpha:1.0];
  kColorPadBg        = [NSColor colorWithRed:0.914 green:0.702 blue:1.000 alpha:1.0];
}

// ---------------------------------------------------------------------------
// Layout Constants
// ---------------------------------------------------------------------------

static constexpr CGFloat kPanelCornerRadius = 10.0;
static constexpr CGFloat kPanelBorderWidth = 1.0;
static constexpr CGFloat kButtonCornerRadius = 6.0;
static constexpr CGFloat kStepCellCornerRadius = 4.0;
static constexpr CGFloat kSectionSpacing = 10.0;
static constexpr CGFloat kMarginH = 16.0;
static constexpr CGFloat kContentWidth = 1240.0 - 2 * kMarginH;  // 1208

// Step grid
static constexpr CGFloat kCellWidth = 62.0;
static constexpr CGFloat kCellHeight = 26.0;
static constexpr CGFloat kCellGapX = 4.0;
static constexpr CGFloat kCellGapY = 4.0;
static constexpr CGFloat kBeatGapExtra = 4.0;

// Pads
static constexpr CGFloat kPadHeight = 52.0;
static constexpr CGFloat kPadGap = 7.0;
static constexpr CGFloat kPadCornerRadius = 8.0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NSTextField* MakeLabel(NSRect frame, NSString* text, CGFloat font_size,
                       NSFontWeight weight = NSFontWeightRegular) {
  NSTextField* label = [[FFLabel alloc] initWithFrame:frame];
  [label setStringValue:text];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:[NSFont systemFontOfSize:font_size weight:weight]];
  return label;
}

NSView* MakePanel(NSRect frame) {
  NSView* panel = [[FFPanelView alloc] initWithFrame:frame];
  [panel setWantsLayer:YES];
  panel.layer.backgroundColor = [kColorPanel CGColor];
  panel.layer.cornerRadius = kPanelCornerRadius;
  panel.layer.borderWidth = kPanelBorderWidth;
  panel.layer.borderColor = [kColorPanelBorder CGColor];
  return panel;
}

NSView* MakeSectionHeader(NSRect frame, NSString* title) {
  NSView* container = [[NSView alloc] initWithFrame:frame];

  NSTextField* label = MakeLabel(NSMakeRect(0, 4, frame.size.width, 18),
                                  title, 11, NSFontWeightBold);
  [label setTextColor:kColorTextSecondary];
  [container addSubview:label];

  NSView* line = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, 1)];
  [line setWantsLayer:YES];
  line.layer.backgroundColor = [[kColorAccent colorWithAlphaComponent:0.3] CGColor];
  [container addSubview:line];

  return container;
}

static NSAttributedString* StyledTitle(NSString* text, NSColor* color,
                                        CGFloat size, NSFontWeight weight) {
  NSMutableParagraphStyle* para = [[NSMutableParagraphStyle alloc] init];
  [para setAlignment:NSTextAlignmentCenter];
  return [[NSAttributedString alloc] initWithString:text attributes:@{
    NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
    NSForegroundColorAttributeName: color,
    NSParagraphStyleAttributeName: para,
  }];
}

NSButton* MakeStyledButton(NSRect frame, NSString* title, id target, SEL action,
                            NSColor* bgColor, NSColor* textColor,
                            CGFloat fontSize = 12.0,
                            NSFontWeight fontWeight = NSFontWeightSemibold,
                            CGFloat cornerRadius = kButtonCornerRadius) {
  NSButton* button = [[NSButton alloc] initWithFrame:frame];
  [button setButtonType:NSButtonTypeMomentaryPushIn];
  [button setTarget:target];
  [button setAction:action];
  [button setBordered:NO];
  [button setWantsLayer:YES];
  button.layer.backgroundColor = [bgColor CGColor];
  button.layer.cornerRadius = cornerRadius;
  [button setAttributedTitle:StyledTitle(title, textColor, fontSize, fontWeight)];
  return button;
}

NSTextField* MakeValueReadout(NSRect frame) {
  NSTextField* label = MakeLabel(frame, @"--", 10, NSFontWeightMedium);
  [label setTextColor:kColorTextDim];
  [label setAlignment:NSTextAlignmentLeft];
  return label;
}

void PresentErrorAlert(NSString* title, const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  [alert setAlertStyle:NSAlertStyleCritical];
  [alert setMessageText:title];
  [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
  [alert addButtonWithTitle:@"OK"];
  [alert runModal];
}

// Compute X offset for a step column, with extra gap between beat groups.
static CGFloat StepColumnX(CGFloat base_x, std::size_t step) {
  CGFloat x = base_x + static_cast<CGFloat>(step) * (kCellWidth + kCellGapX);
  x += static_cast<CGFloat>(step / 4U) * kBeatGapExtra;
  return x;
}

}  // namespace

@interface FFAppDelegate : NSObject <NSApplicationDelegate>
- (instancetype)initWithRuntime:(Runtime*)runtime;
@end

@implementation FFAppDelegate {
  Runtime* runtime_;

  NSWindow* window_;
  NSButton* play_button_;
  NSTextField* status_label_;
  NSSlider* bpm_slider_;
  NSSlider* swing_slider_;
  NSPopUpButton* track_selector_;

  NSSlider* gain_slider_;
  NSSlider* pan_slider_;
  NSSlider* cutoff_slider_;
  NSSlider* decay_slider_;
  NSSlider* pitch_slider_;
  NSSlider* choke_slider_;

  std::array<std::array<NSButton*, kSteps>, kTrackCount> step_buttons_;
  std::array<NSButton*, kTrackCount> pad_buttons_;
  NSSound* interaction_sound_;

  NSTimer* ui_timer_;
  std::uint32_t highlighted_playhead_;

  // Value readouts
  NSTextField* bpm_readout_;
  NSTextField* swing_readout_;
  NSTextField* gain_readout_;
  NSTextField* pan_readout_;
  NSTextField* cutoff_readout_;
  NSTextField* decay_readout_;
  NSTextField* pitch_readout_;
  NSTextField* choke_readout_;
}

- (instancetype)initWithRuntime:(Runtime*)runtime {
  self = [super init];
  if (self != nil) {
    runtime_ = runtime;
    highlighted_playhead_ = 0;
    for (auto& row : step_buttons_) {
      for (auto& button : row) {
        button = nil;
      }
    }
    for (auto& button : pad_buttons_) {
      button = nil;
    }
    interaction_sound_ = [NSSound soundNamed:@"Tink"];
  }
  return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;

  RuntimeConfig config;
  config.audio.device_id = "default";
  config.audio.sample_rate_hz = 48'000;
  config.audio.buffer_size_frames = 256;

  std::string error;
  if (!runtime_->start(config, &error)) {
    PresentErrorAlert(@"Forest Floor failed to start", error);
    [NSApp terminate:nil];
    return;
  }

  const auto status = runtime_->status();
  NSLog(@"[FF] Runtime started: audio=%d midi=%d device=%s",
        status.audio_running, status.midi_running,
        status.audio_device_id.c_str());

  InitTheme();
  [self buildWindow];
  [NSApp activateIgnoringOtherApps:YES];
  [self refreshPatternButtonsFromRuntime];
  [self syncTrackControlsFromRuntime];
  [self updateStatus:nil];

  ui_timer_ = [NSTimer scheduledTimerWithTimeInterval:0.1
                                               target:self
                                             selector:@selector(updateStatus:)
                                             userInfo:nil
                                              repeats:YES];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  (void)notification;
  if (ui_timer_ != nil) {
    [ui_timer_ invalidate];
    ui_timer_ = nil;
  }
  runtime_->stop();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return YES;
}

// ---------------------------------------------------------------------------
// Window Construction
// ---------------------------------------------------------------------------

- (void)buildWindow {
  window_ = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(160, 120, 1240, 760)
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window_ setTitle:@"Forest Floor Drum Machine"];
  [window_ makeKeyAndOrderFront:nil];

  NSView* content = [window_ contentView];
  [content setWantsLayer:YES];
  content.layer.backgroundColor = [kColorBackground CGColor];

  // Glimmer effect -- diagonal gradient highlight that sweeps across the background
  {
    CAGradientLayer* glimmer = [CAGradientLayer layer];
    glimmer.frame = CGRectMake(-1240, 0, 1240 * 3, 760);
    glimmer.colors = @[
      (id)[[NSColor clearColor] CGColor],
      (id)[[NSColor colorWithWhite:1.0 alpha:0.06] CGColor],
      (id)[[NSColor colorWithWhite:1.0 alpha:0.12] CGColor],
      (id)[[NSColor colorWithWhite:1.0 alpha:0.06] CGColor],
      (id)[[NSColor clearColor] CGColor],
    ];
    glimmer.locations = @[@0.0, @0.42, @0.5, @0.58, @1.0];
    glimmer.startPoint = CGPointMake(0.0, 0.5);
    glimmer.endPoint = CGPointMake(1.0, 0.5);

    // Transform to make the sweep diagonal
    glimmer.transform = CATransform3DMakeRotation(-0.4, 0, 0, 1);

    CABasicAnimation* sweep = [CABasicAnimation animationWithKeyPath:@"position.x"];
    sweep.fromValue = @(-1240.0);
    sweep.toValue = @(1240.0 * 2.0);
    sweep.duration = 6.0;
    sweep.repeatCount = HUGE_VALF;
    sweep.timingFunction = [CAMediaTimingFunction
        functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    [glimmer addAnimation:sweep forKey:@"glimmerSweep"];

    [content.layer addSublayer:glimmer];
  }

  // -----------------------------------------------------------------------
  // Transport Panel
  // -----------------------------------------------------------------------
  const CGFloat transport_h = 72.0;
  const CGFloat transport_y = 760.0 - kMarginH - transport_h;  // ~672
  NSView* transport_panel = MakePanel(NSMakeRect(kMarginH, transport_y, kContentWidth, transport_h));
  [content addSubview:transport_panel];

  // Title
  NSTextField* title = MakeLabel(NSMakeRect(16, 42, 140, 22),
                                  @"Forest Floor",
                                  17, NSFontWeightBold);
  [title setTextColor:kColorTextPrimary];
  [transport_panel addSubview:title];

  // Play button -- styled with accent bg
  play_button_ = MakeStyledButton(NSMakeRect(16, 8, 72, 28),
                                   @"Play", self, @selector(onPlayToggle:),
                                   kColorAccent, kColorBackground,
                                   13.0, NSFontWeightBold);
  [transport_panel addSubview:play_button_];

  // BPM
  NSTextField* bpm_label = MakeLabel(NSMakeRect(104, 12, 36, 18), @"BPM", 11, NSFontWeightSemibold);
  [bpm_label setTextColor:kColorTextSecondary];
  [transport_panel addSubview:bpm_label];
  bpm_slider_ = [[NSSlider alloc] initWithFrame:NSMakeRect(140, 10, 160, 22)];
  [bpm_slider_ setMinValue:20.0];
  [bpm_slider_ setMaxValue:300.0];
  [bpm_slider_ setFloatValue:120.0];
  [bpm_slider_ setTarget:self];
  [bpm_slider_ setAction:@selector(onBpmChanged:)];
  if ([bpm_slider_ respondsToSelector:@selector(setTrackFillColor:)]) {
    [bpm_slider_ setTrackFillColor:kColorAccent];
  }
  [transport_panel addSubview:bpm_slider_];
  bpm_readout_ = MakeValueReadout(NSMakeRect(304, 12, 40, 16));
  [transport_panel addSubview:bpm_readout_];

  // Swing
  NSTextField* swing_label = MakeLabel(NSMakeRect(356, 12, 44, 18), @"Swing", 11, NSFontWeightSemibold);
  [swing_label setTextColor:kColorTextSecondary];
  [transport_panel addSubview:swing_label];
  swing_slider_ = [[NSSlider alloc] initWithFrame:NSMakeRect(400, 10, 130, 22)];
  [swing_slider_ setMinValue:0.0];
  [swing_slider_ setMaxValue:0.45];
  [swing_slider_ setFloatValue:0.12];
  [swing_slider_ setTarget:self];
  [swing_slider_ setAction:@selector(onSwingChanged:)];
  if ([swing_slider_ respondsToSelector:@selector(setTrackFillColor:)]) {
    [swing_slider_ setTrackFillColor:kColorAccent];
  }
  [transport_panel addSubview:swing_slider_];
  swing_readout_ = MakeValueReadout(NSMakeRect(534, 12, 40, 16));
  [transport_panel addSubview:swing_readout_];

  // Save / Load / Diagnostics -- subtle utility buttons
  NSButton* save_button = MakeStyledButton(NSMakeRect(kContentWidth - 340, 8, 80, 28),
                                            @"Save", self, @selector(onSaveProject:),
                                            kColorButtonBg, kColorButtonText, 11.0, NSFontWeightMedium);
  [transport_panel addSubview:save_button];

  NSButton* load_button = MakeStyledButton(NSMakeRect(kContentWidth - 252, 8, 80, 28),
                                            @"Load", self, @selector(onLoadProject:),
                                            kColorButtonBg, kColorButtonText, 11.0, NSFontWeightMedium);
  [transport_panel addSubview:load_button];

  NSButton* open_diag_button = MakeStyledButton(NSMakeRect(kContentWidth - 164, 8, 148, 28),
                                                 @"Open Diagnostics", self, @selector(onOpenDiagnostics:),
                                                 kColorButtonBg, kColorButtonText, 11.0, NSFontWeightMedium);
  [transport_panel addSubview:open_diag_button];

  // Status label -- positioned to the right of the title
  status_label_ = MakeLabel(NSMakeRect(160, 46, kContentWidth - 520, 16), @"Status", 10, NSFontWeightRegular);
  [status_label_ setTextColor:kColorTextDim];
  [transport_panel addSubview:status_label_];

  // -----------------------------------------------------------------------
  // Pads Panel
  // -----------------------------------------------------------------------
  const CGFloat pads_h = 86.0;
  const CGFloat pads_y = transport_y - kSectionSpacing - pads_h;
  NSView* pads_panel = MakePanel(NSMakeRect(kMarginH, pads_y, kContentWidth, pads_h));
  [content addSubview:pads_panel];

  NSView* pads_header = MakeSectionHeader(NSMakeRect(16, pads_h - 26, 120, 22), @"TRIGGER PADS");
  [pads_panel addSubview:pads_header];

  const CGFloat pad_total_width = kContentWidth - 32;
  const CGFloat pad_w = (pad_total_width - kPadGap * (kTrackCount - 1)) / kTrackCount;
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    NSString* label = [NSString stringWithFormat:@"%zu", track + 1U];
    NSButton* pad = MakeStyledButton(
        NSMakeRect(16 + (pad_w + kPadGap) * track, 6, pad_w, kPadHeight),
        label, self, @selector(onPadPressed:),
        kColorPadBg, [NSColor colorWithRed:0.600 green:0.976 blue:1.000 alpha:1.0],
        15.0, NSFontWeightBold, kPadCornerRadius);
    [pad setTag:static_cast<NSInteger>(track)];
    pad.layer.borderWidth = 1.0;
    pad.layer.borderColor = [kColorPanelBorder CGColor];
    [pads_panel addSubview:pad];
    pad_buttons_[track] = pad;
  }

  // -----------------------------------------------------------------------
  // Step Sequencer Panel
  // -----------------------------------------------------------------------
  const CGFloat grid_rows = static_cast<CGFloat>(kTrackCount);
  const CGFloat grid_inner_h = grid_rows * kCellHeight + (grid_rows - 1) * kCellGapY;
  const CGFloat seq_h = grid_inner_h + 60;  // header + column labels + padding
  const CGFloat seq_y = pads_y - kSectionSpacing - seq_h;
  NSView* seq_panel = MakePanel(NSMakeRect(kMarginH, seq_y, kContentWidth, seq_h));
  [content addSubview:seq_panel];

  NSView* seq_header = MakeSectionHeader(NSMakeRect(16, seq_h - 26, 140, 22), @"STEP SEQUENCER");
  [seq_panel addSubview:seq_header];

  const CGFloat grid_start_x = 80;
  const CGFloat grid_start_y = 8;

  // Step column headers
  for (std::size_t step = 0; step < kSteps; ++step) {
    NSString* step_title = [NSString stringWithFormat:@"%zu", step + 1U];
    CGFloat x = StepColumnX(grid_start_x, step);
    NSTextField* header = MakeLabel(
        NSMakeRect(x, grid_start_y + grid_inner_h + 4, kCellWidth, 14),
        step_title, 9, NSFontWeightMedium);
    [header setAlignment:NSTextAlignmentCenter];
    [header setTextColor:kColorTextDim];
    [seq_panel addSubview:header];
  }

  // Track rows
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    CGFloat row_y = grid_start_y + (kTrackCount - track - 1U) * (kCellHeight + kCellGapY);

    // Track label
    NSString* track_title = [NSString stringWithFormat:@"Track %zu", track + 1U];
    NSTextField* track_label = MakeLabel(
        NSMakeRect(4, row_y + 3, 70, 16),
        track_title, 10, NSFontWeightMedium);
    [track_label setAlignment:NSTextAlignmentRight];
    [track_label setTextColor:kColorTextSecondary];
    [seq_panel addSubview:track_label];

    // Step cells
    for (std::size_t step = 0; step < kSteps; ++step) {
      CGFloat x = StepColumnX(grid_start_x, step);
      NSButton* cell = [[NSButton alloc] initWithFrame:NSMakeRect(
          x, row_y, kCellWidth, kCellHeight)];
      [cell setTitle:@""];
      [cell setButtonType:NSButtonTypeMomentaryPushIn];
      [cell setBordered:NO];
      [cell setTag:static_cast<NSInteger>(track * 100U + step)];
      [cell setTarget:self];
      [cell setAction:@selector(onStepToggled:)];
      [cell setWantsLayer:YES];
      [seq_panel addSubview:cell];
      step_buttons_[track][step] = cell;
    }
  }

  // -----------------------------------------------------------------------
  // Per-Track Controls Panel
  // -----------------------------------------------------------------------
  const CGFloat controls_h = seq_y - kSectionSpacing - kMarginH;
  const CGFloat controls_y = kMarginH;
  NSView* controls_panel = MakePanel(NSMakeRect(kMarginH, controls_y, kContentWidth, controls_h));
  [content addSubview:controls_panel];

  NSView* controls_header = MakeSectionHeader(NSMakeRect(16, controls_h - 26, 160, 22), @"PER-TRACK CONTROLS");
  [controls_panel addSubview:controls_header];

  const CGFloat ctrl_row_top = controls_h - 56;

  // Track selector
  track_selector_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, ctrl_row_top, 140, 24) pullsDown:NO];
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    NSString* option = [NSString stringWithFormat:@"Track %zu", track + 1U];
    [track_selector_ addItemWithTitle:option];
  }
  [track_selector_ setTarget:self];
  [track_selector_ setAction:@selector(onTrackSelectionChanged:)];
  [controls_panel addSubview:track_selector_];

  // Load Sample
  NSButton* load_sample_button = MakeStyledButton(NSMakeRect(168, ctrl_row_top, 120, 24),
                                                   @"Load Sample", self, @selector(onLoadSample:),
                                                   kColorButtonBg, kColorButtonText, 11.0, NSFontWeightMedium);
  [controls_panel addSubview:load_sample_button];

  // MIDI Learn buttons
  NSButton* learn_gain_button = MakeStyledButton(NSMakeRect(300, ctrl_row_top, 105, 24),
                                                  @"Learn Gain", self, @selector(onLearnGain:),
                                                  kColorButtonBg, kColorButtonText, 10.0, NSFontWeightMedium);
  [controls_panel addSubview:learn_gain_button];

  NSButton* learn_cutoff_button = MakeStyledButton(NSMakeRect(413, ctrl_row_top, 115, 24),
                                                    @"Learn Cutoff", self, @selector(onLearnCutoff:),
                                                    kColorButtonBg, kColorButtonText, 10.0, NSFontWeightMedium);
  [controls_panel addSubview:learn_cutoff_button];

  NSButton* learn_decay_button = MakeStyledButton(NSMakeRect(536, ctrl_row_top, 115, 24),
                                                   @"Learn Decay", self, @selector(onLearnDecay:),
                                                   kColorButtonBg, kColorButtonText, 10.0, NSFontWeightMedium);
  [controls_panel addSubview:learn_decay_button];

  // Parameter sliders -- 3 rows of 2
  const CGFloat slider_row_top = ctrl_row_top - 40;
  const CGFloat slider_gap_v = 36;
  const CGFloat col2_x = 16 + 390;

  gain_readout_ = [self addTrackSliderWithContent:controls_panel
                                          originY:slider_row_top
                                            title:@"Gain"
                                         minValue:0.0
                                         maxValue:2.0
                                            store:&gain_slider_
                                           action:@selector(onTrackControlsChanged:)
                                          originX:16];
  pan_readout_ = [self addTrackSliderWithContent:controls_panel
                                         originY:slider_row_top
                                           title:@"Pan"
                                        minValue:-1.0
                                        maxValue:1.0
                                           store:&pan_slider_
                                          action:@selector(onTrackControlsChanged:)
                                         originX:col2_x];

  cutoff_readout_ = [self addTrackSliderWithContent:controls_panel
                                            originY:slider_row_top - slider_gap_v
                                              title:@"Filter"
                                           minValue:0.0
                                           maxValue:1.0
                                              store:&cutoff_slider_
                                             action:@selector(onTrackControlsChanged:)
                                            originX:16];
  decay_readout_ = [self addTrackSliderWithContent:controls_panel
                                           originY:slider_row_top - slider_gap_v
                                             title:@"Decay"
                                          minValue:0.0
                                          maxValue:1.0
                                             store:&decay_slider_
                                            action:@selector(onTrackControlsChanged:)
                                           originX:col2_x];

  pitch_readout_ = [self addTrackSliderWithContent:controls_panel
                                           originY:slider_row_top - (slider_gap_v * 2)
                                             title:@"Pitch"
                                          minValue:-24.0
                                          maxValue:24.0
                                             store:&pitch_slider_
                                            action:@selector(onTrackControlsChanged:)
                                           originX:16];
  choke_readout_ = [self addTrackSliderWithContent:controls_panel
                                           originY:slider_row_top - (slider_gap_v * 2)
                                             title:@"Choke"
                                          minValue:-1.0
                                          maxValue:15.0
                                             store:&choke_slider_
                                            action:@selector(onTrackControlsChanged:)
                                           originX:col2_x];
}

- (NSTextField*)addTrackSliderWithContent:(NSView*)content
                                  originY:(CGFloat)origin_y
                                    title:(NSString*)title
                                 minValue:(CGFloat)min_value
                                 maxValue:(CGFloat)max_value
                                    store:(NSSlider* __strong*)store
                                   action:(SEL)action
                                  originX:(CGFloat)origin_x {
  NSTextField* label = MakeLabel(NSMakeRect(origin_x, origin_y + 16, 50, 16),
                                  title, 11, NSFontWeightSemibold);
  [label setTextColor:kColorTextSecondary];
  [content addSubview:label];

  NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(origin_x + 52, origin_y + 12, 260, 20)];
  [slider setMinValue:min_value];
  [slider setMaxValue:max_value];
  [slider setTarget:self];
  [slider setAction:action];
  if ([slider respondsToSelector:@selector(setTrackFillColor:)]) {
    [slider setTrackFillColor:kColorAccent];
  }
  [content addSubview:slider];
  *store = slider;

  NSTextField* readout = MakeValueReadout(NSMakeRect(origin_x + 316, origin_y + 14, 56, 16));
  [content addSubview:readout];
  return readout;
}

// ---------------------------------------------------------------------------
// Step Grid Styling
// ---------------------------------------------------------------------------

- (void)refreshPatternButtonsFromRuntime {
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    for (std::size_t step = 0; step < kSteps; ++step) {
      NSButton* button = step_buttons_[track][step];
      if (button == nil) {
        continue;
      }
      const auto cell = runtime_->step(track, step);
      [button setState:cell.active ? NSControlStateValueOn : NSControlStateValueOff];
      [self styleStepButton:button active:cell.active playhead:(highlighted_playhead_ == step)];
    }
  }
}

- (void)styleStepButton:(NSButton*)button active:(BOOL)active playhead:(BOOL)playhead {
  if (button == nil) {
    return;
  }

  [button setWantsLayer:YES];
  button.layer.cornerRadius = kStepCellCornerRadius;

  if (playhead && active) {
    button.layer.backgroundColor = [kColorGridActive CGColor];
    button.layer.borderWidth = 2.0;
    button.layer.borderColor = [kColorPlayhead CGColor];
  } else if (playhead) {
    button.layer.backgroundColor = [kColorPlayheadGlow CGColor];
    button.layer.borderWidth = 2.0;
    button.layer.borderColor = [kColorPlayhead CGColor];
  } else if (active) {
    button.layer.backgroundColor = [kColorGridActive CGColor];
    button.layer.borderWidth = 0.0;
    button.layer.borderColor = [[NSColor clearColor] CGColor];
  } else {
    button.layer.backgroundColor = [kColorGridInactive CGColor];
    button.layer.borderWidth = 0.0;
    button.layer.borderColor = [[NSColor clearColor] CGColor];
  }
}

// ---------------------------------------------------------------------------
// Readout Formatting
// ---------------------------------------------------------------------------

- (void)updateReadouts {
  [bpm_readout_ setStringValue:[NSString stringWithFormat:@"%.0f", [bpm_slider_ floatValue]]];

  float swing = [swing_slider_ floatValue];
  [swing_readout_ setStringValue:[NSString stringWithFormat:@"%.0f%%", swing / 0.45f * 100.0f]];

  [gain_readout_ setStringValue:[NSString stringWithFormat:@"%.2f", [gain_slider_ floatValue]]];

  float pan = [pan_slider_ floatValue];
  if (std::fabs(pan) < 0.01f) {
    [pan_readout_ setStringValue:@"C"];
  } else if (pan < 0) {
    [pan_readout_ setStringValue:[NSString stringWithFormat:@"L%.0f", std::fabs(pan) * 100.0f]];
  } else {
    [pan_readout_ setStringValue:[NSString stringWithFormat:@"R%.0f", pan * 100.0f]];
  }

  [cutoff_readout_ setStringValue:[NSString stringWithFormat:@"%.0f%%", [cutoff_slider_ floatValue] * 100.0f]];
  [decay_readout_ setStringValue:[NSString stringWithFormat:@"%.0f%%", [decay_slider_ floatValue] * 100.0f]];

  float pitch = [pitch_slider_ floatValue];
  [pitch_readout_ setStringValue:[NSString stringWithFormat:@"%+.1f st", pitch]];

  int choke = static_cast<int>(std::llround([choke_slider_ floatValue]));
  if (choke < 0) {
    [choke_readout_ setStringValue:@"Off"];
  } else {
    [choke_readout_ setStringValue:[NSString stringWithFormat:@"G%d", choke]];
  }
}

// ---------------------------------------------------------------------------
// Track Controls
// ---------------------------------------------------------------------------

- (std::size_t)selectedTrack {
  NSInteger selected = [track_selector_ indexOfSelectedItem];
  if (selected < 0) {
    return 0;
  }
  return static_cast<std::size_t>(selected);
}

- (void)syncTrackControlsFromRuntime {
  const std::size_t track = [self selectedTrack];
  const auto parameters = runtime_->trackParameters(track);

  [gain_slider_ setFloatValue:parameters.gain];
  [pan_slider_ setFloatValue:parameters.pan];
  [cutoff_slider_ setFloatValue:parameters.filter_cutoff];
  [decay_slider_ setFloatValue:parameters.envelope_decay];
  [pitch_slider_ setFloatValue:parameters.pitch_semitones];
  [choke_slider_ setFloatValue:static_cast<float>(parameters.choke_group)];
  [self updateReadouts];
}

// ---------------------------------------------------------------------------
// Pad Flash Animation
// ---------------------------------------------------------------------------

- (void)flashPadButton:(NSButton*)button {
  CABasicAnimation* flash = [CABasicAnimation animationWithKeyPath:@"backgroundColor"];
  flash.fromValue = (id)[kColorAccent CGColor];
  flash.toValue = (id)[kColorPadBg CGColor];
  flash.duration = 0.15;
  flash.timingFunction = [CAMediaTimingFunction
      functionWithName:kCAMediaTimingFunctionEaseOut];
  [button.layer addAnimation:flash forKey:@"padFlash"];
  button.layer.backgroundColor = [kColorPadBg CGColor];
}

- (void)playInteractionSound {
  if (interaction_sound_ != nil) {
    [interaction_sound_ stop];
    [interaction_sound_ play];
    return;
  }
  NSBeep();
}

- (void)stylePlayButtonForRunning:(BOOL)running {
  if (running) {
    [play_button_ setAttributedTitle:StyledTitle(@"Stop", [NSColor whiteColor], 13, NSFontWeightBold)];
    play_button_.layer.backgroundColor = [kColorPlayActive CGColor];
  } else {
    [play_button_ setAttributedTitle:StyledTitle(@"Play", kColorBackground, 13, NSFontWeightBold)];
    play_button_.layer.backgroundColor = [kColorAccent CGColor];
  }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

- (void)onPlayToggle:(id)sender {
  (void)sender;
  const bool running = !runtime_->transportRunning();
  runtime_->setTransportRunning(running);
  [self stylePlayButtonForRunning:running];
  [self playInteractionSound];
}

- (void)onBpmChanged:(id)sender {
  (void)sender;
  runtime_->setTempoBpm([bpm_slider_ floatValue]);
  [self updateReadouts];
}

- (void)onSwingChanged:(id)sender {
  (void)sender;
  runtime_->setSwing([swing_slider_ floatValue]);
  [self updateReadouts];
}

- (void)onPadPressed:(id)sender {
  NSButton* button = (NSButton*)sender;
  const NSInteger track = [button tag];
  if (track >= 0 && runtime_->triggerPad(static_cast<std::size_t>(track), 120)) {
    [self flashPadButton:button];
    [self playInteractionSound];
  }
}

- (void)onStepToggled:(id)sender {
  NSButton* button = (NSButton*)sender;
  const NSInteger tag = [button tag];
  if (tag < 0) {
    return;
  }

  const std::size_t track = static_cast<std::size_t>(tag / 100);
  const std::size_t step = static_cast<std::size_t>(tag % 100);
  const bool active = !runtime_->step(track, step).active;
  runtime_->setStep(track, step, active, active ? 110 : 0);
  [self styleStepButton:button active:active playhead:(highlighted_playhead_ == step)];
  [self playInteractionSound];
}

- (void)onTrackSelectionChanged:(id)sender {
  (void)sender;
  [self syncTrackControlsFromRuntime];
}

- (void)onTrackControlsChanged:(id)sender {
  (void)sender;
  ff::engine::TrackParameters parameters;
  parameters.gain = [gain_slider_ floatValue];
  parameters.pan = [pan_slider_ floatValue];
  parameters.filter_cutoff = [cutoff_slider_ floatValue];
  parameters.envelope_decay = [decay_slider_ floatValue];
  parameters.pitch_semitones = [pitch_slider_ floatValue];
  parameters.choke_group = static_cast<int>(std::llround([choke_slider_ floatValue]));

  runtime_->setTrackParameters([self selectedTrack], parameters);
  [self updateReadouts];
}

- (void)onLearnGain:(id)sender {
  (void)sender;
  runtime_->beginMidiLearn([self selectedTrack], MidiLearnSlot::kTrackGain);
}

- (void)onLearnCutoff:(id)sender {
  (void)sender;
  runtime_->beginMidiLearn([self selectedTrack], MidiLearnSlot::kTrackFilterCutoff);
}

- (void)onLearnDecay:(id)sender {
  (void)sender;
  runtime_->beginMidiLearn([self selectedTrack], MidiLearnSlot::kTrackEnvelopeDecay);
}

- (void)onLoadSample:(id)sender {
  (void)sender;

  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  [panel setAllowedFileTypes:@[@"wav", @"WAV"]];

  if ([panel runModal] != NSModalResponseOK) {
    return;
  }

  NSURL* url = [[panel URLs] firstObject];
  if (url == nil) {
    return;
  }

  std::string error;
  if (!runtime_->setTrackSampleFromFile([self selectedTrack],
                                        std::filesystem::path([[url path] UTF8String]),
                                        &error)) {
    PresentErrorAlert(@"Sample load failed", error);
  }
}

- (void)onSaveProject:(id)sender {
  (void)sender;

  NSSavePanel* panel = [NSSavePanel savePanel];
  [panel setAllowedFileTypes:@[@"ffproject", @"txt"]];
  [panel setNameFieldStringValue:@"session.ffproject"];

  if ([panel runModal] != NSModalResponseOK) {
    return;
  }

  NSURL* url = [panel URL];
  if (url == nil) {
    return;
  }

  std::string error;
  if (!runtime_->saveProject(std::filesystem::path([[url path] UTF8String]), &error)) {
    PresentErrorAlert(@"Save failed", error);
  }
}

- (void)onLoadProject:(id)sender {
  (void)sender;

  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  [panel setAllowedFileTypes:@[@"ffproject", @"txt"]];

  if ([panel runModal] != NSModalResponseOK) {
    return;
  }

  NSURL* url = [[panel URLs] firstObject];
  if (url == nil) {
    return;
  }

  std::string error;
  if (!runtime_->loadProject(std::filesystem::path([[url path] UTF8String]), &error)) {
    PresentErrorAlert(@"Load failed", error);
    return;
  }

  [bpm_slider_ setFloatValue:runtime_->tempoBpm()];
  [swing_slider_ setFloatValue:runtime_->swing()];
  [self refreshPatternButtonsFromRuntime];
  [self syncTrackControlsFromRuntime];
}

- (void)onOpenDiagnostics:(id)sender {
  (void)sender;
  const auto path = runtime_->diagnosticsDirectory();
  if (path.empty()) {
    return;
  }

  NSString* diagnostics_path = [NSString stringWithUTF8String:path.string().c_str()];
  NSURL* url = [NSURL fileURLWithPath:diagnostics_path isDirectory:YES];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

// ---------------------------------------------------------------------------
// Status Update (100ms Timer)
// ---------------------------------------------------------------------------

- (void)updateStatus:(id)sender {
  (void)sender;

  const auto status = runtime_->status();

  (void)0; // status logging removed — diagnostics available via UI status bar

  NSString* transport = status.transport_running ? @"Playing" : @"Stopped";
  NSString* callback = [NSString stringWithFormat:@"XRuns backend=%llu engine=%llu",
                                                  status.backend_xruns,
                                                  status.engine_xruns];
  NSString* midi = [NSString stringWithUTF8String:status.midi_device_summary.c_str()];
  NSString* playhead = [NSString stringWithFormat:@"Step %u", status.playhead_step + 1U];

  NSString* learned = @"";
  if (status.learned_cc_binding.has_value()) {
    learned = [NSString stringWithFormat:@"  |  %@",
               [NSString stringWithUTF8String:status.learned_cc_binding->c_str()]];
  }

  NSString* status_text = [NSString stringWithFormat:@"%@  |  BPM %.1f  |  %@  |  %@  |  %@%@",
                           transport,
                           runtime_->tempoBpm(),
                           playhead,
                           callback,
                           midi,
                           learned];
  [status_label_ setStringValue:status_text];

  [self stylePlayButtonForRunning:status.transport_running];

  // Update BPM readout from actual value
  [bpm_readout_ setStringValue:[NSString stringWithFormat:@"%.0f", runtime_->tempoBpm()]];

  const std::uint32_t new_playhead = status.playhead_step % kSteps;
  if (new_playhead != highlighted_playhead_) {
    highlighted_playhead_ = new_playhead;
    [self refreshPatternButtonsFromRuntime];
  }
}

@end

int runMacDesktopApp(ff::desktop::Runtime* runtime,
                     ff::diagnostics::Reporter* diagnostics) {
  (void)diagnostics;

  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    FFAppDelegate* delegate = [[FFAppDelegate alloc] initWithRuntime:runtime];
    [app setDelegate:delegate];
    [app run];
  }

  return 0;
}

#endif
