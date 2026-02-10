pub const FF_ABI_VERSION_MAJOR: u32 = 1;
pub const FF_ABI_VERSION_MINOR: u32 = 0;

pub const FF_EVENT_TYPE_NOTE_ON: u32 = 1;
pub const FF_EVENT_TYPE_NOTE_OFF: u32 = 2;
pub const FF_EVENT_TYPE_TRIGGER: u32 = 3;
pub const FF_EVENT_TYPE_TRANSPORT_START: u32 = 4;
pub const FF_EVENT_TYPE_TRANSPORT_STOP: u32 = 5;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FfNoteEvent {
    pub track_index: u8,
    pub note: u8,
    pub reserved: u16,
    pub velocity: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FfTriggerEvent {
    pub track_index: u8,
    pub step_index: u8,
    pub reserved: u16,
    pub velocity: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FfTransportEvent {
    pub bpm: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union FfEventPayload {
    pub note: FfNoteEvent,
    pub trigger: FfTriggerEvent,
    pub transport: FfTransportEvent,
}

impl Default for FfEventPayload {
    fn default() -> Self {
        Self {
            transport: FfTransportEvent { bpm: 120.0 },
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FfEvent {
    pub timeline_sample: u64,
    pub block_offset: u32,
    pub source_id: u16,
    pub reserved: u16,
    pub event_type: u32,
    pub payload: FfEventPayload,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FfParameterUpdate {
    pub parameter_id: u32,
    pub normalized_value: f32,
    pub ramp_samples: u32,
    pub reserved: u32,
}

#[cfg(test)]
mod tests {
    use super::{FfEvent, FfParameterUpdate};

    #[test]
    fn parameter_update_layout_is_16_bytes() {
        assert_eq!(std::mem::size_of::<FfParameterUpdate>(), 16);
    }

    #[test]
    fn event_layout_is_nonzero() {
        assert!(std::mem::size_of::<FfEvent>() >= 24);
    }
}

