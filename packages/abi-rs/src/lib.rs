pub const FF_ABI_VERSION_MAJOR: u32 = 1;
pub const FF_ABI_VERSION_MINOR: u32 = 0;

pub const FF_PARAM_TRACK_BASE: u32 = 0x1000;
pub const FF_PARAM_TRACK_STRIDE: u32 = 0x10;

pub const FF_PARAM_SLOT_GAIN: u32 = 1;
pub const FF_PARAM_SLOT_PAN: u32 = 2;
pub const FF_PARAM_SLOT_FILTER_CUTOFF: u32 = 3;
pub const FF_PARAM_SLOT_ENVELOPE_DECAY: u32 = 4;
pub const FF_PARAM_SLOT_PITCH: u32 = 5;
pub const FF_PARAM_SLOT_CHOKE_GROUP: u32 = 6;

pub const FF_EVENT_TYPE_NOTE_ON: u32 = 1;
pub const FF_EVENT_TYPE_NOTE_OFF: u32 = 2;
pub const FF_EVENT_TYPE_TRIGGER: u32 = 3;
pub const FF_EVENT_TYPE_TRANSPORT_START: u32 = 4;
pub const FF_EVENT_TYPE_TRANSPORT_STOP: u32 = 5;

pub fn ff_track_parameter_id(track_index: u8, parameter_slot: u32) -> Option<u32> {
    if usize::from(track_index) >= 8 {
        return None;
    }

    if !(FF_PARAM_SLOT_GAIN..=FF_PARAM_SLOT_CHOKE_GROUP).contains(&parameter_slot) {
        return None;
    }

    Some(FF_PARAM_TRACK_BASE + (u32::from(track_index) * FF_PARAM_TRACK_STRIDE) + parameter_slot)
}

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
    use super::{
        ff_track_parameter_id, FfEvent, FfParameterUpdate, FF_PARAM_SLOT_CHOKE_GROUP,
        FF_PARAM_SLOT_GAIN,
    };

    #[test]
    fn parameter_update_layout_is_16_bytes() {
        assert_eq!(std::mem::size_of::<FfParameterUpdate>(), 16);
    }

    #[test]
    fn event_layout_is_nonzero() {
        assert!(std::mem::size_of::<FfEvent>() >= 24);
    }

    #[test]
    fn track_parameter_id_is_stable() {
        assert_eq!(ff_track_parameter_id(0, FF_PARAM_SLOT_GAIN), Some(0x1001));
        assert_eq!(
            ff_track_parameter_id(7, FF_PARAM_SLOT_CHOKE_GROUP),
            Some(0x1076)
        );
        assert_eq!(ff_track_parameter_id(8, FF_PARAM_SLOT_GAIN), None);
    }
}
