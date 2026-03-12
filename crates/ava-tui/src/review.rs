use std::sync::Arc;

use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_platform::StandardPlatform;
use ava_praxis::review::{
    build_review_system_prompt, collect_diff, determine_exit_code, format_json, format_markdown,
    format_text, parse_review_output, run_review_agent,
};
use ava_praxis::DiffMode;
use color_eyre::eyre::{eyre, Result};

use crate::config::cli::{ReviewArgs, ReviewFormat};

pub async fn run_review(args: ReviewArgs) -> Result<()> {
    // 1. Validate git repo
    let git_check = tokio::process::Command::new("git")
        .args(["rev-parse", "--is-inside-work-tree"])
        .output()
        .await;

    match git_check {
        Ok(output) if output.status.success() => {}
        _ => return Err(eyre!("Not inside a git repository")),
    }

    // 2. Determine diff mode
    let mode = if let Some(ref range) = args.diff {
        DiffMode::Range(range.clone())
    } else if let Some(ref sha) = args.commit {
        DiffMode::Commit(sha.clone())
    } else if args.working {
        DiffMode::Working
    } else {
        // Default to staged
        DiffMode::Staged
    };

    // 3. Collect diff
    eprintln!("[review] Collecting diff...");
    let review_context = collect_diff(&mode).await.map_err(|e| eyre!("{e}"))?;

    eprintln!(
        "[review] {} file(s) changed, {} bytes of diff",
        review_context.stats.len(),
        review_context.diff.len()
    );

    // 4. Resolve provider
    let (provider, model) =
        crate::config::cli::resolve_provider_model(args.provider.as_deref(), args.model.as_deref())
            .await?;

    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let (stack, _question_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: args.max_turns,
        yolo: true, // Review agent doesn't need approval prompts
        ..Default::default()
    })
    .await?;

    let (provider_name, model_name) = stack.current_model().await;
    let resolved_provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await?;

    eprintln!("[review] Using {provider_name}/{model_name}");

    // 5. Build system prompt and run agent
    let system_prompt = build_review_system_prompt(&args.focus);
    let platform = Arc::new(StandardPlatform);

    let output = run_review_agent(
        resolved_provider,
        platform,
        &review_context,
        &system_prompt,
        args.max_turns,
    )
    .await
    .map_err(|e| eyre!("Review agent failed: {e}"))?;

    // 6. Parse and format output
    let result = parse_review_output(&output);

    let formatted = match args.format {
        ReviewFormat::Text => format_text(&result),
        ReviewFormat::Json => format_json(&result),
        ReviewFormat::Markdown => format_markdown(&result),
    };

    println!("{formatted}");

    // 7. Exit with appropriate code
    let threshold = args.fail_on.to_severity();
    let exit_code = determine_exit_code(&result, threshold);

    std::process::exit(exit_code);
}
