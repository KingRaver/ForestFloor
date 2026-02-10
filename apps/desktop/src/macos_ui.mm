#if defined(APPLE)

#import "macos_ui.hpp"

#import <AppKit/AppKit.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "runtime.hpp"

namespace {

using ff::desktop::MidiLearnSlot;
using ff::desktop::Runtime;
using ff::desktop::RuntimeConfig;

static constexpr std::size_t kTrackCount = Runtime::kTrackCount;
static constexpr std::size_t kSteps = Runtime::kSteps;

NSTextField* MakeLabel(NSRect frame, NSString* text, CGFloat font_size,
                       NSFontWeight weight = NSFontWeightRegular) {
  NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
  [label setStringValue:text];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:[NSFont systemFontOfSize:font_size weight:weight]];
  return label;
}

NSButton* MakeButton(NSRect frame, NSString* title, id target, SEL action) {
  NSButton* button = [[NSButton alloc] initWithFrame:frame];
  [button setTitle:title];
  [button setTarget:target];
  [button setAction:action];
  [button setBezelStyle:NSBezelStyleRounded];
  return button;
}

void PresentErrorAlert(NSString* title, const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  [alert setAlertStyle:NSAlertStyleCritical];
  [alert setMessageText:title];
  [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
  [alert addButtonWithTitle:@"OK"];
  [alert runModal];
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

  NSTimer* ui_timer_;
  std::uint32_t highlighted_playhead_;
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

  [self buildWindow];
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
  content.layer.backgroundColor = [[NSColor colorWithRed:0.09 green:0.1 blue:0.11 alpha:1.0] CGColor];

  NSTextField* title = MakeLabel(NSMakeRect(24, 718, 460, 24),
                                 @"Forest Floor - Production Drum Machine",
                                 20,
                                 NSFontWeightSemibold);
  [title setTextColor:[NSColor colorWithRed:0.95 green:0.96 blue:0.97 alpha:1.0]];
  [content addSubview:title];

  play_button_ = MakeButton(NSMakeRect(24, 676, 120, 30), @"Play", self,
                            @selector(onPlayToggle:));
  [content addSubview:play_button_];

  NSTextField* bpm_label = MakeLabel(NSMakeRect(168, 680, 70, 20), @"BPM", 13, NSFontWeightMedium);
  [bpm_label setTextColor:[NSColor colorWithRed:0.91 green:0.92 blue:0.93 alpha:1.0]];
  [content addSubview:bpm_label];
  bpm_slider_ = [[NSSlider alloc] initWithFrame:NSMakeRect(210, 676, 210, 24)];
  [bpm_slider_ setMinValue:20.0];
  [bpm_slider_ setMaxValue:300.0];
  [bpm_slider_ setFloatValue:120.0];
  [bpm_slider_ setTarget:self];
  [bpm_slider_ setAction:@selector(onBpmChanged:)];
  [content addSubview:bpm_slider_];

  NSTextField* swing_label = MakeLabel(NSMakeRect(438, 680, 70, 20), @"Swing", 13, NSFontWeightMedium);
  [swing_label setTextColor:[NSColor colorWithRed:0.91 green:0.92 blue:0.93 alpha:1.0]];
  [content addSubview:swing_label];
  swing_slider_ = [[NSSlider alloc] initWithFrame:NSMakeRect(492, 676, 170, 24)];
  [swing_slider_ setMinValue:0.0];
  [swing_slider_ setMaxValue:0.45];
  [swing_slider_ setFloatValue:0.12];
  [swing_slider_ setTarget:self];
  [swing_slider_ setAction:@selector(onSwingChanged:)];
  [content addSubview:swing_slider_];

  NSButton* save_button = MakeButton(NSMakeRect(680, 676, 90, 30), @"Save", self,
                                     @selector(onSaveProject:));
  [content addSubview:save_button];

  NSButton* load_button = MakeButton(NSMakeRect(780, 676, 90, 30), @"Load", self,
                                     @selector(onLoadProject:));
  [content addSubview:load_button];

  NSButton* open_diag_button = MakeButton(NSMakeRect(880, 676, 180, 30),
                                          @"Open Diagnostics", self,
                                          @selector(onOpenDiagnostics:));
  [content addSubview:open_diag_button];

  status_label_ = MakeLabel(NSMakeRect(24, 646, 1180, 20), @"Status", 12, NSFontWeightRegular);
  [status_label_ setTextColor:[NSColor colorWithRed:0.81 green:0.84 blue:0.87 alpha:1.0]];
  [content addSubview:status_label_];

  const CGFloat pad_start_x = 24;
  const CGFloat pad_start_y = 590;
  const CGFloat pad_w = 140;
  const CGFloat pad_h = 34;
  const CGFloat pad_gap = 10;
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    NSString* label = [NSString stringWithFormat:@"Pad %zu", track + 1U];
    NSButton* pad = MakeButton(
        NSMakeRect(pad_start_x + (pad_w + pad_gap) * track, pad_start_y, pad_w, pad_h),
        label,
        self,
        @selector(onPadPressed:));
    [pad setTag:static_cast<NSInteger>(track)];
    [pad setBezelStyle:NSBezelStyleTexturedRounded];
    [content addSubview:pad];
    pad_buttons_[track] = pad;
  }

  const CGFloat grid_start_x = 24;
  const CGFloat grid_start_y = 368;
  const CGFloat cell_w = 64;
  const CGFloat cell_h = 24;
  const CGFloat cell_gap_x = 6;
  const CGFloat cell_gap_y = 6;

  for (std::size_t step = 0; step < kSteps; ++step) {
    NSString* step_title = [NSString stringWithFormat:@"%zu", step + 1U];
    NSTextField* header = MakeLabel(
        NSMakeRect(grid_start_x + (cell_w + cell_gap_x) * step, grid_start_y + 8 * (cell_h + cell_gap_y) + 4,
                   cell_w, 16),
        step_title,
        11,
        NSFontWeightMedium);
    [header setAlignment:NSTextAlignmentCenter];
    [header setTextColor:[NSColor colorWithRed:0.86 green:0.88 blue:0.9 alpha:1.0]];
    [content addSubview:header];
  }

  for (std::size_t track = 0; track < kTrackCount; ++track) {
    NSString* track_title = [NSString stringWithFormat:@"Track %zu", track + 1U];
    NSTextField* track_label = MakeLabel(
        NSMakeRect(grid_start_x - 82,
                   grid_start_y + (kTrackCount - track - 1U) * (cell_h + cell_gap_y) + 2,
                   74,
                   18),
        track_title,
        11,
        NSFontWeightMedium);
    [track_label setAlignment:NSTextAlignmentRight];
    [track_label setTextColor:[NSColor colorWithRed:0.86 green:0.88 blue:0.9 alpha:1.0]];
    [content addSubview:track_label];

    for (std::size_t step = 0; step < kSteps; ++step) {
      NSButton* cell = [[NSButton alloc] initWithFrame:NSMakeRect(
          grid_start_x + (cell_w + cell_gap_x) * step,
          grid_start_y + (kTrackCount - track - 1U) * (cell_h + cell_gap_y),
          cell_w,
          cell_h)];
      [cell setTitle:@""];
      [cell setButtonType:NSButtonTypePushOnPushOff];
      [cell setBezelStyle:NSBezelStyleTexturedSquare];
      [cell setTag:static_cast<NSInteger>(track * 100U + step)];
      [cell setTarget:self];
      [cell setAction:@selector(onStepToggled:)];
      [cell setWantsLayer:YES];
      [content addSubview:cell];
      step_buttons_[track][step] = cell;
    }
  }

  NSTextField* controls_title = MakeLabel(NSMakeRect(24, 318, 220, 20),
                                          @"Per-Track Controls",
                                          15,
                                          NSFontWeightSemibold);
  [controls_title setTextColor:[NSColor colorWithRed:0.95 green:0.96 blue:0.97 alpha:1.0]];
  [content addSubview:controls_title];

  track_selector_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(24, 286, 160, 28) pullsDown:NO];
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    NSString* option = [NSString stringWithFormat:@"Track %zu", track + 1U];
    [track_selector_ addItemWithTitle:option];
  }
  [track_selector_ setTarget:self];
  [track_selector_ setAction:@selector(onTrackSelectionChanged:)];
  [content addSubview:track_selector_];

  NSButton* load_sample_button = MakeButton(NSMakeRect(196, 286, 140, 28),
                                            @"Load Sample", self,
                                            @selector(onLoadSample:));
  [content addSubview:load_sample_button];

  NSButton* learn_gain_button = MakeButton(NSMakeRect(348, 286, 120, 28),
                                           @"Learn Gain", self,
                                           @selector(onLearnGain:));
  [content addSubview:learn_gain_button];

  NSButton* learn_cutoff_button = MakeButton(NSMakeRect(476, 286, 130, 28),
                                             @"Learn Cutoff", self,
                                             @selector(onLearnCutoff:));
  [content addSubview:learn_cutoff_button];

  NSButton* learn_decay_button = MakeButton(NSMakeRect(614, 286, 130, 28),
                                            @"Learn Decay", self,
                                            @selector(onLearnDecay:));
  [content addSubview:learn_decay_button];

  const CGFloat slider_left = 24;
  const CGFloat slider_width = 340;
  const CGFloat slider_gap = 44;

  [self addTrackSliderWithContent:content
                          originY:238
                            title:@"Gain"
                         minValue:0.0
                         maxValue:2.0
                            store:&gain_slider_
                           action:@selector(onTrackControlsChanged:)];
  [self addTrackSliderWithContent:content
                          originY:238
                            title:@"Pan"
                         minValue:-1.0
                         maxValue:1.0
                            store:&pan_slider_
                           action:@selector(onTrackControlsChanged:)
                         originX:slider_left + slider_width + 26];

  [self addTrackSliderWithContent:content
                          originY:238 - slider_gap
                            title:@"Filter"
                         minValue:0.0
                         maxValue:1.0
                            store:&cutoff_slider_
                           action:@selector(onTrackControlsChanged:)];
  [self addTrackSliderWithContent:content
                          originY:238 - slider_gap
                            title:@"Decay"
                         minValue:0.0
                         maxValue:1.0
                            store:&decay_slider_
                           action:@selector(onTrackControlsChanged:)
                         originX:slider_left + slider_width + 26];

  [self addTrackSliderWithContent:content
                          originY:238 - (slider_gap * 2)
                            title:@"Pitch"
                         minValue:-24.0
                         maxValue:24.0
                            store:&pitch_slider_
                           action:@selector(onTrackControlsChanged:)];
  [self addTrackSliderWithContent:content
                          originY:238 - (slider_gap * 2)
                            title:@"Choke"
                         minValue:-1.0
                         maxValue:15.0
                            store:&choke_slider_
                           action:@selector(onTrackControlsChanged:)
                         originX:slider_left + slider_width + 26];
}

