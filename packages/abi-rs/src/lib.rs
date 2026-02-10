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
        ff_track_parameter_id, FfEvent, FfEventPayload, FfNoteEvent, FfParameterUpdate,
        FfTriggerEvent, FF_PARAM_SLOT_CHOKE_GROUP, FF_PARAM_SLOT_GAIN,
    };
    use std::mem::{align_of, offset_of, size_of};

    #[test]
    fn note_event_layout_is_stable() {
        assert_eq!(size_of::<FfNoteEvent>(), 8);
        assert_eq!(align_of::<FfNoteEvent>(), 4);
        assert_eq!(offset_of!(FfNoteEvent, track_index), 0);
        assert_eq!(offset_of!(FfNoteEvent, note), 1);
        assert_eq!(offset_of!(FfNoteEvent, reserved), 2);
        assert_eq!(offset_of!(FfNoteEvent, velocity), 4);
    }

    #[test]
    fn trigger_event_layout_is_stable() {
        assert_eq!(size_of::<FfTriggerEvent>(), 8);
        assert_eq!(align_of::<FfTriggerEvent>(), 4);
        assert_eq!(offset_of!(FfTriggerEvent, track_index), 0);
        assert_eq!(offset_of!(FfTriggerEvent, step_index), 1);
        assert_eq!(offset_of!(FfTriggerEvent, reserved), 2);
        assert_eq!(offset_of!(FfTriggerEvent, velocity), 4);
    }

    #[test]
    fn event_payload_layout_is_stable() {
        assert_eq!(size_of::<FfEventPayload>(), 8);
        assert_eq!(align_of::<FfEventPayload>(), 4);
    }

    #[test]
    fn event_layout_is_stable() {
        assert_eq!(size_of::<FfEvent>(), 32);
        assert_eq!(align_of::<FfEvent>(), 8);
        assert_eq!(offset_of!(FfEvent, timeline_sample), 0);
        assert_eq!(offset_of!(FfEvent, block_offset), 8);
        assert_eq!(offset_of!(FfEvent, source_id), 12);
        assert_eq!(offset_of!(FfEvent, reserved), 14);
        assert_eq!(offset_of!(FfEvent, event_type), 16);
        assert_eq!(offset_of!(FfEvent, payload), 20);
    }

    #[test]
    fn parameter_update_layout_is_stable() {
        assert_eq!(size_of::<FfParameterUpdate>(), 16);
        assert_eq!(align_of::<FfParameterUpdate>(), 4);
        assert_eq!(offset_of!(FfParameterUpdate, parameter_id), 0);
        assert_eq!(offset_of!(FfParameterUpdate, normalized_value), 4);
        assert_eq!(offset_of!(FfParameterUpdate, ramp_samples), 8);
        assert_eq!(offset_of!(FfParameterUpdate, reserved), 12);
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
