use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use crate::event::AppEvent;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use color_eyre::eyre::{eyre, Result};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub(super) async fn run_voice_loop(cli: CliArgs) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let voice_config = ava_config::VoiceConfig::default();
    let transcriber = crate::transcribe::create_transcriber(&voice_config).await?;

    eprintln!("[voice] Continuous voice mode. Speak into your microphone. Ctrl+C to exit.");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\n[voice] Stopping...");
        cancel_clone.cancel();
    });

    loop {
        if cancel.is_cancelled() {
            break;
        }

        eprintln!("[voice] Listening...");

        let (audio_tx, mut audio_rx) = mpsc::unbounded_channel();
        let mut recorder = crate::audio::AudioRecorder::start(
            audio_tx,
            voice_config.silence_threshold,
            voice_config.silence_duration_secs,
            voice_config.max_duration_secs,
        )
        .map_err(|e| eyre!("{e}"))?;

        loop {
            match audio_rx.recv().await {
                Some(AppEvent::VoiceSilenceDetected) => break,
                Some(AppEvent::VoiceError(e)) => {
                    eprintln!("[voice] Error: {e}");
                    break;
                }
                Some(_) => {}
                None => break,
            }
        }

        let wav = recorder.stop().map_err(|e| eyre!("{e}"))?;
        eprintln!("[voice] Transcribing...");

        let text = match transcriber
            .transcribe(wav, voice_config.language.as_deref())
            .await
        {
            Ok(t) => t,
            Err(e) => {
                eprintln!("[voice] Transcription error: {e}");
                continue;
            }
        };

        let text = text.trim().to_string();
        if text.is_empty() {
            eprintln!("[voice] (no speech detected)");
            continue;
        }

        eprintln!("[voice] Goal: {text}");

        let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
            data_dir: data_dir.clone(),
            provider: provider.clone(),
            model: model.clone(),
            max_turns: cli.max_turns,
            max_budget_usd: cli.max_budget_usd,
            yolo: cli.auto_approve,
            ..Default::default()
        })
        .await?;
        spawn_auto_approve_requests(approval_rx);

        let (tx, mut rx) = mpsc::unbounded_channel();
        let agent_cancel = CancellationToken::new();

        let goal = text.clone();
        let max_turns = cli.max_turns;
        let handle = tokio::spawn(async move {
            stack
                .run(
                    &goal,
                    max_turns,
                    Some(tx),
                    agent_cancel,
                    Vec::new(),
                    None,
                    Vec::new(),
                )
                .await
        });

        while let Some(event) = rx.recv().await {
            match &event {
                AgentEvent::Token(t) => print!("{t}"),
                AgentEvent::Complete(_) => break,
                AgentEvent::Error(e) => {
                    eprintln!("[error: {e}]");
                    break;
                }
                _ => {}
            }
        }
        println!();

        let result = handle.await??;
        eprintln!(
            "[voice] Done — success={}, turns={}",
            result.success, result.turns
        );
    }

    Ok(())
}
