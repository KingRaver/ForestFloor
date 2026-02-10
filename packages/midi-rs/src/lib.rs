#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MidiBinding {
    pub cc: u8,
    pub parameter_id: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LearnTarget {
    TrackGain(u8),
    TrackFilterCutoff(u8),
    TrackEnvelopeDecay(u8),
}

impl LearnTarget {
    pub fn parameter_id(self) -> String {
        match self {
            LearnTarget::TrackGain(track_index) => {
                format!("engine.track.{track_index}.gain")
            }
            LearnTarget::TrackFilterCutoff(track_index) => {
                format!("engine.track.{track_index}.filter_cutoff")
            }
            LearnTarget::TrackEnvelopeDecay(track_index) => {
                format!("engine.track.{track_index}.envelope_decay")
            }
        }
    }

    pub fn parameter_numeric_id(self) -> Option<u32> {
        match self {
            LearnTarget::TrackGain(track_index) => {
                abi_rs::ff_track_parameter_id(track_index, abi_rs::FF_PARAM_SLOT_GAIN)
            }
            LearnTarget::TrackFilterCutoff(track_index) => {
                abi_rs::ff_track_parameter_id(track_index, abi_rs::FF_PARAM_SLOT_FILTER_CUTOFF)
            }
            LearnTarget::TrackEnvelopeDecay(track_index) => {
                abi_rs::ff_track_parameter_id(track_index, abi_rs::FF_PARAM_SLOT_ENVELOPE_DECAY)
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MidiMessage {
    NoteOn {
        channel: u8,
        note: u8,
        velocity: u8,
    },
    NoteOff {
        channel: u8,
        note: u8,
        velocity: u8,
    },
    ControlChange {
        channel: u8,
        controller: u8,
        value: u8,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PadTrigger {
    pub track_index: u8,
    pub velocity: u8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NoteMap {
    note_to_track: [Option<u8>; 128],
    track_count: u8,
}

impl NoteMap {
    pub fn new(track_count: u8) -> Self {
        Self {
            note_to_track: [None; 128],
            track_count: track_count.max(1),
        }
    }

    pub fn bind_note(&mut self, note: u8, track_index: u8) -> bool {
        if !is_midi_data_byte(note) || track_index >= self.track_count {
            return false;
        }

        self.note_to_track[note as usize] = Some(track_index);
        true
    }

    pub fn resolve_track(&self, note: u8) -> Option<u8> {
        if !is_midi_data_byte(note) {
            return None;
        }

        self.note_to_track[note as usize]
    }
}

impl Default for NoteMap {
    fn default() -> Self {
        Self::new(8)
    }
}

#[derive(Debug, Default)]
pub struct MappingProfile {
    bindings: Vec<MidiBinding>,
    learn_target: Option<LearnTarget>,
}

impl MappingProfile {
    pub fn bind_cc(&mut self, cc: u8, parameter_id: impl Into<String>) {
        if let Some(existing) = self.bindings.iter_mut().find(|binding| binding.cc == cc) {
            existing.parameter_id = parameter_id.into();
            return;
        }

        self.bindings.push(MidiBinding {
            cc,
            parameter_id: parameter_id.into(),
        });
    }

    pub fn resolve_cc(&self, cc: u8) -> Option<&str> {
        self.bindings
            .iter()
            .find(|binding| binding.cc == cc)
            .map(|binding| binding.parameter_id.as_str())
    }

    pub fn begin_learn(&mut self, target: LearnTarget) {
        self.learn_target = Some(target);
    }

    pub fn cancel_learn(&mut self) {
        self.learn_target = None;
    }

    pub fn active_learn_target(&self) -> Option<LearnTarget> {
        self.learn_target
    }

    pub fn handle_message_for_learn(&mut self, message: MidiMessage) -> Option<MidiBinding> {
        let target = self.learn_target?;
        if let MidiMessage::ControlChange { controller, .. } = message {
            let parameter_id = target.parameter_id();
            self.bind_cc(controller, parameter_id.clone());
            self.learn_target = None;
            return Some(MidiBinding {
                cc: controller,
                parameter_id,
            });
        }

        None
    }
}

pub fn parse_midi_message(bytes: &[u8]) -> Option<MidiMessage> {
    if bytes.len() < 3 {
        return None;
    }

    let status = bytes[0];
    let data1 = bytes[1];
    let data2 = bytes[2];
    if !is_midi_data_byte(data1) || !is_midi_data_byte(data2) {
        return None;
    }

    let message_type = status & 0xF0;
    let channel = status & 0x0F;

    match message_type {
        0x80 => Some(MidiMessage::NoteOff {
            channel,
            note: data1,
            velocity: data2,
        }),
        0x90 if data2 == 0 => Some(MidiMessage::NoteOff {
            channel,
            note: data1,
            velocity: data2,
        }),
        0x90 => Some(MidiMessage::NoteOn {
            channel,
            note: data1,
            velocity: data2,
        }),
        0xB0 => Some(MidiMessage::ControlChange {
            channel,
            controller: data1,
            value: data2,
        }),
        _ => None,
    }
}

fn is_midi_data_byte(value: u8) -> bool {
    value <= 0x7F
}

pub fn note_on_to_pad_trigger(note_map: &NoteMap, note: u8, velocity: u8) -> Option<PadTrigger> {
    if velocity == 0 {
        return None;
    }

    note_map.resolve_track(note).map(|track_index| PadTrigger {
        track_index,
        velocity,
    })
}

#[cfg(test)]
mod tests {
    use super::{
        note_on_to_pad_trigger, parse_midi_message, LearnTarget, MappingProfile, MidiMessage,
        NoteMap,
    };

    #[test]
    fn bind_cc_replaces_existing_mapping() {
        let mut profile = MappingProfile::default();
        profile.bind_cc(74, "filter.cutoff");
        profile.bind_cc(74, "filter.drive");

        assert_eq!(profile.resolve_cc(74), Some("filter.drive"));
    }

    #[test]
    fn note_map_binds_notes_to_tracks() {
        let mut note_map = NoteMap::new(8);
        assert!(note_map.bind_note(36, 0));
        assert!(note_map.bind_note(43, 7));
        assert_eq!(note_map.resolve_track(36), Some(0));
        assert_eq!(note_map.resolve_track(43), Some(7));
    }

    #[test]
    fn note_map_rejects_out_of_range_tracks() {
        let mut note_map = NoteMap::new(8);
        assert!(!note_map.bind_note(48, 8));
    }

    #[test]
    fn note_map_rejects_out_of_range_notes() {
        let mut note_map = NoteMap::new(8);
        assert!(!note_map.bind_note(200, 0));
        assert_eq!(note_map.resolve_track(200), None);
    }

    #[test]
    fn parse_note_on_and_control_change_messages() {
        assert_eq!(
            parse_midi_message(&[0x90, 36, 127]),
            Some(MidiMessage::NoteOn {
                channel: 0,
                note: 36,
                velocity: 127,
            })
        );
        assert_eq!(
            parse_midi_message(&[0xB3, 74, 99]),
            Some(MidiMessage::ControlChange {
                channel: 3,
                controller: 74,
                value: 99,
            })
        );
    }

    #[test]
    fn parse_rejects_invalid_data_bytes() {
        assert_eq!(parse_midi_message(&[0x90, 200, 127]), None);
        assert_eq!(parse_midi_message(&[0xB0, 74, 200]), None);
    }

    #[test]
    fn map_note_on_to_pad_trigger() {
        let mut note_map = NoteMap::new(8);
        assert!(note_map.bind_note(38, 2));

        let trigger = note_on_to_pad_trigger(&note_map, 38, 100).expect("trigger should exist");
        assert_eq!(trigger.track_index, 2);
        assert_eq!(trigger.velocity, 100);
        assert_eq!(note_on_to_pad_trigger(&note_map, 38, 0), None);
    }

    #[test]
    fn midi_learn_binds_first_control_change() {
        let mut profile = MappingProfile::default();
        profile.begin_learn(LearnTarget::TrackGain(2));

        let learned = profile
            .handle_message_for_learn(MidiMessage::ControlChange {
                channel: 0,
                controller: 21,
                value: 80,
            })
            .expect("learn should produce binding");

        assert_eq!(learned.cc, 21);
        assert_eq!(learned.parameter_id, "engine.track.2.gain");
        assert_eq!(profile.resolve_cc(21), Some("engine.track.2.gain"));
        assert_eq!(profile.active_learn_target(), None);
    }

    #[test]
    fn midi_learn_ignores_non_control_messages() {
        let mut profile = MappingProfile::default();
        profile.begin_learn(LearnTarget::TrackFilterCutoff(1));

        assert_eq!(
            profile.handle_message_for_learn(MidiMessage::NoteOn {
                channel: 0,
                note: 36,
                velocity: 100,
            }),
            None
        );
        assert_eq!(
            profile.active_learn_target(),
            Some(LearnTarget::TrackFilterCutoff(1))
        );
    }

    #[test]
    fn learn_target_maps_to_expected_parameter_ids() {
        assert_eq!(
            LearnTarget::TrackGain(0).parameter_id(),
            "engine.track.0.gain"
        );
        assert_eq!(
            LearnTarget::TrackFilterCutoff(3).parameter_id(),
            "engine.track.3.filter_cutoff"
        );
        assert_eq!(
            LearnTarget::TrackEnvelopeDecay(7).parameter_id(),
            "engine.track.7.envelope_decay"
        );
    }

    #[test]
    fn learn_target_maps_to_expected_numeric_ids() {
        assert_eq!(
            LearnTarget::TrackGain(0).parameter_numeric_id(),
            Some(0x1001)
        );
        assert_eq!(
            LearnTarget::TrackFilterCutoff(3).parameter_numeric_id(),
            Some(0x1033)
        );
        assert_eq!(
            LearnTarget::TrackEnvelopeDecay(7).parameter_numeric_id(),
            Some(0x1074)
        );
    }
}
