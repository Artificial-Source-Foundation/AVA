//! Speech-to-text transcription backends.
//!
//! Only compiled when `feature = "voice"` is enabled.

use ava_types::Result;

/// Trait for speech-to-text transcription.
#[async_trait::async_trait]
pub trait Transcriber: Send + Sync {
    /// Transcribe WAV audio bytes to text.
    async fn transcribe(&self, wav: Vec<u8>, language: Option<&str>) -> Result<String>;
}

/// Transcribes audio via the OpenAI Whisper API.
pub struct WhisperApiClient {
    api_key: String,
    model: String,
}

impl WhisperApiClient {
    /// Create a new Whisper API client.
    ///
    /// Looks for the API key in:
    /// 1. `OPENAI_API_KEY` environment variable
    /// 2. `$XDG_DATA_HOME/ava/credentials.json` under the "openai" provider
    pub async fn new(model: String) -> Result<Self> {
        let api_key = Self::resolve_api_key().await?;
        Ok(Self { api_key, model })
    }

    async fn resolve_api_key() -> Result<String> {
        // 1. Environment variable
        if let Ok(key) = std::env::var("OPENAI_API_KEY") {
            if !key.is_empty() {
                return Ok(key);
            }
        }

        // 2. Credential store
        let cred_path = ava_config::CredentialStore::default_path()?;
        let store = ava_config::CredentialStore::load(&cred_path).await?;
        if let Some(cred) = store.get("openai") {
            return Ok(cred.api_key.clone());
        }

        Err(ava_types::AvaError::ConfigError(
            "No OpenAI API key found. Set OPENAI_API_KEY or add openai credentials to $XDG_DATA_HOME/ava/credentials.json".to_string(),
        ))
    }
}

#[async_trait::async_trait]
impl Transcriber for WhisperApiClient {
    async fn transcribe(&self, wav: Vec<u8>, language: Option<&str>) -> Result<String> {
        use reqwest::multipart;

        let file_part = multipart::Part::bytes(wav)
            .file_name("audio.wav")
            .mime_str("audio/wav")
            .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;

        let mut form = multipart::Form::new()
            .part("file", file_part)
            .text("model", self.model.clone());

        if let Some(lang) = language {
            form = form.text("language", lang.to_string());
        }

        let client = reqwest::Client::new();
        let response = client
            .post("https://api.openai.com/v1/audio/transcriptions")
            .bearer_auth(&self.api_key)
            .multipart(form)
            .timeout(std::time::Duration::from_secs(30))
            .send()
            .await
            .map_err(|e| {
                ava_types::AvaError::IoError(format!("Whisper API request failed: {e}"))
            })?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response
                .text()
                .await
                .unwrap_or_else(|_| "unknown error".to_string());
            return Err(ava_types::AvaError::ProviderError {
                provider: "openai-whisper".to_string(),
                message: format!("HTTP {status}: {body}"),
            });
        }

        #[derive(serde::Deserialize)]
        struct TranscriptionResponse {
            text: String,
        }

        let result: TranscriptionResponse = response
            .json()
            .await
            .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?;

        Ok(result.text)
    }
}

/// Local Whisper transcription using whisper-rs.
#[cfg(feature = "local-whisper")]
pub struct LocalWhisper {
    model_path: std::path::PathBuf,
}

#[cfg(feature = "local-whisper")]
impl LocalWhisper {
    pub fn new(model_name: &str) -> Result<Self> {
        let model_path = ava_config::models_dir()
            .unwrap_or_default()
            .join(format!("ggml-{model_name}.bin"));

        if !model_path.exists() {
            return Err(ava_types::AvaError::ConfigError(format!(
                "Whisper model not found at {}. Download from https://huggingface.co/ggerganov/whisper.cpp/tree/main",
                model_path.display()
            )));
        }

        Ok(Self { model_path })
    }
}

#[cfg(feature = "local-whisper")]
#[async_trait::async_trait]
impl Transcriber for LocalWhisper {
    async fn transcribe(&self, wav: Vec<u8>, language: Option<&str>) -> Result<String> {
        let model_path = self.model_path.clone();
        let language = language.map(|s| s.to_string());

        tokio::task::spawn_blocking(move || {
            let ctx = whisper_rs::WhisperContext::new(&model_path.to_string_lossy())
                .map_err(|e| ava_types::AvaError::ToolError(format!("Whisper init: {e}")))?;

            let mut params =
                whisper_rs::FullParams::new(whisper_rs::SamplingStrategy::Greedy { best_of: 1 });
            if let Some(ref lang) = language {
                params.set_language(Some(lang));
            }
            params.set_print_special(false);
            params.set_print_progress(false);
            params.set_print_realtime(false);
            params.set_print_timestamps(false);

            // Decode WAV to f32 samples
            let reader = hound::WavReader::new(std::io::Cursor::new(wav))
                .map_err(|e| ava_types::AvaError::IoError(format!("WAV decode: {e}")))?;
            let samples: Vec<f32> = reader
                .into_samples::<i16>()
                .filter_map(|s| s.ok())
                .map(|s| s as f32 / i16::MAX as f32)
                .collect();

            let mut state = ctx
                .create_state()
                .map_err(|e| ava_types::AvaError::ToolError(format!("Whisper state: {e}")))?;
            state
                .full(params, &samples)
                .map_err(|e| ava_types::AvaError::ToolError(format!("Whisper run: {e}")))?;

            let num_segments = state
                .full_n_segments()
                .map_err(|e| ava_types::AvaError::ToolError(format!("Whisper segments: {e}")))?;
            let mut text = String::new();
            for i in 0..num_segments {
                if let Ok(segment) = state.full_get_segment_text(i) {
                    text.push_str(&segment);
                }
            }

            Ok(text.trim().to_string())
        })
        .await
        .map_err(|e| ava_types::AvaError::ToolError(format!("Whisper task: {e}")))?
    }
}

/// Create a transcriber based on configuration.
///
/// Prefers local Whisper if the model file exists, otherwise falls back to API.
pub async fn create_transcriber(config: &ava_config::VoiceConfig) -> Result<Box<dyn Transcriber>> {
    #[cfg(feature = "local-whisper")]
    {
        let model_path = ava_config::models_dir()
            .unwrap_or_default()
            .join(format!("ggml-{}.bin", config.model));

        if model_path.exists() {
            return Ok(Box::new(LocalWhisper::new(&config.model)?));
        }
    }

    Ok(Box::new(WhisperApiClient::new(config.model.clone()).await?))
}
