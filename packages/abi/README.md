# Forest Floor ABI

This package defines the C ABI contract shared between the host engine, plugin
SDK boundary, and Rust control modules.

Current canonical contract header:
- `include/ff/abi/contracts.h`

Rules:
- Additive changes are allowed in minor ABI updates.
- Breaking layout changes require a major ABI version bump.
- Update mirrored definitions in `packages/abi-rs` in the same change.

