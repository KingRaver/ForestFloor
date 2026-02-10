pub const TRACK_COUNT: usize = 8;
pub const STEPS_PER_PATTERN: usize = 16;
pub const DEFAULT_BPM: f32 = 120.0;
pub const MIN_BPM: f32 = 20.0;
pub const MAX_BPM: f32 = 300.0;

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
    pub timeline_sample: u64,
    pub block_offset: u32,
}

#[derive(Debug)]
pub struct Sequencer {
    sample_rate_hz: u32,
    transport: Transport,
    pattern: Pattern,
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
            .min(samples_per_step(self.sample_rate_hz, self.transport.bpm()));
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
        self.samples_to_next_step = samples_per_step(self.sample_rate_hz, self.transport.bpm());
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
            self.samples_to_next_step = samples_per_step(self.sample_rate_hz, self.transport.bpm());
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
                self.samples_to_next_step =
                    samples_per_step(self.sample_rate_hz, self.transport.bpm());
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
                    timeline_sample,
                    block_offset,
                });
            }
        }
    }
}

fn samples_per_step(sample_rate_hz: u32, bpm: f32) -> f64 {
    let safe_bpm = bpm.clamp(MIN_BPM, MAX_BPM);
    f64::from(sample_rate_hz) * 60.0 / f64::from(safe_bpm) / 4.0
}

#[cfg(test)]
mod tests {
    use super::{
        Pattern, Sequencer, Step, Transport, DEFAULT_BPM, MAX_BPM, MIN_BPM, STEPS_PER_PATTERN,
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
}
