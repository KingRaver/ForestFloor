# Audio Backend Debugging Log

## Problem
Buttons not reactive, no sounds being made from the desktop drum machine app.

## Root Causes Identified

### 1. `#if defined(APPLE)` preprocessor bug (FIXED)
- `APPLE` is a CMake variable, not a C++ preprocessor define
- Changed to `#if defined(__APPLE__)` in `main.cpp`, `audio_backend_stub.cpp`, `audio_backend_coreaudio.mm`, `macos_ui.mm`
- **Status: Fixed** (was already in working tree)

### 2. Transport race condition in runtime.cpp (FIXED)
- `setTransportRunning()` was calling `transport_running_.store(true)` immediately AND enqueueing a command
- `processSequencer()` would see transport as running before sequencer state was initialized by the audio callback
- **Fix**: Only store the atomic if `enqueueCommand` fails (fallback path)
- **Status: Fixed**

### 3. Step button type conflict in macos_ui.mm (FIXED)
- Buttons used `NSButtonTypeToggle` which auto-toggles state on click
- Conflicted with handler that manually manages state via `runtime_->step()`
- **Fix**: Changed to `NSButtonTypeMomentaryPushIn`
- **Status: Fixed**

### 4. `callback_` assigned after `AudioOutputUnitStart` (FIXED)
- In `audio_backend_coreaudio.mm`, `callback_` was set AFTER `AudioOutputUnitStart`
- The render callback could fire before `callback_` was set, hitting the null check early return
- **Fix**: Moved `callback_` assignment before `AudioOutputUnitStart`
- **Status: Fixed**

### 5. CoreAudio render callback never fires — `kAudioUnitErr_TooManyFramesToProcess` (FIXED)
- `AudioOutputUnitStart` returns `noErr` (success)
- `kAudioOutputUnitProperty_IsRunning` reports `value=1` (running)
- But the render callback function was **never invoked** — `callbacks=0` after 5+ seconds
- This was the **core blocker** — no audio callback means no command processing, no sequencer, no sound

#### Root Cause Discovery
- The macOS unified system log (`/usr/bin/log show`) revealed the real error:
  ```
  (CoreAudio) from <private>, render err: -10874
  ```
- Error **-10874** = `kAudioUnitErr_TooManyFramesToProcess`
- The code was setting `kAudioUnitProperty_MaximumFramesPerSlice` to **256** frames
- The MacBook Air Speakers hardware buffer size is **512** frames
- CoreAudio tried to render 512 frames but the AudioUnit's max was 256
- Every render cycle failed **internally** before reaching our callback
- `AudioOutputUnitStart` still returned `noErr` — the unit was "running" but every render attempt errored silently
- This also explains why ALL previous attempts (A–E) failed: they all inherited the same MaximumFramesPerSlice=256

#### Fix
- Query `kAudioDevicePropertyBufferFrameSize` from the hardware to get its actual buffer size
- Set `MaximumFramesPerSlice` to `max(hw_buffer_frames, requested_buffer_size, 512)`
- This ensures the AudioUnit can handle whatever frame count the hardware delivers
- **Status: Fixed** — callbacks now fire at ~94/sec (48000Hz / 512 frames)

### 6. Sample rate mismatch between engine and hardware (FIXED)
- Runtime configured engine for 48000Hz, but CoreAudio backend may negotiate a different rate with hardware (e.g. 44100Hz for MOTU)
- Envelope coefficients, filter cutoffs, and sequencer timing were all calculated with the wrong sample rate
- **Fix**: Added `actualSampleRate()` to AudioBackend interface; Runtime feeds back the actual rate to the engine after audio backend starts
- **Status: Fixed**

### 7. Audio thread memory allocation (FIXED)
- `handleAudioCallback()` was creating a new `std::vector<TriggerEvent>` on every callback
- Memory allocation on the real-time audio thread can cause priority inversion and glitches
- **Fix**: Changed to `thread_local` vector that is cleared and reused each callback
- **Status: Fixed**

## Approaches Tried for Issue #5 (Before Root Cause Found)

### Attempt A: kAudioUnitSubType_DefaultOutput
- Used `kAudioUnitSubType_DefaultOutput` instead of `kAudioUnitSubType_HALOutput`
- **Result**: callbacks=0 (same MaxFrames bug was present)

### Attempt B: kAudioUnitSubType_HALOutput with explicit default device
- Resolved default device via `kAudioHardwarePropertyDefaultOutputDevice`
- Default device was **MOTU Audio Express** (device 102), 8ch, 44100Hz, non-interleaved
- Used HALOutput with explicit device ID, matched hardware sample rate (44100Hz)
- **Result**: callbacks=0 (same MaxFrames bug)

### Attempt C: Match hardware sample rate
- Queried `kAudioUnitProperty_StreamFormat` on output scope to get hardware rate
- Set input scope stream format to match (44100Hz instead of 48000Hz)
- **Result**: callbacks=0 (same MaxFrames bug)

