//! Audio capture and silence detection for voice input.
//!
//! Only compiled when `feature = "voice"` is enabled.

use crate::event::AppEvent;
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{SampleFormat, SampleRate, Stream};
use std::sync::{Arc, Mutex};
use tokio::sync::mpsc;

/// Detects silence in an audio stream using RMS amplitude with a sliding window.
pub struct SilenceDetector {
    threshold: f32,
    duration_samples: usize,
    /// Ring buffer of per-chunk RMS values (sliding window).
    window: Vec<f32>,
    window_pos: usize,
    window_filled: bool,
    /// Samples per RMS chunk (matches cpal callback size, typically ~480 samples).
    chunk_size: usize,
    sample_rate: u32,
}

impl SilenceDetector {
    /// Create a new silence detector.
    ///
    /// - `threshold`: RMS amplitude (0.0–1.0) below which audio is "silent"
    /// - `duration_secs`: seconds of continuous silence before triggering
    /// - `sample_rate`: audio sample rate in Hz
    pub fn new(threshold: f32, duration_secs: f32, sample_rate: u32) -> Self {
        // 500ms sliding window, broken into chunks
        let window_ms = 500;
        let chunk_size = (sample_rate as usize * window_ms) / 1000;
        // How many chunks of silence we need to see
        let window_len = ((duration_secs * 1000.0) / window_ms as f32).ceil() as usize;
        let window_len = window_len.max(1);

        Self {
            threshold,
            duration_samples: 0, // unused, we use window-based
            window: vec![0.0; window_len],
            window_pos: 0,
            window_filled: false,
            chunk_size,
            sample_rate,
        }
    }

    /// Feed samples and return `true` if sustained silence is detected.
    pub fn feed(&mut self, samples: &[i16]) -> bool {
        if samples.is_empty() {
            return false;
        }

        let rms = compute_rms(samples);
        let normalized = rms / i16::MAX as f32;

        self.window[self.window_pos] = normalized;
        self.window_pos = (self.window_pos + 1) % self.window.len();
        if self.window_pos == 0 {
            self.window_filled = true;
        }

        if !self.window_filled {
            return false;
        }

        // All windows below threshold → silence detected
        self.window.iter().all(|&v| v < self.threshold)
    }

    /// Reset the detector state.
    pub fn reset(&mut self) {
        self.window.fill(0.0);
        self.window_pos = 0;
        self.window_filled = false;
    }
}

/// Compute RMS of i16 samples.
fn compute_rms(samples: &[i16]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }
    let sum_sq: f64 = samples.iter().map(|&s| (s as f64) * (s as f64)).sum();
    (sum_sq / samples.len() as f64).sqrt() as f32
}

/// Compute normalized amplitude (0.0–1.0) from i16 samples.
fn compute_amplitude(samples: &[i16]) -> f32 {
    compute_rms(samples) / i16::MAX as f32
}

/// Records audio from the default input device.
pub struct AudioRecorder {
    stream: Option<Stream>,
    samples: Arc<Mutex<Vec<i16>>>,
    sample_rate: u32,
}

impl AudioRecorder {
    /// Start recording from the default microphone.
    ///
    /// Sends `VoiceAmplitude` and `VoiceSilenceDetected` events through `app_tx`.
    pub fn start(
        app_tx: mpsc::UnboundedSender<AppEvent>,
        silence_threshold: f32,
        silence_duration_secs: f32,
        max_duration_secs: u32,
    ) -> Result<Self, String> {
        let host = cpal::default_host();
        let device = host
            .default_input_device()
            .ok_or_else(|| "No microphone found. Check audio input settings.".to_string())?;

        let supported = device
            .supported_input_configs()
            .map_err(|e| format!("Failed to query audio configs: {e}"))?;

        // Try to find 16kHz mono i16; fall back to default
        let target_rate = SampleRate(16000);
        let config = supported
            .into_iter()
            .filter(|c| c.channels() == 1 && c.sample_format() == SampleFormat::I16)
            .find(|c| c.min_sample_rate() <= target_rate && c.max_sample_rate() >= target_rate)
            .map(|c| c.with_sample_rate(target_rate));

        let (stream_config, native_rate) = if let Some(cfg) = config {
            (cfg.into(), 16000u32)
        } else {
            let default_cfg = device
                .default_input_config()
                .map_err(|e| format!("Failed to get default audio config: {e}"))?;
            let rate = default_cfg.sample_rate().0;
            (default_cfg.into(), rate)
        };

        let samples: Arc<Mutex<Vec<i16>>> = Arc::new(Mutex::new(Vec::new()));
        let samples_clone = Arc::clone(&samples);

        let mut silence_detector =
            SilenceDetector::new(silence_threshold, silence_duration_secs, native_rate);

        let app_tx_clone = app_tx.clone();

        let stream = device
            .build_input_stream(
                &stream_config,
                move |data: &[i16], _: &cpal::InputCallbackInfo| {
                    // Store samples
                    if let Ok(mut buf) = samples_clone.lock() {
                        buf.extend_from_slice(data);
                    }

                    // Send amplitude
                    let amp = compute_amplitude(data);
                    let _ = app_tx_clone.send(AppEvent::VoiceAmplitude(amp));

                    // Check silence
                    if silence_detector.feed(data) {
                        let _ = app_tx_clone.send(AppEvent::VoiceSilenceDetected);
                        silence_detector.reset();
                    }
                },
                move |err| {
                    let _ = app_tx.send(AppEvent::VoiceError(format!("Audio error: {err}")));
                },
                None,
            )
            .map_err(|e| format!("Failed to open audio stream: {e}"))?;

        stream
            .play()
            .map_err(|e| format!("Failed to start recording: {e}"))?;

        Ok(Self {
            stream: Some(stream),
            samples,
            sample_rate: native_rate,
        })
    }

