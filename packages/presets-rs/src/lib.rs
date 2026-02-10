#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TrackAssignment {
    pub track_index: u8,
    pub sample_id: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Kit {
    pub name: String,
    pub tracks: Vec<TrackAssignment>,
}

impl Kit {
    pub fn add_assignment(&mut self, assignment: TrackAssignment) -> bool {
        if self
            .tracks
            .iter()
            .any(|track| track.track_index == assignment.track_index)
        {
            return false;
        }

        self.tracks.push(assignment);
        true
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Project {
    pub name: String,
    pub kits: Vec<Kit>,
    pub active_kit: Option<usize>,
}

impl Project {
    pub fn set_active_kit(&mut self, index: usize) -> bool {
        if index >= self.kits.len() {
            return false;
        }

        self.active_kit = Some(index);
        true
    }
}

#[cfg(test)]
mod tests {
    use super::{Kit, Project, TrackAssignment};

    #[test]
    fn duplicate_track_assignment_is_rejected() {
        let mut kit = Kit::default();
        assert!(kit.add_assignment(TrackAssignment {
            track_index: 0,
            sample_id: "kick.01".to_string(),
        }));
        assert!(!kit.add_assignment(TrackAssignment {
            track_index: 0,
            sample_id: "kick.02".to_string(),
        }));
    }

    #[test]
    fn active_kit_must_exist() {
        let mut project = Project {
            name: "demo".to_string(),
            kits: vec![Kit::default()],
            active_kit: None,
        };

        assert!(project.set_active_kit(0));
        assert!(!project.set_active_kit(2));
    }
}

