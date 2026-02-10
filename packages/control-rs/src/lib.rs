pub const STEPS_PER_PATTERN: usize = 16;

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

#[derive(Debug, Eq, PartialEq)]
pub struct Pattern {
    length: usize,
    steps: [Step; STEPS_PER_PATTERN],
}

impl Pattern {
    pub fn new(length: usize) -> Self {
        Self {
            length: length.clamp(1, STEPS_PER_PATTERN),
            steps: [Step::default(); STEPS_PER_PATTERN],
        }
    }

    pub fn length(&self) -> usize {
        self.length
    }

    pub fn set_step(&mut self, index: usize, step: Step) -> bool {
        if index >= self.length {
            return false;
        }

        self.steps[index] = step;
        true
    }

    pub fn step(&self, index: usize) -> Option<Step> {
        if index >= self.length {
            return None;
        }

        Some(self.steps[index])
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Transport {
    pub bpm: f32,
    pub is_playing: bool,
}

impl Default for Transport {
    fn default() -> Self {
        Self {
            bpm: 120.0,
            is_playing: false,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Pattern, Step, STEPS_PER_PATTERN};

    #[test]
    fn pattern_length_is_clamped() {
        let pattern = Pattern::new(STEPS_PER_PATTERN + 4);
        assert_eq!(pattern.length(), STEPS_PER_PATTERN);
    }

    #[test]
    fn set_step_respects_pattern_length() {
        let mut pattern = Pattern::new(8);
        assert!(pattern.set_step(
            0,
            Step {
                active: true,
                velocity: 127
            }
        ));
        assert!(!pattern.set_step(
            15,
            Step {
                active: true,
                velocity: 127
            }
        ));
    }
}

