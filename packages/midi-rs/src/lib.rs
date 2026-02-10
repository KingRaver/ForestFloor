#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MidiBinding {
    pub cc: u8,
    pub parameter_id: String,
}

#[derive(Debug, Default)]
pub struct MappingProfile {
    bindings: Vec<MidiBinding>,
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
}

#[cfg(test)]
mod tests {
    use super::MappingProfile;

    #[test]
    fn bind_cc_replaces_existing_mapping() {
        let mut profile = MappingProfile::default();
        profile.bind_cc(74, "filter.cutoff");
        profile.bind_cc(74, "filter.drive");

        assert_eq!(profile.resolve_cc(74), Some("filter.drive"));
    }
}

