# Sprint 48: Voice Input & Hands-Free Coding

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Add voice input to AVA so developers can speak their requests instead of typing. After this sprint, pressing a hotkey in the TUI starts recording, and AVA transcribes the speech into the input field. This enables hands-free coding workflows.

## Key Files to Read

```
crates/ava-tui/src/app.rs                # App, AppState, event loop, key handling
crates/ava-tui/src/ui/mod.rs             # UI rendering, input area
crates/ava-tui/src/ui/status_bar.rs      # Status bar (activity indicators)
crates/ava-tui/src/config/cli.rs         # CliArgs
crates/ava-tui/Cargo.toml               # Dependencies

crates/ava-types/src/lib.rs              # Shared types
```

## What Already Exists

- **TUI input**: Text input field with cursor, history, multi-line support
- **Status bar**: Shows model, cost, activity status, MCP info
- **Key bindings**: Enter to submit, Ctrl+C to cancel, Ctrl+M for model selector
- **Headless mode**: Accepts goal as CLI argument

## Theme 1: Audio Capture

### Story 1.1: Audio Recording Module

Build a cross-platform audio recording module.

**Implementation:**
- File: `crates/ava-tui/src/audio.rs` (NEW)
- Use `cpal` crate for cross-platform audio capture
- Record to a buffer in PCM format (16-bit, 16kHz mono — Whisper's preferred format)

```rust
pub struct AudioRecorder {
    state: RecordingState,
    buffer: Vec<i16>,
    sample_rate: u32,
}

pub enum RecordingState {
    Idle,
    Recording { started: Instant },
    Processing,
}

impl AudioRecorder {
    pub fn new() -> Result<Self>;
    pub fn start_recording(&mut self) -> Result<()>;
    pub fn stop_recording(&mut self) -> Result<Vec<u8>>;  // Returns WAV bytes
    pub fn is_recording(&self) -> bool;
    pub fn duration(&self) -> Duration;
}
```

- Convert PCM buffer to WAV format on stop (simple header + data)
- Configurable max recording duration (default: 60 seconds)
- Auto-stop on silence detection (optional, 3s silence threshold)

**Acceptance criteria:**
- Records audio from default input device
- Produces valid WAV data
- Start/stop works cleanly
- Max duration enforced
- Handles "no microphone" gracefully (error, not crash)
- Add tests (mock audio source)

### Story 1.2: Silence Detection

Detect when the user stops speaking and auto-stop recording.

**Implementation:**
- In `AudioRecorder`, monitor RMS (root mean square) energy of incoming samples
- If RMS drops below threshold for `silence_duration` (default 2.5s), auto-stop
- Use a sliding window (500ms) to smooth out brief pauses

```rust
pub struct SilenceDetector {
    threshold: f32,        // RMS threshold (default: 0.01)
    duration: Duration,    // Required silence duration (default: 2.5s)
    window_ms: usize,      // Smoothing window (default: 500ms)
    silent_since: Option<Instant>,
}

impl SilenceDetector {
    pub fn feed(&mut self, samples: &[i16]) -> bool;  // Returns true if silence detected
}
```

**Acceptance criteria:**
- Detects silence after speech stops
- Configurable threshold and duration
- Brief pauses (< 1s) don't trigger stop
- Add tests with synthetic audio data

## Theme 2: Speech-to-Text

### Story 2.1: Whisper API Integration

Transcribe recorded audio using OpenAI's Whisper API.

**Implementation:**
- File: `crates/ava-tui/src/transcribe.rs` (NEW)
- Use OpenAI's `/v1/audio/transcriptions` endpoint
- Send WAV data as multipart form upload

```rust
pub struct WhisperTranscriber {
    api_key: String,
    model: String,  // "whisper-1"
    language: Option<String>,
}

impl WhisperTranscriber {
    pub async fn transcribe(&self, audio: &[u8]) -> Result<String>;
}
```

- API key: reuse OpenAI API key from credentials, or dedicated `OPENAI_API_KEY` env var
- Model: `whisper-1` (only option currently)
- Language hint: optional, auto-detect by default

**Acceptance criteria:**
- Sends audio to Whisper API and returns transcript
- Handles API errors (rate limit, auth, network)
- Timeout: 30 seconds
- Add test with mock HTTP server

### Story 2.2: Local Whisper Fallback (Optional)

Support local Whisper inference via `whisper-rs` for offline/privacy use.

**Implementation:**
- Feature-gated: `--features local-whisper`
- Use `whisper-rs` crate (Rust bindings for whisper.cpp)
- Model download: `ava voice --download-model base` downloads the model to `~/.ava/models/`
- Auto-detect: use local model if available, API if not

```rust
pub enum TranscriberBackend {
    Api(WhisperTranscriber),
    Local(LocalWhisperTranscriber),
}

pub struct LocalWhisperTranscriber {
    model_path: PathBuf,
}
```

- Default model: `base.en` (~150MB, good balance of speed/accuracy)
- Model sizes: tiny (75MB), base (150MB), small (500MB), medium (1.5GB)

**Acceptance criteria:**
- Local transcription works offline
- Feature-gated (not compiled by default)
- Model download command works
- Graceful fallback to API when local model not found
- Add test

## Theme 3: TUI Integration

### Story 3.1: Voice Input Hotkey

Add a hotkey to toggle voice recording in the TUI.

**Implementation:**
- Hotkey: `Ctrl+V` (for Voice) — toggles recording on/off
- When recording starts:
  - Status bar shows: `🎤 Recording... (3.2s)`
  - Input field shows placeholder: `[Recording... press Ctrl+V to stop]`
  - Timer updates every 100ms
- When recording stops:
  - Status bar shows: `🔄 Transcribing...`
  - Audio sent to Whisper
  - Transcript inserted into input field at cursor position
  - User can edit before submitting

**Implementation in `app.rs`:**
- Add `audio_recorder: Option<AudioRecorder>` to `AppState`
- Add `transcriber: Option<TranscriberBackend>` to `AppState`
- Handle `Ctrl+V` in key event handler:
  - If idle → start recording
  - If recording → stop, transcribe, insert

**Acceptance criteria:**
- Ctrl+V starts recording (visual feedback)
- Ctrl+V again stops and transcribes
- Transcript appears in input field
- User can edit before sending
- Recording timer visible in status bar
- Works smoothly with existing TUI rendering (no flicker)

### Story 3.2: Voice Mode (Continuous)

Add a continuous voice mode for hands-free operation.

**Implementation:**
- `ava --voice` flag enables continuous voice mode
- Flow:
  1. AVA shows prompt: "Listening..."
  2. User speaks → auto-detected via silence detection
  3. Transcription → shown in input field
  4. Auto-submit after 1s pause (or user presses Enter to submit immediately)
  5. AVA processes request
  6. When agent completes, return to step 1

- Add `--voice` to `CliArgs`:
  ```rust
  #[arg(long)]
  pub voice: bool,
  ```

- In voice mode, the TUI input area shows the live transcript as it's being processed

**Acceptance criteria:**
- `--voice` enables continuous listening
- Auto-detect speech start/stop
- Auto-submit after silence
- Agent response displayed normally
- Returns to listening after completion
- Ctrl+C exits voice mode
- Works in both TUI and headless mode

### Story 3.3: Voice Configuration

Add voice settings to config file.

**Implementation:**
- In `~/.ava/config.yaml`:
```yaml
voice:
  backend: api          # "api" or "local"
  model: whisper-1      # API model or local model name
  language: en          # Language hint (optional)
  silence_threshold: 0.01
  silence_duration: 2.5  # seconds
  max_duration: 60       # seconds
  auto_submit: true      # Auto-submit in voice mode
```

- In `crates/ava-config/src/lib.rs`, add `VoiceConfig` struct

**Acceptance criteria:**
- Config loaded from file
- Defaults work without config
- Invalid config doesn't crash
- Add test

## Implementation Order

1. Story 1.1 (audio recording) — foundation
2. Story 1.2 (silence detection) — UX improvement
3. Story 2.1 (Whisper API) — transcription
4. Story 3.1 (TUI hotkey) — user-facing integration
5. Story 3.3 (voice config) — configurability
6. Story 3.2 (continuous voice mode) — advanced feature
7. Story 2.2 (local Whisper) — optional, do last

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Audio recording (`cpal`) should be feature-gated: `--features voice`
  - Without the feature, `Ctrl+V` shows a message: "Voice input requires --features voice"
  - This keeps the default binary small and avoids audio library dependencies
- Don't break existing TUI behavior
- Voice features are additive — everything works without voice enabled
- Local Whisper (`whisper-rs`) is a separate feature: `--features local-whisper`
- Handle "no microphone" gracefully — show error, don't crash

## New Dependencies

| Crate | Purpose | Feature-gated? |
|-------|---------|---------------|
| `cpal` | Cross-platform audio capture | `voice` |
| `whisper-rs` | Local Whisper inference | `local-whisper` |
| `hound` | WAV encoding | `voice` |

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Build without voice (default)
cargo build --bin ava

# Build with voice
cargo build --bin ava --features voice

# Test recording (requires microphone)
cargo run --bin ava --features voice -- --voice --provider openrouter --model anthropic/claude-sonnet-4
```
