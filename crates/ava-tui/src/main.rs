use ava_tui::app::App;
use ava_tui::auth::run_auth;
use ava_tui::benchmark;
use ava_tui::config::cli::{CliArgs, Command};
use ava_tui::headless::run_headless;
use ava_tui::review::run_review;
use clap::Parser;
use color_eyre::Result;
use std::io::IsTerminal;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::util::SubscriberInitExt;
use tracing_subscriber::Layer;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;

    let cli = CliArgs::parse();

    let is_tui = cli.command.is_none()
        && !cli.headless
        && !cli.json
        && !cli.benchmark
        && !cli.harness
        && std::io::stdout().is_terminal();

    init_logging(is_tui);

    // Subcommand routing
    match cli.command.clone() {
        Some(Command::Review(args)) => return run_review(args).await,
        Some(Command::Auth { action }) => return run_auth(action).await,
        None => {}
    }

    // Harnessed-pair benchmark mode
    if cli.harness {
        let director_str = cli.director.as_deref().ok_or_else(|| {
            color_eyre::eyre::eyre!(
                "Missing --director flag. Usage: ava --harness --director \"openrouter:anthropic/claude-opus-4.6\" --worker \"inception:mercury-2\""
            )
        })?;
        let worker_str = cli.worker.as_deref().ok_or_else(|| {
            color_eyre::eyre::eyre!(
                "Missing --worker flag. Usage: ava --harness --director \"openrouter:anthropic/claude-opus-4.6\" --worker \"inception:mercury-2\""
            )
        })?;
        let director_spec = ava_tui::benchmark_harness::parse_single_model_spec(director_str)?;
        let worker_spec = ava_tui::benchmark_harness::parse_single_model_spec(worker_str)?;
        let suite =
            ava_tui::benchmark_tasks::BenchmarkSuite::from_str(&cli.suite).unwrap_or_else(|| {
                eprintln!(
                    "Warning: unknown suite '{}', defaulting to 'all'",
                    cli.suite
                );
                ava_tui::benchmark_tasks::BenchmarkSuite::All
            });
        ava_tui::benchmark_harness::run_harness(director_spec, worker_spec, cli.max_turns, suite)
            .await?;
        return Ok(());
    }

    // Benchmark mode
    if cli.benchmark {
        let specs = benchmark::parse_model_specs(
            cli.provider.as_deref(),
            cli.model.as_deref(),
            cli.models.as_deref(),
        )?;
        let judge_specs = benchmark::parse_judge_specs(cli.judges.as_deref())?;
        let suite =
            ava_tui::benchmark_tasks::BenchmarkSuite::from_str(&cli.suite).unwrap_or_else(|| {
                eprintln!(
                    "Warning: unknown suite '{}', defaulting to 'all'",
                    cli.suite
                );
                ava_tui::benchmark_tasks::BenchmarkSuite::All
            });

        // Import external benchmark tasks if requested
        let imported_tasks = if let Some(ref polyglot_path) = cli.import_polyglot {
            ava_tui::benchmark_import::import_polyglot(std::path::Path::new(polyglot_path))?
        } else {
            Vec::new()
        };

        benchmark::run_benchmark(specs, None, cli.max_turns, judge_specs, suite, imported_tasks)
            .await?;
        return Ok(());
    }

    if !is_tui {
        return run_headless(cli).await;
    }

    let mut app = App::new(cli).await?;
    app.run().await
}

/// Initialize logging:
/// - **File**: Always writes debug+ logs to `~/.ava/logs/ava.log` (daily rotation)
/// - **Stderr**: Only in headless/CLI mode, controlled by `RUST_LOG` (default: warn).
///   NEVER enabled in TUI mode — stderr output corrupts the alternate screen.
fn init_logging(is_tui: bool) {
    let log_dir = dirs::home_dir()
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join(".ava")
        .join("logs");

    // File layer: AVA crates at debug, third-party at warn
    let file_appender = tracing_appender::rolling::daily(&log_dir, "ava.log");
    let file_filter = tracing_subscriber::EnvFilter::new(
        "warn,ava_agent=debug,ava_llm=debug,ava_tui=debug,ava_tools=debug,\
         ava_praxis=debug,ava_config=debug,ava_session=debug,ava_context=debug,\
         ava_permissions=info,ava_mcp=info,ava_auth=info,ava_platform=info",
    );
    let file_layer = tracing_subscriber::fmt::layer()
        .with_writer(file_appender)
        .with_target(true)
        .with_ansi(false)
        .with_filter(file_filter);

    if is_tui {
        // TUI mode: file only — no stderr output
        tracing_subscriber::registry().with(file_layer).init();
    } else {
        // Headless/CLI mode: file + stderr
        let stderr_layer = tracing_subscriber::fmt::layer()
            .with_target(false)
            .compact()
            .with_filter(
                tracing_subscriber::EnvFilter::try_from_default_env()
                    .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn")),
            );

        tracing_subscriber::registry()
            .with(file_layer)
            .with(stderr_layer)
            .init();
    }

    tracing::info!(
        "AVA logging initialized — tui={is_tui}, log dir: {}",
        log_dir.display()
    );
}
