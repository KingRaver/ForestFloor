# Plugin SDK Specification (Draft)

## Scope
Defines the host-plugin contract for Forest Floor node modules.

## Plugin Classes
- Instrument: event in -> audio out.
- Effect: audio in -> audio out.
- MIDI processor: event in -> event out.
- Utility: analysis/routing helpers with bounded cost.

## ABI Strategy
- C ABI boundary with versioned structs.
- Explicit `sdk_version` negotiation during load.
- Host rejects plugins with incompatible major ABI.

## Lifecycle Contract
1. `ff_create(context)`
2. `ff_prepare(sample_rate, max_block_size, channel_config)`
3. `ff_process(process_context)`
4. `ff_reset()`
5. `ff_destroy()`

## Real-Time Contract
`ff_process` must:
- Avoid allocation and blocking.
- Complete within declared CPU budget.
- Read parameters via provided snapshot handles only.

## Port Model
- Audio ports: fixed channel layouts negotiated at prepare time.
- Event ports: timestamped event streams with block offsets.
- Parameter ports: normalized scalar values with optional smoothing hints.

## State Serialization
- Plugin exposes versioned state blob.
- Host stores plugin state inside project documents.
- Plugin is responsible for migration from older internal state versions.

## Validation Requirements
Host-side validation on load:
- ABI compatibility.
- Required symbols.
- Declared capabilities.
- RT safety flags and limits.

## Compatibility Policy
- Major SDK bump: explicit breaking changes.
- Minor SDK bump: additive fields only.
- Deprecations require at least one stable cycle before removal.