- (void)addTrackSliderWithContent:(NSView*)content
                          originY:(CGFloat)origin_y
                            title:(NSString*)title
                         minValue:(CGFloat)min_value
                         maxValue:(CGFloat)max_value
                            store:(NSSlider* __strong*)store
                           action:(SEL)action {
  [self addTrackSliderWithContent:content
                          originY:origin_y
                            title:title
                         minValue:min_value
                         maxValue:max_value
                            store:store
                           action:action
                          originX:24];
}

- (void)addTrackSliderWithContent:(NSView*)content
                          originY:(CGFloat)origin_y
                            title:(NSString*)title
                         minValue:(CGFloat)min_value
                         maxValue:(CGFloat)max_value
                            store:(NSSlider* __strong*)store
                           action:(SEL)action
                          originX:(CGFloat)origin_x {
  NSTextField* label = MakeLabel(NSMakeRect(origin_x, origin_y + 18, 70, 18),
                                 title,
                                 12,
                                 NSFontWeightMedium);
  [label setTextColor:[NSColor colorWithRed:0.86 green:0.88 blue:0.9 alpha:1.0]];
  [content addSubview:label];

  NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(origin_x + 62, origin_y + 14, 290, 20)];
  [slider setMinValue:min_value];
  [slider setMaxValue:max_value];
  [slider setTarget:self];
  [slider setAction:action];
  [content addSubview:slider];
  *store = slider;
}

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
  if (active) {
    button.layer.backgroundColor = [[NSColor colorWithRed:0.22 green:0.56 blue:0.35 alpha:1.0] CGColor];
  } else {
    button.layer.backgroundColor = [[NSColor colorWithRed:0.23 green:0.24 blue:0.25 alpha:1.0] CGColor];
  }
  button.layer.cornerRadius = 4.0;
  if (playhead) {
    button.layer.borderWidth = 2.0;
    button.layer.borderColor = [[NSColor colorWithRed:0.95 green:0.79 blue:0.34 alpha:1.0] CGColor];
  } else {
    button.layer.borderWidth = 0.0;
    button.layer.borderColor = [[NSColor clearColor] CGColor];
  }
}

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
}

- (void)onPlayToggle:(id)sender {
  (void)sender;
  runtime_->toggleTransport();
}

- (void)onBpmChanged:(id)sender {
  (void)sender;
  runtime_->setTempoBpm([bpm_slider_ floatValue]);
}

- (void)onSwingChanged:(id)sender {
  (void)sender;
  runtime_->setSwing([swing_slider_ floatValue]);
}

- (void)onPadPressed:(id)sender {
  NSButton* button = (NSButton*)sender;
  const NSInteger track = [button tag];
  if (track >= 0) {
    runtime_->triggerPad(static_cast<std::size_t>(track), 120);
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
  const bool active = [button state] == NSControlStateValueOn;
  runtime_->setStep(track, step, active, active ? 110 : 0);
  [self styleStepButton:button active:active playhead:(highlighted_playhead_ == step)];
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

- (void)updateStatus:(id)sender {
  (void)sender;

  const auto status = runtime_->status();

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

  [play_button_ setTitle:status.transport_running ? @"Stop" : @"Play"];

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
    [app activateIgnoringOtherApps:YES];
    [app run];
  }

  return 0;
}

#endif
