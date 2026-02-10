# Forest Floor

**A modular drum machine platform built for real-time performance and long-term composability.**

Forest Floor is a hybrid C++/Rust audio engine designed from the ground up for deterministic, low-latency drum synthesis and sequencing. The architecture enforces strict layer boundaries — C++ owns the real-time audio path, Rust owns the control plane — connected through a versioned C ABI contract that keeps both sides honest.

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  UI Layer  (replaceable client)                 │
├─────────────────────────────────────────────────┤
│  Control Plane  [Rust]                          │
│  sequencing · MIDI mapping · state · presets    │
├─────────────────────────────────────────────────┤
│  Engine  [C++]                                  │
│  real-time graph host · parameter smoothing     │
├─────────────────────────────────────────────────┤
│  DSP  [C++]                                     │
│  samplers · filters · effects · voice mgmt      │
└─────────────────────────────────────────────────┘
```

Each layer communicates downward through typed interfaces and upward through lock-free event channels. No layer may bypass the one directly below it.

## Key Design Decisions

- **Zero allocation in the audio thread.** No mallocs, no locks, no syscalls on the RT path. Events flow through bounded SPSC ring buffers; parameters through atomic double-buffered snapshots.
- **Deterministic replay.** Every event is timestamped in the sample domain. A saved project replays the identical event stream and parameter payload, frame for frame.
- **Versioned plugin SDK.** Plugins declare capabilities against a stable ABI. The host validates trust gates before loading and tracks isolation state per session.
- **No external runtime dependencies.** The entire Rust workspace builds with zero crates. The C++ side uses only the standard library and platform audio APIs.

## Project Structure

```
ForestFloor/
├── apps/desktop/           # Standalone desktop host
├── packages/
│   ├── abi/                # C ABI contract (language-neutral)
│   ├── abi-rs/             # Rust ABI mirror with layout assertions
│   ├── control-rs/         # Sequencer, transport, MIDI learn
│   ├── midi-rs/            # Device discovery and message parsing
│   ├── presets-rs/         # Project/kit/pattern persistence
│   ├── dsp-cpp/            # DSP kernels and signal math
│   ├── engine-cpp/         # Real-time graph host and audio callback
│   ├── diagnostics-cpp/    # Crash and runtime telemetry
│   └── plugin-host/        # SDK loader, validation, routing graph
├── docs/                   # Full specification and architecture docs
└── tools/scripts/          # Build, release, and validation scripts
```

## Quick Start

**Prerequisites:** CMake 3.28+, Ninja, a C++20 compiler, and a Rust 2021-edition toolchain.

```bash
# Clone
git clone https://github.com/KingRaver/ForestFloor.git
cd ForestFloor

# Build C++ engine + desktop host
cmake --preset macos-arm64-dev
cmake --build --preset macos-arm64-dev

# Build Rust control plane
cargo build --workspace

# Run all tests
ctest --test-dir build --output-on-failure
cargo test --workspace

# Or validate everything in one shot
./tools/scripts/dev-check.sh --clean
```

**Run the desktop host:**

```bash
open "./build/apps/desktop/Forest Floor.app"
```

For terminal logs:

```bash
"./build/apps/desktop/Forest Floor.app/Contents/MacOS/Forest Floor"
```

**Load an external plugin:**

```bash
FF_DESKTOP_PLUGIN_PATH=/path/to/plugin.dylib \
  "./build/apps/desktop/Forest Floor.app/Contents/MacOS/Forest Floor"
```

## Engine Capabilities

| Feature | Detail |
|---|---|
| Tracks | 8 tracks, polyphonic voice management per track |
| Sequencer | 16-step patterns with swing, per-step velocity and probability |
| Per-track controls | Gain, pan, filter cutoff, envelope decay, pitch (semitones), choke groups |
| MIDI | Device discovery, message parsing, learn-mode parameter binding |
| Plugins | Instruments, effects, MIDI processors — loaded via trust-gated SDK |
| Routing | Configurable signal graph with automation lanes and parameter interpolation |
| Profiling | Callback timing, XRun detection, utilization metrics (target: peak < 75%) |
| Persistence | Deterministic text formats (FF_PROJECT_V1, FF_KIT_V1, FF_PATTERN_V1) |
| Diagnostics | Timestamped crash and runtime reports written to `diagnostics/` |

## Performance Targets

- Average callback utilization: **< 50%**
- Peak callback utilization: **< 75%**
- XRun count: **0** (release blocker)

## Platform Support

| Platform | Toolchain | Status |
|---|---|---|
| macOS (Apple Silicon) | System Clang + Ninja | Primary |
| macOS (Intel) | Homebrew LLVM + Ninja | Supported |
| Windows | MSVC v143 | Supported |
| Linux | CMake + Ninja | Supported |

## Documentation

| Topic | Location |
|---|---|
| Overview | [docs/00-overview.md](docs/00-overview.md) |
| Architecture | [docs/architecture/ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md) |
| Threading & RT constraints | [docs/architecture/THREADING_AND_REALTIME.md](docs/architecture/THREADING_AND_REALTIME.md) |
| Module ownership | [docs/architecture/MODULES.md](docs/architecture/MODULES.md) |
| Plugin SDK spec | [docs/specs/PLUGIN_SDK.md](docs/specs/PLUGIN_SDK.md) |
| Parameter ID scheme | [docs/specs/PARAMETERS.md](docs/specs/PARAMETERS.md) |
| Event system | [docs/specs/EVENTS.md](docs/specs/EVENTS.md) |
| State & presets | [docs/specs/STATE_AND_PRESETS.md](docs/specs/STATE_AND_PRESETS.md) |
| Build guide | [docs/build/BUILD.md](docs/build/BUILD.md) |
| Developer workflow | [docs/development/WORKFLOW.md](docs/development/WORKFLOW.md) |
| Roadmap | [docs/project/ROADMAP.md](docs/project/ROADMAP.md) |
| Desktop icon checklist | [docs/project/ICON.MD](docs/project/ICON.MD) |
| Decision log (ADRs) | [docs/project/DECISIONS.md](docs/project/DECISIONS.md) |
| Testing strategy | [docs/quality/TESTING.md](docs/quality/TESTING.md) |
| Contributing | [docs/community/CONTRIBUTING.md](docs/community/CONTRIBUTING.md) |
| Security policy | [docs/community/SECURITY.md](docs/community/SECURITY.md) |

## Contributing

See [CONTRIBUTING.md](docs/community/CONTRIBUTING.md) for guidelines. Run the full validation suite before submitting:

```bash
./tools/scripts/dev-check.sh --clean
```

## License

See [LICENSE](LICENSE) for details.
