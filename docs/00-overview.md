# Forest Floor Overview

## Purpose
Forest Floor is a modular instrument platform starting with a drum machine and expanding into a broader node-based music system.

## North Star
A stable real-time core with composable modules that can be developed and released independently.

## Vocabulary
- Node: A processing unit in the graph (instrument, effect, utility, or MIDI processor).
- Graph: Directed routing of audio, events, and parameters between nodes.
- Event: Timestamped discrete message (note, transport, trigger, automation point).
- Parameter: Continuous control value exposed by a module.
- Kit: Instrument/sample configuration for a playable setup.
- Pattern: Sequenced event data.
- Project: Full session containing kits, patterns, routing, and mappings.

## Layer Model
- UI Layer: Visualization and editing only.
- Control Plane: Sequencing, mappings, state, persistence, undo/redo.
- Engine Layer: Real-time scheduler, node graph execution, parameter smoothing.
- DSP Layer: Voice generation and effects math.

## Architectural Priorities
1. Real-time safety first.
2. Stable module contracts over convenience shortcuts.
3. Backward-compatible state formats.
4. Deterministic behavior under CPU pressure.

## First Delivery Target
- Standalone desktop app.
- 16-step sequencer with 8 tracks.
- Sample playback voices.
- MIDI note trigger and initial MIDI Learn.
- Audio device selection and latency controls.

## Daily Developer Entry Point
- Local commands and tool usage: `docs/development/WORKFLOW.md`
