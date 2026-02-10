pub const TRACK_COUNT: usize = 8;
pub const STEPS_PER_PATTERN: usize = 16;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TrackAssignment {
    pub track_index: u8,
    pub sample_id: String,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TrackControls {
    pub gain: f32,
    pub pan: f32,
    pub filter_cutoff: f32,
    pub envelope_decay: f32,
    pub pitch_semitones: f32,
    pub choke_group: Option<u8>,
}

impl Default for TrackControls {
    fn default() -> Self {
        Self {
            gain: 1.0,
            pan: 0.0,
            filter_cutoff: 1.0,
            envelope_decay: 1.0,
            pitch_semitones: 0.0,
            choke_group: None,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct TrackControlAssignment {
    pub track_index: u8,
    pub controls: TrackControls,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Kit {
    pub name: String,
    pub tracks: Vec<TrackAssignment>,
    pub controls: Vec<TrackControlAssignment>,
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

    pub fn set_track_controls(&mut self, track_index: u8, controls: TrackControls) {
        if let Some(existing) = self
            .controls
            .iter_mut()
            .find(|value| value.track_index == track_index)
        {
            existing.controls = controls;
            return;
        }

        self.controls.push(TrackControlAssignment {
            track_index,
            controls,
        });
    }

    pub fn track_controls(&self, track_index: u8) -> Option<TrackControls> {
        self.controls
            .iter()
            .find(|value| value.track_index == track_index)
            .map(|value| value.controls)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PatternStep {
    pub active: bool,
    pub velocity: u8,
}

impl Default for PatternStep {
    fn default() -> Self {
        Self {
            active: false,
            velocity: 100,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Pattern {
    pub name: String,
    pub swing: f32,
    pub steps: [[PatternStep; STEPS_PER_PATTERN]; TRACK_COUNT],
}

impl Default for Pattern {
    fn default() -> Self {
        Self {
            name: "pattern".to_string(),
            swing: 0.0,
            steps: [[PatternStep::default(); STEPS_PER_PATTERN]; TRACK_COUNT],
        }
    }
}

impl Pattern {
    pub fn set_step(&mut self, track_index: usize, step_index: usize, step: PatternStep) -> bool {
        if track_index >= TRACK_COUNT || step_index >= STEPS_PER_PATTERN {
            return false;
        }

        self.steps[track_index][step_index] = step;
        true
    }

    pub fn step(&self, track_index: usize, step_index: usize) -> Option<PatternStep> {
        if track_index >= TRACK_COUNT || step_index >= STEPS_PER_PATTERN {
            return None;
        }

        Some(self.steps[track_index][step_index])
    }

    pub fn set_swing(&mut self, swing: f32) {
        self.swing = swing.clamp(0.0, 0.45);
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Project {
    pub name: String,
    pub kits: Vec<Kit>,
    pub active_kit: Option<usize>,
    pub patterns: Vec<Pattern>,
    pub active_pattern: Option<usize>,
}

impl Project {
    pub fn set_active_kit(&mut self, index: usize) -> bool {
        if index >= self.kits.len() {
            return false;
        }

        self.active_kit = Some(index);
        true
    }

    pub fn set_active_pattern(&mut self, index: usize) -> bool {
        if index >= self.patterns.len() {
            return false;
        }

        self.active_pattern = Some(index);
        true
    }
}

fn format_f32(value: f32) -> String {
    format!("{value:.6}")
}

fn parse_f32(value: &str, field: &str) -> Result<f32, String> {
    value
        .parse::<f32>()
        .map_err(|_| format!("invalid float for {field}: {value}"))
}

fn parse_usize(value: &str, field: &str) -> Result<usize, String> {
    value
        .parse::<usize>()
        .map_err(|_| format!("invalid usize for {field}: {value}"))
}

fn parse_u8(value: &str, field: &str) -> Result<u8, String> {
    value
        .parse::<u8>()
        .map_err(|_| format!("invalid u8 for {field}: {value}"))
}

fn encode_text(value: &str) -> String {
    let mut encoded = String::with_capacity(value.len() * 2);
    for byte in value.as_bytes() {
        encoded.push_str(&format!("{byte:02X}"));
    }
    encoded
}

fn decode_text(value: &str) -> Result<String, String> {
    if !value.len().is_multiple_of(2) {
        return Err("hex string length must be even".to_string());
    }

    let mut bytes = Vec::with_capacity(value.len() / 2);
    let mut index = 0;
    while index < value.len() {
        let end = index + 2;
        let byte = u8::from_str_radix(&value[index..end], 16)
            .map_err(|_| format!("invalid hex byte: {}", &value[index..end]))?;
        bytes.push(byte);
        index = end;
    }

    String::from_utf8(bytes).map_err(|_| "invalid utf8 in encoded text".to_string())
}

fn serialize_kit_body(kit: &Kit) -> Vec<String> {
    let mut lines = Vec::new();
    lines.push(format!("name={}", encode_text(&kit.name)));

    let mut tracks = kit.tracks.clone();
    tracks.sort_by_key(|value| value.track_index);
    for track in tracks {
        lines.push(format!(
            "track|{}|{}",
            track.track_index,
            encode_text(&track.sample_id)
        ));
    }

    let mut controls = kit.controls.clone();
    controls.sort_by_key(|value| value.track_index);
    for control in controls {
        lines.push(format!(
            "control|{}|{}|{}|{}|{}|{}|{}",
            control.track_index,
            format_f32(control.controls.gain),
            format_f32(control.controls.pan),
            format_f32(control.controls.filter_cutoff),
            format_f32(control.controls.envelope_decay),
            format_f32(control.controls.pitch_semitones),
            control.controls.choke_group.map(i32::from).unwrap_or(-1),
        ));
    }

    lines
}

fn deserialize_kit_body(lines: &[String]) -> Result<Kit, String> {
    let mut kit = Kit::default();

    for line in lines {
        if let Some(name_hex) = line.strip_prefix("name=") {
            kit.name = decode_text(name_hex)?;
            continue;
        }

        if let Some(rest) = line.strip_prefix("track|") {
            let fields: Vec<&str> = rest.split('|').collect();
            if fields.len() != 2 {
                return Err(format!("invalid track line: {line}"));
            }

            let track_index = parse_u8(fields[0], "track_index")?;
            let sample_id = decode_text(fields[1])?;
            if !kit.add_assignment(TrackAssignment {
                track_index,
                sample_id,
            }) {
                return Err(format!("duplicate track assignment: {track_index}"));
            }
            continue;
        }

        if let Some(rest) = line.strip_prefix("control|") {
            let fields: Vec<&str> = rest.split('|').collect();
            if fields.len() != 7 {
                return Err(format!("invalid control line: {line}"));
            }

            let track_index = parse_u8(fields[0], "control.track_index")?;
            let choke_group_value = fields[6]
                .parse::<i32>()
                .map_err(|_| format!("invalid choke group: {}", fields[6]))?;
            let choke_group = if choke_group_value < 0 {
                None
            } else {
                Some(
                    u8::try_from(choke_group_value)
                        .map_err(|_| format!("choke group out of range: {choke_group_value}"))?,
                )
            };

            kit.set_track_controls(
                track_index,
                TrackControls {
                    gain: parse_f32(fields[1], "control.gain")?,
                    pan: parse_f32(fields[2], "control.pan")?,
                    filter_cutoff: parse_f32(fields[3], "control.filter_cutoff")?,
                    envelope_decay: parse_f32(fields[4], "control.envelope_decay")?,
                    pitch_semitones: parse_f32(fields[5], "control.pitch_semitones")?,
                    choke_group,
                },
            );
            continue;
        }

        return Err(format!("unknown kit line: {line}"));
    }

    Ok(kit)
}

fn serialize_pattern_body(pattern: &Pattern) -> Vec<String> {
    let mut lines = Vec::new();
    lines.push(format!("name={}", encode_text(&pattern.name)));
    lines.push(format!("swing={}", format_f32(pattern.swing)));

    for track_index in 0..TRACK_COUNT {
        for step_index in 0..STEPS_PER_PATTERN {
            let step = pattern.steps[track_index][step_index];
            lines.push(format!(
                "step|{}|{}|{}|{}",
                track_index,
                step_index,
                if step.active { 1 } else { 0 },
                step.velocity
            ));
        }
    }

    lines
}

fn deserialize_pattern_body(lines: &[String]) -> Result<Pattern, String> {
    let mut pattern = Pattern::default();
    for line in lines {
        if let Some(name_hex) = line.strip_prefix("name=") {
            pattern.name = decode_text(name_hex)?;
            continue;
        }

        if let Some(value) = line.strip_prefix("swing=") {
            pattern.set_swing(parse_f32(value, "pattern.swing")?);
            continue;
        }

        if let Some(rest) = line.strip_prefix("step|") {
            let fields: Vec<&str> = rest.split('|').collect();
            if fields.len() != 4 {
                return Err(format!("invalid step line: {line}"));
            }

            let track_index = parse_usize(fields[0], "step.track_index")?;
            let step_index = parse_usize(fields[1], "step.step_index")?;
            let active = match fields[2] {
                "0" => false,
                "1" => true,
                _ => return Err(format!("invalid step active value: {}", fields[2])),
            };
            let velocity = parse_u8(fields[3], "step.velocity")?;
            if !pattern.set_step(track_index, step_index, PatternStep { active, velocity }) {
                return Err(format!("step index out of range: {line}"));
            }
            continue;
        }

        return Err(format!("unknown pattern line: {line}"));
    }

    Ok(pattern)
}

pub fn save_kit_to_text(kit: &Kit) -> String {
    let mut lines = Vec::new();
    lines.push("FF_KIT_V1".to_string());
    lines.extend(serialize_kit_body(kit));
    lines.join("\n")
}

pub fn load_kit_from_text(text: &str) -> Result<Kit, String> {
    let mut lines = text.lines();
    let header = lines
        .next()
        .ok_or_else(|| "missing kit header".to_string())?;
    if header != "FF_KIT_V1" {
        return Err(format!("unexpected kit header: {header}"));
    }
    deserialize_kit_body(&lines.map(|line| line.to_string()).collect::<Vec<_>>())
}

pub fn save_pattern_to_text(pattern: &Pattern) -> String {
    let mut lines = Vec::new();
    lines.push("FF_PATTERN_V1".to_string());
    lines.extend(serialize_pattern_body(pattern));
    lines.join("\n")
}

pub fn load_pattern_from_text(text: &str) -> Result<Pattern, String> {
    let mut lines = text.lines();
    let header = lines
        .next()
        .ok_or_else(|| "missing pattern header".to_string())?;
    if header != "FF_PATTERN_V1" {
        return Err(format!("unexpected pattern header: {header}"));
    }
    deserialize_pattern_body(&lines.map(|line| line.to_string()).collect::<Vec<_>>())
}

pub fn save_project_to_text(project: &Project) -> String {
    let mut lines = Vec::new();
    lines.push("FF_PROJECT_V1".to_string());
    lines.push(format!("name={}", encode_text(&project.name)));
    lines.push(format!(
        "active_kit={}",
        project
            .active_kit
            .map(|value| value.to_string())
            .unwrap_or_else(|| "-1".to_string())
    ));
    lines.push(format!(
        "active_pattern={}",
        project
            .active_pattern
            .map(|value| value.to_string())
            .unwrap_or_else(|| "-1".to_string())
    ));

    for kit in &project.kits {
        lines.push("BEGIN_KIT".to_string());
        lines.extend(serialize_kit_body(kit));
        lines.push("END_KIT".to_string());
    }

    for pattern in &project.patterns {
        lines.push("BEGIN_PATTERN".to_string());
        lines.extend(serialize_pattern_body(pattern));
        lines.push("END_PATTERN".to_string());
    }

    lines.join("\n")
}

pub fn load_project_from_text(text: &str) -> Result<Project, String> {
    let mut lines = text.lines().peekable();
    let header = lines
        .next()
        .ok_or_else(|| "missing project header".to_string())?;
    if header != "FF_PROJECT_V1" {
        return Err(format!("unexpected project header: {header}"));
    }

    let mut project = Project::default();
    let mut active_kit_raw: Option<isize> = None;
    let mut active_pattern_raw: Option<isize> = None;

    while let Some(line) = lines.next() {
        if let Some(name_hex) = line.strip_prefix("name=") {
            project.name = decode_text(name_hex)?;
            continue;
        }

        if let Some(value) = line.strip_prefix("active_kit=") {
            active_kit_raw = Some(
                value
                    .parse::<isize>()
                    .map_err(|_| format!("invalid active_kit value: {value}"))?,
            );
            continue;
        }

        if let Some(value) = line.strip_prefix("active_pattern=") {
            active_pattern_raw = Some(
                value
                    .parse::<isize>()
                    .map_err(|_| format!("invalid active_pattern value: {value}"))?,
            );
            continue;
        }

        if line == "BEGIN_KIT" {
            let mut block = Vec::new();
            loop {
                let next_line = lines
                    .next()
                    .ok_or_else(|| "unterminated kit block".to_string())?;
                if next_line == "END_KIT" {
                    break;
                }
                block.push(next_line.to_string());
            }
            project.kits.push(deserialize_kit_body(&block)?);
            continue;
        }

        if line == "BEGIN_PATTERN" {
            let mut block = Vec::new();
            loop {
                let next_line = lines
                    .next()
                    .ok_or_else(|| "unterminated pattern block".to_string())?;
                if next_line == "END_PATTERN" {
                    break;
                }
                block.push(next_line.to_string());
            }
            project.patterns.push(deserialize_pattern_body(&block)?);
            continue;
        }

        return Err(format!("unknown project line: {line}"));
    }

    if let Some(raw) = active_kit_raw {
        if raw >= 0 {
            let index = usize::try_from(raw).map_err(|_| "invalid active_kit index".to_string())?;
            if index >= project.kits.len() {
                return Err(format!("active_kit out of range: {index}"));
            }
            project.active_kit = Some(index);
        }
    }

    if let Some(raw) = active_pattern_raw {
        if raw >= 0 {
            let index =
                usize::try_from(raw).map_err(|_| "invalid active_pattern index".to_string())?;
            if index >= project.patterns.len() {
                return Err(format!("active_pattern out of range: {index}"));
            }
            project.active_pattern = Some(index);
        }
    }

    Ok(project)
}

#[cfg(test)]
mod tests {
    use super::{
        load_kit_from_text, load_pattern_from_text, load_project_from_text, save_kit_to_text,
        save_pattern_to_text, save_project_to_text, Kit, Pattern, PatternStep, Project,
        TrackAssignment, TrackControls,
    };

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
    fn track_controls_roundtrip_in_kit() {
        let mut kit = Kit::default();
        kit.set_track_controls(
            3,
            TrackControls {
                gain: 0.8,
                pan: -0.25,
                filter_cutoff: 0.4,
                envelope_decay: 0.7,
                pitch_semitones: 3.0,
                choke_group: Some(1),
            },
        );

        let encoded = save_kit_to_text(&kit);
        let decoded = load_kit_from_text(&encoded).expect("kit decode");
        assert_eq!(kit, decoded);
    }

    #[test]
    fn pattern_steps_and_swing_are_mutable() {
        let mut pattern = Pattern::default();
        assert!(pattern.set_step(
            2,
            4,
            PatternStep {
                active: true,
                velocity: 127,
            },
        ));
        pattern.set_swing(0.3);

        let encoded = save_pattern_to_text(&pattern);
        let decoded = load_pattern_from_text(&encoded).expect("pattern decode");
        assert_eq!(pattern, decoded);
    }

    #[test]
    fn active_indexes_must_exist() {
        let mut project = Project {
            name: "demo".to_string(),
            kits: vec![Kit::default()],
            active_kit: None,
            patterns: vec![Pattern::default()],
            active_pattern: None,
        };

        assert!(project.set_active_kit(0));
        assert!(!project.set_active_kit(3));
        assert!(project.set_active_pattern(0));
        assert!(!project.set_active_pattern(2));
    }

    #[test]
    fn project_text_roundtrip_is_deterministic() {
        let mut project = Project {
            name: "phase2".to_string(),
            kits: vec![Kit::default()],
            active_kit: Some(0),
            patterns: vec![Pattern::default()],
            active_pattern: Some(0),
        };

        project.kits[0].name = "kit-a".to_string();
        project.kits[0].add_assignment(TrackAssignment {
            track_index: 0,
            sample_id: "kick.01".to_string(),
        });
        project.kits[0].set_track_controls(
            0,
            TrackControls {
                gain: 1.2,
                pan: 0.1,
                filter_cutoff: 0.6,
                envelope_decay: 0.8,
                pitch_semitones: -2.0,
                choke_group: Some(1),
            },
        );
        project.patterns[0].name = "main".to_string();
        project.patterns[0].set_swing(0.2);
        project.patterns[0].set_step(
            0,
            0,
            PatternStep {
                active: true,
                velocity: 120,
            },
        );

        let encoded_1 = save_project_to_text(&project);
        let decoded = load_project_from_text(&encoded_1).expect("project decode");
        let encoded_2 = save_project_to_text(&decoded);

        assert_eq!(project, decoded);
        assert_eq!(encoded_1, encoded_2);
    }
}