    /// Stop recording and return WAV-encoded audio bytes.
    pub fn stop(&mut self) -> Result<Vec<u8>, String> {
        // Drop the stream to stop recording
        self.stream.take();

        let samples = self
            .samples
            .lock()
            .map_err(|e| format!("Failed to lock samples: {e}"))?
            .clone();

        if samples.is_empty() {
            return Err("No audio captured".to_string());
        }

        // Resample to 16kHz if needed
        let (final_samples, final_rate) = if self.sample_rate != 16000 {
            (resample(&samples, self.sample_rate, 16000), 16000u32)
        } else {
            (samples, self.sample_rate)
        };

        encode_wav(&final_samples, final_rate)
    }
}

/// Linear interpolation resampling.
fn resample(samples: &[i16], from_rate: u32, to_rate: u32) -> Vec<i16> {
    if from_rate == to_rate || samples.is_empty() {
        return samples.to_vec();
    }

    let ratio = from_rate as f64 / to_rate as f64;
    let new_len = (samples.len() as f64 / ratio) as usize;
    let mut output = Vec::with_capacity(new_len);

    for i in 0..new_len {
        let src_pos = i as f64 * ratio;
        let idx = src_pos as usize;
        let frac = src_pos - idx as f64;

        let sample = if idx + 1 < samples.len() {
            let a = samples[idx] as f64;
            let b = samples[idx + 1] as f64;
            (a + (b - a) * frac) as i16
        } else if idx < samples.len() {
            samples[idx]
        } else {
            0
        };
        output.push(sample);
    }

    output
}

/// Encode i16 samples as WAV bytes (16kHz, mono, 16-bit PCM).
fn encode_wav(samples: &[i16], sample_rate: u32) -> Result<Vec<u8>, String> {
    let mut buf = std::io::Cursor::new(Vec::new());
    let spec = hound::WavSpec {
        channels: 1,
        sample_rate,
        bits_per_sample: 16,
        sample_format: hound::SampleFormat::Int,
    };

    let mut writer =
        hound::WavWriter::new(&mut buf, spec).map_err(|e| format!("WAV encode error: {e}"))?;

    for &sample in samples {
        writer
            .write_sample(sample)
            .map_err(|e| format!("WAV write error: {e}"))?;
    }

    writer
        .finalize()
        .map_err(|e| format!("WAV finalize error: {e}"))?;

    Ok(buf.into_inner())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_silence_detector_below_threshold() {
        let mut detector = SilenceDetector::new(0.01, 1.0, 16000);
        // Feed enough silent chunks to fill the window
        let silence = vec![0i16; 8000]; // 500ms at 16kHz
        for _ in 0..10 {
            detector.feed(&silence);
        }
        // After enough silent windows, should detect silence
        assert!(detector.feed(&silence));
    }

    #[test]
    fn test_silence_detector_above_threshold() {
        let mut detector = SilenceDetector::new(0.01, 1.0, 16000);
        // Loud signal
        let loud: Vec<i16> = (0..8000).map(|i| ((i % 100) * 300) as i16).collect();
        for _ in 0..10 {
            assert!(!detector.feed(&loud));
        }
    }

    #[test]
    fn test_compute_rms() {
        assert_eq!(compute_rms(&[]), 0.0);
        assert_eq!(compute_rms(&[0, 0, 0]), 0.0);
        assert!(compute_rms(&[1000, -1000, 1000]) > 0.0);
    }

    #[test]
    fn test_resample_same_rate() {
        let samples = vec![1, 2, 3, 4, 5];
        assert_eq!(resample(&samples, 16000, 16000), samples);
    }

    #[test]
    fn test_resample_downsample() {
        let samples: Vec<i16> = (0..48000).map(|i| (i % 1000) as i16).collect();
        let resampled = resample(&samples, 48000, 16000);
        // Should be roughly 1/3 the length
        assert!((resampled.len() as f64 - 16000.0).abs() < 2.0);
    }

    #[test]
    fn test_encode_wav() {
        let samples = vec![0i16; 1600]; // 100ms at 16kHz
        let wav = encode_wav(&samples, 16000).unwrap();
        // WAV header is 44 bytes, then 1600 * 2 bytes of data
        assert_eq!(wav.len(), 44 + 1600 * 2);
        // Check RIFF header
        assert_eq!(&wav[0..4], b"RIFF");
        assert_eq!(&wav[8..12], b"WAVE");
    }
}
