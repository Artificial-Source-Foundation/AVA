use std::time::Instant;

/// Phase of the voice input pipeline.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VoicePhase {
    /// No voice activity.
    Idle,
    /// Microphone is active, capturing audio.
    Recording,
    /// Audio captured, waiting for transcription result.
    Transcribing,
}

/// Tracks voice input state.
///
/// Always compiled (not cfg-gated) so `AppState` doesn't need conditional fields.
#[derive(Debug, Clone)]
pub struct VoiceState {
    pub phase: VoicePhase,
    /// When recording started (for elapsed timer).
    pub recording_start: Option<Instant>,
    /// Current microphone amplitude (0.0–1.0), updated from `VoiceAmplitude` events.
    pub amplitude: f32,
    /// Last error message from voice pipeline.
    pub error: Option<String>,
    /// Automatically submit transcribed text.
    pub auto_submit: bool,
    /// Restart recording after transcription completes (--voice mode).
    pub continuous: bool,
}

impl Default for VoiceState {
    fn default() -> Self {
        Self {
            phase: VoicePhase::Idle,
            recording_start: None,
            amplitude: 0.0,
            error: None,
            auto_submit: false,
            continuous: false,
        }
    }
}

impl VoiceState {
    /// Seconds elapsed since recording started, or 0 if not recording.
    pub fn recording_duration(&self) -> f32 {
        self.recording_start
            .map(|start| start.elapsed().as_secs_f32())
            .unwrap_or(0.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_state() {
        let state = VoiceState::default();
        assert_eq!(state.phase, VoicePhase::Idle);
        assert!(!state.auto_submit);
        assert!(!state.continuous);
        assert_eq!(state.recording_duration(), 0.0);
    }

    #[test]
    fn test_recording_duration() {
        let state = VoiceState {
            recording_start: Some(Instant::now()),
            ..VoiceState::default()
        };
        // Should be very small but > 0
        std::thread::sleep(std::time::Duration::from_millis(10));
        assert!(state.recording_duration() > 0.0);
    }

    #[test]
    fn test_phase_transitions() {
        let mut state = VoiceState::default();
        assert_eq!(state.phase, VoicePhase::Idle);

        state.phase = VoicePhase::Recording;
        state.recording_start = Some(Instant::now());
        assert_eq!(state.phase, VoicePhase::Recording);

        state.phase = VoicePhase::Transcribing;
        assert_eq!(state.phase, VoicePhase::Transcribing);

        state.phase = VoicePhase::Idle;
        state.recording_start = None;
        assert_eq!(state.phase, VoicePhase::Idle);
    }
}
