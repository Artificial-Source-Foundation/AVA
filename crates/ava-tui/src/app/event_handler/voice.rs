use super::*;

impl App {
    pub(crate) fn toggle_voice(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        match self.state.voice.phase {
            VoicePhase::Idle => {
                #[cfg(feature = "voice")]
                {
                    self.start_recording(app_tx);
                }
                #[cfg(not(feature = "voice"))]
                {
                    let _ = app_tx;
                    self.set_status(
                        "Voice requires --features voice. Rebuild with: cargo build --features voice",
                        StatusLevel::Error,
                    );
                }
            }
            VoicePhase::Recording => {
                self.stop_and_transcribe(app_tx);
            }
            VoicePhase::Transcribing => {
                // Ignore toggle while transcribing
            }
        }
    }

    #[cfg(feature = "voice")]
    fn start_recording(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        match crate::audio::AudioRecorder::start(
            app_tx.clone(),
            self.voice_config.silence_threshold,
            self.voice_config.silence_duration_secs,
            self.voice_config.max_duration_secs,
        ) {
            Ok(recorder) => {
                self.audio_recorder = Some(recorder);
                self.state.voice.phase = VoicePhase::Recording;
                self.state.voice.recording_start = Some(std::time::Instant::now());
                self.state.voice.error = None;

                // Max duration timeout
                let max_secs = self.voice_config.max_duration_secs;
                let tx = app_tx;
                tokio::spawn(async move {
                    tokio::time::sleep(std::time::Duration::from_secs(max_secs as u64)).await;
                    let _ = tx.send(AppEvent::VoiceSilenceDetected);
                });
            }
            Err(err) => {
                let _ = app_tx.send(AppEvent::VoiceError(err));
            }
        }
    }

    pub(crate) fn stop_and_transcribe(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        #[cfg(feature = "voice")]
        {
            let wav = self.audio_recorder.as_mut().and_then(|r| r.stop().ok());

            self.audio_recorder = None;

            match wav {
                Some(wav_data) => {
                    self.state.voice.phase = VoicePhase::Transcribing;
                    self.state.voice.amplitude = 0.0;

                    // Initialize transcriber lazily
                    if self.transcriber.is_none() {
                        let config = self.voice_config.clone();
                        let tx = app_tx.clone();
                        tokio::spawn(async move {
                            match crate::transcribe::create_transcriber(&config).await {
                                Ok(transcriber) => {
                                    match transcriber
                                        .transcribe(wav_data, config.language.as_deref())
                                        .await
                                    {
                                        Ok(text) => {
                                            let _ = tx.send(AppEvent::VoiceReady(text));
                                        }
                                        Err(e) => {
                                            let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                        }
                                    }
                                }
                                Err(e) => {
                                    let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                }
                            }
                        });
                    } else {
                        // This branch can't easily borrow self.transcriber for async use,
                        // so we always use the spawn pattern above
                        let config = self.voice_config.clone();
                        let tx = app_tx;
                        tokio::spawn(async move {
                            match crate::transcribe::create_transcriber(&config).await {
                                Ok(transcriber) => {
                                    match transcriber
                                        .transcribe(wav_data, config.language.as_deref())
                                        .await
                                    {
                                        Ok(text) => {
                                            let _ = tx.send(AppEvent::VoiceReady(text));
                                        }
                                        Err(e) => {
                                            let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                        }
                                    }
                                }
                                Err(e) => {
                                    let _ = tx.send(AppEvent::VoiceError(e.to_string()));
                                }
                            }
                        });
                    }
                }
                None => {
                    self.state.voice.phase = VoicePhase::Idle;
                    self.state.voice.recording_start = None;
                    let _ = app_tx.send(AppEvent::VoiceError("No audio captured".to_string()));
                }
            }
        }

        #[cfg(not(feature = "voice"))]
        {
            let _ = app_tx;
        }
    }
}
