pub const TRACK_COUNT: usize = 8;
pub const STEPS_PER_PATTERN: usize = 16;
pub const DEFAULT_BPM: f32 = 120.0;
pub const MIN_BPM: f32 = 20.0;
pub const MAX_BPM: f32 = 300.0;
pub const MAX_SWING: f32 = 0.45;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Step {
    pub active: bool,
    pub velocity: u8,
}

impl Default for Step {
    fn default() -> Self {
        Self {
            active: false,
            velocity: 100,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Pattern {
    tracks: [[Step; STEPS_PER_PATTERN]; TRACK_COUNT],
}

impl Default for Pattern {
    fn default() -> Self {
        Self {
            tracks: [[Step::default(); STEPS_PER_PATTERN]; TRACK_COUNT],
        }
    }
}

impl Pattern {
    pub fn set_step(&mut self, track_index: usize, step_index: usize, step: Step) -> bool {
        if track_index >= TRACK_COUNT || step_index >= STEPS_PER_PATTERN {
            return false;
        }

        self.tracks[track_index][step_index] = step;
        true
    }

    pub fn step(&self, track_index: usize, step_index: usize) -> Option<Step> {
        if track_index >= TRACK_COUNT || step_index >= STEPS_PER_PATTERN {
            return None;
        }

        Some(self.tracks[track_index][step_index])
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Transport {
    bpm: f32,
    is_playing: bool,
}

impl Default for Transport {
    fn default() -> Self {
        Self {
            bpm: DEFAULT_BPM,
            is_playing: false,
        }
    }
}

impl Transport {
    pub fn bpm(&self) -> f32 {
        self.bpm
    }

    pub fn is_playing(&self) -> bool {
        self.is_playing
    }

    pub fn set_bpm(&mut self, bpm: f32) {
        self.bpm = bpm.clamp(MIN_BPM, MAX_BPM);
    }

    pub fn start(&mut self) {
        self.is_playing = true;
    }

    pub fn stop(&mut self) {
        self.is_playing = false;
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StepTriggerEvent {
    pub track_index: u8,
    pub step_index: u8,
    pub velocity: u8,
    pub choke_group: Option<u8>,
    pub timeline_sample: u64,
    pub block_offset: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct TrackPerformance {
    pub choke_group: Option<u8>,
}

#[derive(Debug)]
pub struct Sequencer {
    sample_rate_hz: u32,
    transport: Transport,
    pattern: Pattern,
    swing: f32,
    track_performance: [TrackPerformance; TRACK_COUNT],
    current_step: usize,
    samples_to_next_step: f64,
    timeline_sample: u64,
    emit_step_on_next_process: bool,
}

impl Sequencer {
    pub fn new(sample_rate_hz: u32) -> Self {
        let sample_rate_hz = sample_rate_hz.max(1);
        let transport = Transport::default();
        let samples_to_next_step = samples_per_step(sample_rate_hz, transport.bpm());

        Self {
            sample_rate_hz,
            transport,
            pattern: Pattern::default(),
            swing: 0.0,
            track_performance: [TrackPerformance::default(); TRACK_COUNT],
            current_step: 0,
            samples_to_next_step,
            timeline_sample: 0,
            emit_step_on_next_process: false,
        }
    }

    pub fn transport(&self) -> Transport {
        self.transport
    }

    pub fn set_tempo_bpm(&mut self, bpm: f32) {
        self.transport.set_bpm(bpm);
        self.samples_to_next_step = self
            .samples_to_next_step
            .min(self.step_interval_samples(self.current_step));
    }

    pub fn set_swing(&mut self, swing: f32) {
        self.swing = swing.clamp(0.0, MAX_SWING);
        self.samples_to_next_step = self
            .samples_to_next_step
            .min(self.step_interval_samples(self.current_step));
    }

    pub fn swing(&self) -> f32 {
        self.swing
    }

    pub fn set_track_choke_group(&mut self, track_index: usize, choke_group: Option<u8>) -> bool {
        if track_index >= TRACK_COUNT {
            return false;
        }

        self.track_performance[track_index].choke_group = choke_group;
        true
    }

    pub fn start(&mut self) {
        if !self.transport.is_playing() {
            self.transport.start();
            self.emit_step_on_next_process = true;
        }
    }

    pub fn stop(&mut self) {
        self.transport.stop();
        self.emit_step_on_next_process = false;
    }

    pub fn reset(&mut self) {
        self.current_step = 0;
        self.timeline_sample = 0;
        self.samples_to_next_step = self.step_interval_samples(self.current_step);
        self.emit_step_on_next_process = false;
    }

    pub fn pattern(&self) -> &Pattern {
        &self.pattern
    }

    pub fn pattern_mut(&mut self) -> &mut Pattern {
        &mut self.pattern
    }

    pub fn process_block(&mut self, frames: u32) -> Vec<StepTriggerEvent> {
        if frames == 0 || !self.transport.is_playing() {
            return Vec::new();
        }

        let mut events = Vec::new();
        if self.emit_step_on_next_process {
            self.collect_step_events(self.current_step, 0, self.timeline_sample, &mut events);
            self.emit_step_on_next_process = false;
            self.samples_to_next_step = self.step_interval_samples(self.current_step);
        }

        let mut remaining = f64::from(frames);
        let mut consumed = 0.0;
        while remaining > 0.0 {
            if self.samples_to_next_step <= remaining + f64::EPSILON {
                let step_advance = self.samples_to_next_step.max(0.0);
                consumed += step_advance;
                remaining -= step_advance;

                let offset = consumed.round() as u32;
                self.current_step = (self.current_step + 1) % STEPS_PER_PATTERN;
                self.collect_step_events(
                    self.current_step,
                    offset,
                    self.timeline_sample + u64::from(offset),
                    &mut events,
                );
                self.samples_to_next_step = self.step_interval_samples(self.current_step);
            } else {
                self.samples_to_next_step -= remaining;
                remaining = 0.0;
            }
        }

        self.timeline_sample += u64::from(frames);
        events
    }

    fn collect_step_events(
        &self,
        step_index: usize,
        block_offset: u32,
        timeline_sample: u64,
        output: &mut Vec<StepTriggerEvent>,
    ) {
        for track_index in 0..TRACK_COUNT {
            let step = self.pattern.tracks[track_index][step_index];
            if step.active {
                output.push(StepTriggerEvent {
                    track_index: track_index as u8,
                    step_index: step_index as u8,
                    velocity: step.velocity,
                    choke_group: self.track_performance[track_index].choke_group,
                    timeline_sample,
                    block_offset,
                });
            }
        }
    }

    fn step_interval_samples(&self, step_index: usize) -> f64 {
        let base = samples_per_step(self.sample_rate_hz, self.transport.bpm());
        if self.swing <= f32::EPSILON {
            return base;
        }

        let swing = f64::from(self.swing);
        if step_index % 2 == 0 {
            base * (1.0 + swing)
        } else {
            base * (1.0 - swing)
        }
    }
}

fn samples_per_step(sample_rate_hz: u32, bpm: f32) -> f64 {
    let safe_bpm = bpm.clamp(MIN_BPM, MAX_BPM);
    f64::from(sample_rate_hz) * 60.0 / f64::from(safe_bpm) / 4.0
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct TrackRecall {
    pub sample_id: Option<String>,
    pub choke_group: Option<u8>,
    pub gain_normalized: u8,
    pub pan_normalized: u8,
    pub filter_cutoff_normalized: u8,
    pub envelope_decay_normalized: u8,
    pub pitch_normalized: u8,
}

#[derive(Debug)]
pub struct RecallState {
    sequencer: Sequencer,
    track_recall: [TrackRecall; TRACK_COUNT],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TrackSampleAssignment {
    pub track_index: u8,
    pub sample_id: String,
}

#[derive(Clone, Debug, PartialEq)]
pub struct EngineRecall {
    pub sample_assignments: Vec<TrackSampleAssignment>,
    pub parameter_updates: Vec<abi_rs::FfParameterUpdate>,
}

impl RecallState {
    pub fn sequencer(&self) -> &Sequencer {
        &self.sequencer
    }

    pub fn sequencer_mut(&mut self) -> &mut Sequencer {
        &mut self.sequencer
    }

    pub fn track_recall(&self, track_index: usize) -> Option<&TrackRecall> {
        self.track_recall.get(track_index)
    }

    pub fn to_engine_recall(&self) -> EngineRecall {
        let mut sample_assignments = Vec::new();
        let mut parameter_updates = Vec::with_capacity(TRACK_COUNT * 6);

        for (track_index, track_recall) in self.track_recall.iter().enumerate() {
            let track_index = track_index as u8;
            if let Some(sample_id) = &track_recall.sample_id {
                sample_assignments.push(TrackSampleAssignment {
                    track_index,
                    sample_id: sample_id.clone(),
                });
            }

            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_GAIN,
                normalized_from_u7(track_recall.gain_normalized),
            );
            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_PAN,
                normalized_from_u7(track_recall.pan_normalized),
            );
            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_FILTER_CUTOFF,
                normalized_from_u7(track_recall.filter_cutoff_normalized),
            );
            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_ENVELOPE_DECAY,
                normalized_from_u7(track_recall.envelope_decay_normalized),
            );
            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_PITCH,
                normalized_from_u7(track_recall.pitch_normalized),
            );
            push_parameter_update(
                &mut parameter_updates,
                track_index,
                abi_rs::FF_PARAM_SLOT_CHOKE_GROUP,
                normalize_choke_group_for_engine(track_recall.choke_group),
            );
        }

        EngineRecall {
            sample_assignments,
            parameter_updates,
        }
    }
}

fn normalize_unit(value: f32) -> u8 {
    let clamped = value.clamp(0.0, 1.0);
    (clamped * 127.0).round() as u8
}

fn normalize_pan(value: f32) -> u8 {
    let clamped = value.clamp(-1.0, 1.0);
    let normalized = (clamped + 1.0) * 0.5;
    (normalized * 127.0).round() as u8
}

fn normalize_pitch(value: f32) -> u8 {
    let clamped = value.clamp(-24.0, 24.0);
    let normalized = (clamped + 24.0) / 48.0;
    (normalized * 127.0).round() as u8
}

fn normalized_from_u7(value: u8) -> f32 {
    f32::from(value) / 127.0
}

fn normalize_choke_group_for_engine(choke_group: Option<u8>) -> f32 {
    match choke_group {
        Some(value) => (f32::from(value.min(15)) + 1.0) / 16.0,
        None => 0.0,
    }
}

fn push_parameter_update(
    output: &mut Vec<abi_rs::FfParameterUpdate>,
    track_index: u8,
    parameter_slot: u32,
    normalized_value: f32,
) {
    if let Some(parameter_id) = abi_rs::ff_track_parameter_id(track_index, parameter_slot) {
        output.push(abi_rs::FfParameterUpdate {
            parameter_id,
            normalized_value: normalized_value.clamp(0.0, 1.0),
            ramp_samples: 0,
            reserved: 0,
        });
    }
}

pub fn recall_state_from_project(
    project: &presets_rs::Project,
    sample_rate_hz: u32,
) -> Result<RecallState, String> {
    let kit_index = project
        .active_kit
        .or_else(|| (!project.kits.is_empty()).then_some(0))
        .ok_or_else(|| "project has no kits".to_string())?;
    if kit_index >= project.kits.len() {
        return Err(format!("active kit out of range: {kit_index}"));
    }

    let pattern_index = project
        .active_pattern
        .or_else(|| (!project.patterns.is_empty()).then_some(0))
        .ok_or_else(|| "project has no patterns".to_string())?;
    if pattern_index >= project.patterns.len() {
        return Err(format!("active pattern out of range: {pattern_index}"));
    }

    let kit = &project.kits[kit_index];
    let pattern = &project.patterns[pattern_index];

    let mut sequencer = Sequencer::new(sample_rate_hz);
    sequencer.set_swing(pattern.swing);

    for track_index in 0..TRACK_COUNT {
        for step_index in 0..STEPS_PER_PATTERN {
            let step = pattern.steps[track_index][step_index];
            if !sequencer.pattern_mut().set_step(
                track_index,
                step_index,
                Step {
                    active: step.active,
                    velocity: step.velocity,
                },
            ) {
                return Err(format!(
                    "failed to apply pattern step track={track_index}, step={step_index}"
                ));
            }
        }
    }

    let mut track_recall = std::array::from_fn(|_| TrackRecall::default());
    for assignment in &kit.tracks {
        let track_index = usize::from(assignment.track_index);
        if track_index >= TRACK_COUNT {
            return Err(format!(
                "kit track assignment out of range: {}",
                assignment.track_index
            ));
        }
        track_recall[track_index].sample_id = Some(assignment.sample_id.clone());
    }

    for control in &kit.controls {
        let track_index = usize::from(control.track_index);
        if track_index >= TRACK_COUNT {
            return Err(format!(
                "kit control track out of range: {}",
                control.track_index
            ));
        }

        track_recall[track_index].choke_group = control.controls.choke_group;
        track_recall[track_index].gain_normalized = normalize_unit(control.controls.gain);
        track_recall[track_index].pan_normalized = normalize_pan(control.controls.pan);
        track_recall[track_index].filter_cutoff_normalized =
            normalize_unit(control.controls.filter_cutoff);
        track_recall[track_index].envelope_decay_normalized =
            normalize_unit(control.controls.envelope_decay);
        track_recall[track_index].pitch_normalized =
            normalize_pitch(control.controls.pitch_semitones);

        if !sequencer.set_track_choke_group(track_index, control.controls.choke_group) {
            return Err(format!(
                "failed to apply choke group to track {track_index}"
            ));
        }
    }

    Ok(RecallState {
        sequencer,
        track_recall,
    })
}

pub fn render_recall_events(
    project: &presets_rs::Project,
    sample_rate_hz: u32,
    blocks: &[u32],
) -> Result<Vec<StepTriggerEvent>, String> {
    let mut recall = recall_state_from_project(project, sample_rate_hz)?;
    let mut events = Vec::new();
    recall.sequencer_mut().start();
    for frames in blocks {
        events.extend(recall.sequencer_mut().process_block(*frames));
    }
    Ok(events)
}

pub fn engine_recall_from_project(
    project: &presets_rs::Project,
    sample_rate_hz: u32,
) -> Result<EngineRecall, String> {
    let recall = recall_state_from_project(project, sample_rate_hz)?;
    Ok(recall.to_engine_recall())
}

#[cfg(test)]
mod tests {
    use abi_rs::{
        ff_track_parameter_id, FF_PARAM_SLOT_CHOKE_GROUP, FF_PARAM_SLOT_GAIN, FF_PARAM_SLOT_PAN,
    };
    use presets_rs::{
        load_project_from_text, save_project_to_text, Kit, Pattern as PresetPattern, PatternStep,
        Project, TrackAssignment, TrackControls,
    };

    use super::{
        engine_recall_from_project, recall_state_from_project, render_recall_events, Pattern,
        Sequencer, Step, Transport, DEFAULT_BPM, MAX_BPM, MAX_SWING, MIN_BPM, STEPS_PER_PATTERN,
        TRACK_COUNT,
    };

    #[test]
    fn pattern_supports_eight_tracks_and_sixteen_steps() {
        let mut pattern = Pattern::default();
        assert!(pattern.set_step(
            TRACK_COUNT - 1,
            STEPS_PER_PATTERN - 1,
            Step {
                active: true,
                velocity: 127,
            },
        ));
        assert!(
            pattern
                .step(TRACK_COUNT - 1, STEPS_PER_PATTERN - 1)
                .expect("step should exist")
                .active
        );
        assert!(!pattern.set_step(
            TRACK_COUNT,
            0,
            Step {
                active: true,
                velocity: 100,
            },
        ));
    }

    #[test]
    fn transport_clamps_tempo() {
        let mut transport = Transport::default();
        transport.set_bpm(9999.0);
        assert_eq!(transport.bpm(), MAX_BPM);
        transport.set_bpm(1.0);
        assert_eq!(transport.bpm(), MIN_BPM);
        transport.set_bpm(DEFAULT_BPM);
        assert_eq!(transport.bpm(), DEFAULT_BPM);
    }

    #[test]
    fn sequencer_emits_step_zero_immediately_on_start() {
        let mut sequencer = Sequencer::new(48_000);
        assert!(sequencer.pattern_mut().set_step(
            0,
            0,
            Step {
                active: true,
                velocity: 120,
            },
        ));
        sequencer.start();

        let events = sequencer.process_block(128);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].track_index, 0);
        assert_eq!(events[0].step_index, 0);
        assert_eq!(events[0].choke_group, None);
        assert_eq!(events[0].block_offset, 0);
    }

    #[test]
    fn sequencer_emits_multi_track_step_events() {
        let mut sequencer = Sequencer::new(48_000);
        assert!(sequencer.pattern_mut().set_step(
            1,
            5,
            Step {
                active: true,
                velocity: 90,
            },
        ));
        assert!(sequencer.pattern_mut().set_step(
            3,
            5,
            Step {
                active: true,
                velocity: 110,
            },
        ));

        sequencer.start();
        let events = sequencer.process_block(30_000);
        let step_five_events: Vec<_> = events
            .iter()
            .filter(|event| event.step_index == 5)
            .collect();
        assert_eq!(step_five_events.len(), 2);
        assert!(step_five_events.iter().any(|event| event.track_index == 1));
        assert!(step_five_events.iter().any(|event| event.track_index == 3));
    }

    #[test]
    fn sequencer_wraps_after_sixteen_steps() {
        let mut sequencer = Sequencer::new(48_000);
        assert!(sequencer.pattern_mut().set_step(
            2,
            0,
            Step {
                active: true,
                velocity: 127,
            },
        ));
        sequencer.start();

        let first_bar = sequencer.process_block(96_000);
        let second_bar = sequencer.process_block(96_000);

        assert!(first_bar
            .iter()
            .any(|event| event.step_index == 0 && event.track_index == 2));
        assert!(second_bar
            .iter()
            .any(|event| event.step_index == 0 && event.track_index == 2));
    }

    #[test]
    fn swing_delays_offbeat_steps() {
        let mut sequencer = Sequencer::new(48_000);
        sequencer.set_swing(0.4);
        assert!(sequencer.pattern_mut().set_step(
            0,
            1,
            Step {
                active: true,
                velocity: 110,
            },
        ));
        sequencer.start();

        let events = sequencer.process_block(9_000);
        let offbeat = events
            .iter()
            .find(|event| event.step_index == 1)
            .expect("step 1 event should exist");
        assert_eq!(offbeat.block_offset, 8_400);
    }

    #[test]
    fn swing_is_clamped() {
        let mut sequencer = Sequencer::new(48_000);
        sequencer.set_swing(1.0);
        assert_eq!(sequencer.swing(), MAX_SWING);
    }

    #[test]
    fn choke_group_is_carried_in_step_events() {
        let mut sequencer = Sequencer::new(48_000);
        assert!(sequencer.set_track_choke_group(3, Some(1)));
        assert!(sequencer.pattern_mut().set_step(
            3,
            0,
            Step {
                active: true,
                velocity: 127,
            },
        ));
        sequencer.start();

        let events = sequencer.process_block(64);
        let event = events
            .iter()
            .find(|value| value.track_index == 3)
            .expect("track 3 event should exist");
        assert_eq!(event.choke_group, Some(1));
    }

    #[test]
    fn recall_state_maps_project_data_to_runtime_shape() {
        let mut project = Project {
            name: "phase2-map".to_string(),
            kits: vec![Kit::default()],
            active_kit: Some(0),
            patterns: vec![PresetPattern::default()],
            active_pattern: Some(0),
        };
        project.kits[0].add_assignment(TrackAssignment {
            track_index: 4,
            sample_id: "hihat-open".to_string(),
        });
        project.kits[0].set_track_controls(
            4,
            TrackControls {
                gain: 0.75,
                pan: -0.5,
                filter_cutoff: 0.35,
                envelope_decay: 0.65,
                pitch_semitones: 12.0,
                choke_group: Some(2),
            },
        );
        project.patterns[0].set_swing(0.25);
        project.patterns[0].set_step(
            4,
            0,
            PatternStep {
                active: true,
                velocity: 118,
            },
        );

        let recall = recall_state_from_project(&project, 48_000).expect("recall should map");
        let track = recall.track_recall(4).expect("track 4 should exist");
        assert_eq!(track.sample_id.as_deref(), Some("hihat-open"));
        assert_eq!(track.choke_group, Some(2));
        assert!(track.gain_normalized > 90);
        assert!(track.pan_normalized < 64);
        assert!(track.pitch_normalized > 90);
    }

    #[test]
    fn recall_state_maps_to_engine_recall_payload() {
        let mut project = Project {
            name: "phase2-engine-recall".to_string(),
            kits: vec![Kit::default()],
            active_kit: Some(0),
            patterns: vec![PresetPattern::default()],
            active_pattern: Some(0),
        };
        project.kits[0].add_assignment(TrackAssignment {
            track_index: 2,
            sample_id: "snare-01".to_string(),
        });
        project.kits[0].set_track_controls(
            2,
            TrackControls {
                gain: 0.5,
                pan: -0.25,
                filter_cutoff: 0.7,
                envelope_decay: 0.9,
                pitch_semitones: -12.0,
                choke_group: Some(3),
            },
        );

        let recall = engine_recall_from_project(&project, 48_000).expect("recall should map");
        assert_eq!(
            recall.sample_assignments[0].sample_id, "snare-01",
            "sample assignment should be preserved"
        );
        assert_eq!(recall.sample_assignments[0].track_index, 2);

        let gain_id = ff_track_parameter_id(2, FF_PARAM_SLOT_GAIN).expect("id should exist");
        let pan_id = ff_track_parameter_id(2, FF_PARAM_SLOT_PAN).expect("id should exist");
        let choke_id =
            ff_track_parameter_id(2, FF_PARAM_SLOT_CHOKE_GROUP).expect("id should exist");

        let gain_update = recall
            .parameter_updates
            .iter()
            .find(|update| update.parameter_id == gain_id)
            .expect("gain parameter update should exist");
        assert!(gain_update.normalized_value > 0.45 && gain_update.normalized_value < 0.55);

        let pan_update = recall
            .parameter_updates
            .iter()
            .find(|update| update.parameter_id == pan_id)
            .expect("pan parameter update should exist");
        assert!(pan_update.normalized_value < 0.5);

        let choke_update = recall
            .parameter_updates
            .iter()
            .find(|update| update.parameter_id == choke_id)
            .expect("choke parameter update should exist");
        assert!((choke_update.normalized_value - 0.25).abs() < 0.0001);
    }

    #[test]
    fn saved_and_loaded_project_produce_identical_event_streams() {
        let mut project = Project {
            name: "phase2-deterministic".to_string(),
            kits: vec![Kit::default()],
            active_kit: Some(0),
            patterns: vec![PresetPattern::default()],
            active_pattern: Some(0),
        };

        project.kits[0].add_assignment(TrackAssignment {
            track_index: 0,
            sample_id: "kick-01".to_string(),
        });
        project.kits[0].set_track_controls(
            0,
            TrackControls {
                gain: 1.0,
                pan: 0.0,
                filter_cutoff: 0.5,
                envelope_decay: 0.7,
                pitch_semitones: 0.0,
                choke_group: Some(1),
            },
        );
        project.patterns[0].set_swing(0.2);
        project.patterns[0].set_step(
            0,
            0,
            PatternStep {
                active: true,
                velocity: 120,
            },
        );
        project.patterns[0].set_step(
            0,
            4,
            PatternStep {
                active: true,
                velocity: 100,
            },
        );

        let blocks = [480u32, 960u32, 2048u32, 4096u32, 16384u32];
        let original_events =
            render_recall_events(&project, 48_000, &blocks).expect("render original");

        let serialized = save_project_to_text(&project);
        let loaded = load_project_from_text(&serialized).expect("load serialized project");
        let loaded_events = render_recall_events(&loaded, 48_000, &blocks).expect("render loaded");

        assert_eq!(original_events, loaded_events);
    }

    #[test]
    fn saved_and_loaded_project_produce_identical_engine_recall() {
        let mut project = Project {
            name: "phase2-recall-deterministic".to_string(),
            kits: vec![Kit::default()],
            active_kit: Some(0),
            patterns: vec![PresetPattern::default()],
            active_pattern: Some(0),
        };

        project.kits[0].add_assignment(TrackAssignment {
            track_index: 1,
            sample_id: "hat-closed".to_string(),
        });
        project.kits[0].set_track_controls(
            1,
            TrackControls {
                gain: 0.8,
                pan: 0.0,
                filter_cutoff: 0.45,
                envelope_decay: 0.5,
                pitch_semitones: 2.0,
                choke_group: Some(1),
            },
        );

        let original = engine_recall_from_project(&project, 48_000).expect("render original");
        let serialized = save_project_to_text(&project);
        let loaded = load_project_from_text(&serialized).expect("load serialized project");
        let restored = engine_recall_from_project(&loaded, 48_000).expect("render loaded");

        assert_eq!(original, restored);
    }
}
