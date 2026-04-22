use super::{
    resolve_headless_startup_selection, single::persist_headless_session,
    spawn_auto_approve_requests, update_headless_resume_context_from_session,
};
use crate::config::cli::CliArgs;
use crate::event::AppEvent;
use ava_agent::AgentEvent;
use ava_agent_orchestration::stack::{AgentStack, AgentStackConfig};
use color_eyre::eyre::{eyre, Result};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::warn;

fn finalize_voice_headless_run<F>(
    resume_session_id: &mut Option<uuid::Uuid>,
    resume_history: &mut Vec<ava_types::Message>,
    session: &mut ava_types::Session,
    provider: &str,
    model: &str,
    primary_agent_id: Option<&str>,
    primary_agent_prompt: Option<&str>,
    persist_session: F,
) -> Result<()>
where
    F: FnOnce(&mut ava_types::Session, &str, &str, Option<&str>, Option<&str>) -> Result<()>,
{
    persist_session(
        session,
        provider,
        model,
        primary_agent_id,
        primary_agent_prompt,
    )?;
    update_headless_resume_context_from_session(resume_session_id, resume_history, session);
    Ok(())
}

pub(super) async fn run_voice_loop(cli: CliArgs) -> Result<()> {
    let data_dir = ava_config::data_dir().unwrap_or_default();
    let startup = resolve_headless_startup_selection(&cli).await?;
    let mut resume_session_id = startup.resume_session_id;
    let mut resume_history = startup.resume_history;
    let resume_restore_model = startup.resume_restore_model;
    let startup_selection = startup.startup;
    let provider = startup_selection.provider.clone();
    let model = startup_selection.model.clone();
    let startup_primary_agent_id = startup_selection.primary_agent_id.clone();
    let startup_primary_agent_prompt = startup_selection.primary_agent_prompt.clone();
    let runtime_lean = cli.runtime_lean_settings();
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

        let (stack, _question_rx, approval_rx, _plan_rx) =
            AgentStack::new(AgentStackConfig::for_headless(
                data_dir.clone(),
                provider.clone(),
                model.clone(),
                cli.max_turns,
                cli.max_budget_usd,
                cli.auto_approve,
                runtime_lean.include_project_instructions,
                runtime_lean.eager_codebase_indexing,
            ))
            .await?;
        let _ = stack
            .set_startup_prompt_suffix(startup_selection.primary_agent_prompt.clone())
            .await;

        if let Some((provider, model)) = resume_restore_model.clone() {
            if let Err(err) = stack.switch_model(&provider, &model).await {
                warn!(
                    provider = %provider,
                    model = %model,
                    error = %err,
                    "failed to restore model from resumed session in voice headless mode; continuing with startup model"
                );
            }
        }

        spawn_auto_approve_requests(approval_rx);

        let (tx, mut rx) = mpsc::unbounded_channel();
        let agent_cancel = CancellationToken::new();

        let goal = text.clone();
        let max_turns = cli.max_turns;
        let run_history = resume_history.clone();
        let run_session_id = resume_session_id;
        let handle = tokio::spawn(async move {
            stack
                .run(
                    &goal,
                    max_turns,
                    Some(tx),
                    agent_cancel,
                    run_history,
                    None,
                    Vec::new(),
                    run_session_id,
                    None,
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

        let mut result = handle.await??;
        let (active_provider, active_model) = stack.current_model().await;
        finalize_voice_headless_run(
            &mut resume_session_id,
            &mut resume_history,
            &mut result.session,
            &active_provider,
            &active_model,
            startup_primary_agent_id.as_deref(),
            startup_primary_agent_prompt.as_deref(),
            persist_headless_session,
        )?;
        eprintln!(
            "[voice] Done — success={}, turns={}",
            result.success, result.turns
        );
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::finalize_voice_headless_run;

    #[test]
    fn finalize_voice_headless_run_updates_resume_state_after_persist() {
        let mut resume_session_id = None;
        let mut resume_history = vec![ava_types::Message::new(ava_types::Role::User, "stale")];
        let mut session = ava_types::Session::new();
        session.add_message(ava_types::Message::new(ava_types::Role::User, "fresh"));

        let mut persist_called = false;
        finalize_voice_headless_run(
            &mut resume_session_id,
            &mut resume_history,
            &mut session,
            "openrouter",
            "anthropic/claude-sonnet-4",
            Some("architect"),
            Some("You are the architect profile"),
            |_session, provider, model, agent_id, agent_prompt| {
                persist_called = true;
                assert_eq!(provider, "openrouter");
                assert_eq!(model, "anthropic/claude-sonnet-4");
                assert_eq!(agent_id, Some("architect"));
                assert_eq!(agent_prompt, Some("You are the architect profile"));
                Ok(())
            },
        )
        .expect("voice headless finalization should succeed");

        assert!(persist_called);
        assert_eq!(resume_session_id, Some(session.id));
        assert_eq!(resume_history, session.messages);
    }
}