### Attempt D: Device fallback with 250ms verification
- After starting, slept 250ms and checked if callbacks arrived
- If not, stopped and tried next output device
- Tried MacBook Air Speakers (device 75, 48kHz, 2ch) and D24-20 HDMI (device 87)
- **Result**: ALL devices showed callbacks=0 (same MaxFrames bug on all devices)
- **User feedback**: "fallback is not the correct approach, these are options i can designate for audio output/input"

### Attempt E: HAL IOProc (AudioDeviceCreateIOProcID) bypassing AudioUnit
- Used low-level `AudioDeviceCreateIOProcID` + `AudioDeviceStart` directly
- Bypasses AudioUnit layer entirely
- **Result**: "whatever you changed killed the audio output"
- **Reverted** back to AudioUnit approach

### Attempt F: Query hardware buffer size and fix MaximumFramesPerSlice (SUCCESS)
- Used macOS unified system log (`/usr/bin/log show`) to discover the real error code
- CoreAudio was logging `render err: -10874` (`kAudioUnitErr_TooManyFramesToProcess`) on every render cycle
- Queried `kAudioDevicePropertyBufferFrameSize` — hardware uses 512 frames, code was capping at 256
- Set MaximumFramesPerSlice to `max(hw_buffer_frames, config_buffer_size, 512)`
- **Result**: callbacks=6 after 100ms, callbacks=43 after 500ms — ~94 callbacks/sec as expected
- **Audio is now working!**

## Key Lessons
1. **Always check the macOS system log** — CoreAudio error codes don't surface through the API. `AudioOutputUnitStart` returns `noErr` even when every render cycle will fail.
2. **Never hardcode MaximumFramesPerSlice** — Always query `kAudioDevicePropertyBufferFrameSize` from the target device and set MaxFrames >= that value.
3. **Error -10874 (`kAudioUnitErr_TooManyFramesToProcess`) is silent** — The AudioUnit appears to be running but the render callback is never invoked. The only diagnostic is the system log.

## Debug Code Removed
- `macos_ui.mm`: Removed NSLog from `onPlayToggle:`, `onPadPressed:`, `onStepToggled:`
- `macos_ui.mm`: Removed auto-start transport on launch
- `macos_ui.mm`: Reduced tick logging to status bar only
- `runtime.hpp`: Moved `audio_callback_count_` back to private

## Devices on This System
| Device ID | Name | Output | Rate | Channels | HW Buffer | Notes |
|-----------|------|--------|------|----------|-----------|-------|
| 102 | MOTU Audio Express | Yes | 44100 | 8 (non-interleaved) | varies | User's primary interface |
| 75 | MacBook Air Speakers | Yes | 48000 | 2 | 512 | Built-in |
| 87 | D24-20 | Yes | 44100 | 2 | varies | HDMI |
| 82 | MacBook Air Microphone | No | - | - | - | Input only |
| 51 | rekordbox Aggregate Device | No | - | - | - | No output channels |

## Issue #8: Trigger Pad Visual Feedback

### Problem
Trigger pads produce sound when clicked but there is no clear visual state change to confirm the press was registered. The user cannot visually tell whether a pad click was successful.

### Attempt A: Scale bounce + color invert (CAAnimationGroup)
- Added `CAKeyframeAnimation` on `transform.scale`: 1.0 → 0.88 → 1.04 → 1.0 over 350ms
- Added `CAKeyframeAnimation` on `backgroundColor`: purple → cyan (held ~160ms) → purple
- Added `CAKeyframeAnimation` on `borderColor`: panel border → white (held ~160ms) → panel border
- Inverted text color via `dispatch_after`: cyan → deep blue, restored after 160ms
- Grouped all three layer animations in a `CAAnimationGroup` (350ms, ease-in-ease-out)
- Extracted pad text color into `kColorPadText` theme constant for reuse
- **Result**: Animation fires but does not achieve the desired visual feedback — the state change is not distinct enough to register at a glance during fast playing
- **Status: Did not meet goal** — need to explore other approaches

### Ideas for Future Attempts
- Custom `NSView` subclass with explicit pressed/unpressed draw states instead of animation
- Toggle a persistent "active" style (e.g. bright border or inverted colors) that holds for a fixed duration
- Use `NSTrackingArea` for mouseDown/mouseUp to show pressed state for the full duration of the physical click
- Add a brief LED-style indicator dot adjacent to each pad

## Current State (All Issues Resolved except #8)
- `audio_backend_coreaudio.mm`: CoreAudio HALOutput with proper MaximumFramesPerSlice handling
- `runtime.cpp`: Sample rate feedback from backend to engine, thread_local event vector
- `macos_ui.mm`: Clean UI code, debug logging removed
- `macos_ui.mm`: Trigger pad flash uses scale bounce + color invert (Attempt A — under review)
- Headless smoke/soak tests pass
- Audio callbacks firing at expected rate (~94/sec for 48000Hz/512 frames)
