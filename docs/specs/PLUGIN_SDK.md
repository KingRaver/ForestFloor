# Plugin SDK Specification (V1)

## Scope
Defines the host-plugin contract for Forest Floor node modules.

## Plugin Classes
- Instrument: event in -> audio out.
- Effect: audio in -> audio out.
- MIDI processor: event in -> event out.
- Utility: analysis/routing helpers with bounded cost.

## Runtime Descriptor (Current Host Model)
Host validation currently uses:
- `PluginDescriptor`:
  - `id` (stable unique plugin identifier)
  - `name` (display name)
- `PluginBinaryInfo`:
  - `sdk_version_major`
  - `sdk_version_minor`
  - `plugin_class`
  - `entrypoints` (`create`, `prepare`, `process`, `reset`, `destroy`)
  - `runtime` flags:
    - `rt_safe_process`
    - `allows_dynamic_allocation`
    - `requests_process_isolation`
    - `has_unbounded_cpu_cost`

Reference implementation:
- `/Users/jeffspirlock/ForestFloor/packages/plugin-host/include/ff/plugin_host/host.hpp`
- `/Users/jeffspirlock/ForestFloor/packages/plugin-host/src/host.cpp`

## Binary Export Symbols (Current)
Required metadata symbols:
- `ff_plugin_get_descriptor_v1`
- `ff_plugin_get_binary_info_v1`

Required lifecycle symbols:
- `ff_create`
- `ff_prepare`
- `ff_process`
- `ff_reset`
- `ff_destroy`

Host loader behavior:
- Dynamically loads binary and resolves metadata symbols first.
- Merges declared entrypoint flags with actual symbol presence.
- Rejects plugin if required lifecycle symbols are missing after merge.
- Queues isolation-requested plugins and loads non-isolated plugins in process.

## Lifecycle Contract
1. `ff_create(host_context) -> instance`
2. `ff_prepare(instance, sample_rate_hz, max_block_size, channel_config) -> bool`
3. `ff_process(instance, frames)`
4. `ff_reset(instance)`
5. `ff_destroy(instance)`

Host runtime APIs:
- `registerInternalPlugin(...)`
- `loadPluginBinary(...)`
- `activatePlugin(...)`
- `processPlugin(...)`
- `resetPlugin(...)`
- `deactivatePlugin(...)`

## Real-Time Contract
`ff_process` must:
- Avoid allocation and blocking.
- Complete within declared CPU budget.
- Read parameters via provided snapshot handles only.

## Load Validation Policy (Current)
Hard failures:
- empty plugin `id` or `name`
- incompatible SDK major version
- missing required lifecycle entrypoints
- process callback not marked RT-safe
- process callback allows dynamic allocation

Warnings (accepted with isolation):
- plugin requests process isolation
- plugin reports unbounded CPU cost

## Sandbox Strategy (Current)
- Plugins with isolation warnings are accepted but marked `requires_isolation`.
- Host keeps isolation metadata (`isolatedPluginCount`) for scheduling/sandbox handoff.
- Host now tracks isolation queue/run states (`pendingIsolationCount`, `runningIsolationCount`) and explicit start (`startIsolationSession`).
- Current strategy focuses on deterministic admission + scheduling state; isolated execution transport can be swapped behind this API surface.

## Routing and Automation (Current)
- Routing graph:
  - `setRoute({ source_id, destination_id, gain })`
  - `removeRoute(source_id, destination_id)`
  - endpoints:
    - source: `host.input` or plugin id
    - destination: plugin id or `host.master`
- Automation lanes:
  - `addAutomationPoint(plugin_id, parameter_id, timeline_sample, normalized_value)`
  - `automationUpdatesAt(timeline_sample)` for interpolated dispatch values
- Dispatch payload:
  - `AutomationDispatch { plugin_id, ff_parameter_update_t }`

## Port Model
- Audio ports: fixed channel layouts negotiated at prepare time.
- Event ports: timestamped event streams with block offsets.
- Parameter ports: normalized scalar values with optional smoothing hints.

## State Serialization
- Plugin exposes versioned state blob.
- Host stores plugin state inside project documents.
- Plugin is responsible for migration from older internal state versions.

## Compatibility Policy
- Major SDK bump: explicit breaking changes.
- Minor SDK bump: additive fields only.
- Deprecations require at least one stable cycle before removal.
