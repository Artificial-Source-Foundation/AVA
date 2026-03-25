use ava_tui::app::App;
use ava_tui::auth::run_auth;
#[cfg(feature = "benchmark")]
use ava_tui::benchmark;
use ava_tui::config::cli::{CliArgs, Command};
use ava_tui::headless::run_headless;
use ava_tui::plugin_commands::run_plugin;
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

    let is_benchmark = cfg!(feature = "benchmark") && (cli.benchmark || cli.harness);
    let is_tui = cli.command.is_none()
        && !cli.headless
        && !cli.json
        && !is_benchmark
        && std::io::stdout().is_terminal();

    // _log_guard MUST be held for the lifetime of main — dropping it loses buffered logs.
    let _log_guard = init_logging(is_tui, cli.verbose);

    // --trust: mark the current project as trusted before loading MCP/hooks
    if cli.trust {
        let cwd = std::env::current_dir().unwrap_or_default();
        if let Err(e) = ava_config::trust_project(&cwd) {
            eprintln!("Failed to trust project: {e}");
        } else {
            eprintln!("Trusted project: {}", cwd.display());
        }
    }

    // Background update check (non-blocking, once per 24h)
    if !cli.no_update_check {
        tokio::spawn(async {
            if let Some(msg) = ava_tui::updater::check_and_notify().await {
                eprintln!("{msg}");
            }
        });
    }

    // ACP server mode — run as an Agent Client Protocol server on stdio
    if cli.acp_server {
        return ava_acp::server::run_acp_server()
            .await
            .map_err(|e| color_eyre::eyre::eyre!("{e}"));
    }

    // Subcommand routing
    match cli.command.clone() {
        Some(Command::Update | Command::SelfUpdate) => {
            return ava_tui::updater::run_update_command().await;
        }
        Some(Command::Review(args)) => return run_review(args).await,
        Some(Command::Auth { action }) => return run_auth(action).await,
        Some(Command::Plugin { action }) => return run_plugin(action).await,
        #[cfg(feature = "web")]
        Some(Command::Serve { port, host }) => {
            return ava_tui::web::run_server(&host, port).await;
        }
        #[cfg(not(feature = "web"))]
        Some(Command::Serve { .. }) => {
            eprintln!("Web server requires the 'web' feature. Rebuild with:");
            eprintln!("  cargo build -p ava-tui --features web");
            std::process::exit(1);
        }
        None => {}
    }

    // Benchmark modes (only available with --features benchmark)
    #[cfg(feature = "benchmark")]
    {
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
            let suite = ava_tui::benchmark_tasks::BenchmarkSuite::parse_str(&cli.suite)
                .unwrap_or_else(|| {
                    eprintln!(
                        "Warning: unknown suite '{}', defaulting to 'all'",
                        cli.suite
                    );
                    ava_tui::benchmark_tasks::BenchmarkSuite::All
                });
            ava_tui::benchmark_harness::run_harness(
                director_spec,
                worker_spec,
                cli.max_turns,
                suite,
            )
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
            let suite = ava_tui::benchmark_tasks::BenchmarkSuite::parse_str(&cli.suite)
                .unwrap_or_else(|| {
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

            let language_filter = cli.language.as_deref().map(|lang_str| {
                lang_str
                    .split(',')
                    .filter_map(|s| {
                        let s = s.trim();
                        ava_tui::benchmark_tasks::Language::parse_str(s).or_else(|| {
                            eprintln!("Warning: unknown language '{}', skipping", s);
                            None
                        })
                    })
                    .collect::<Vec<_>>()
            });

            benchmark::run_benchmark(
                specs,
                None,
                cli.max_turns,
                judge_specs,
                suite,
                imported_tasks,
                language_filter,
            )
            .await?;
            return Ok(());
        }
    }

    if !is_tui {
        return run_headless(cli).await;
    }

    let mut app = App::new(cli).await?;
    app.run().await
}

/// Initialize logging:
/// - **File**: Always writes to `~/.ava/logs/ava-YYYY-MM-DD.log` (daily rotation, non-blocking)
/// - **Stderr**: Only in headless/CLI mode, controlled by `RUST_LOG` (default: warn).
///   NEVER enabled in TUI mode — stderr output corrupts the alternate screen.
/// - **Crash logs**: Written to `~/.ava/logs/crash-YYYY-MM-DDTHH-MM-SS.log` on panic.
///
/// Returns a guard that MUST be held for the lifetime of `main()`.
/// Dropping it flushes and closes the non-blocking file writer.
fn init_logging(is_tui: bool, verbose: u8) -> tracing_appender::non_blocking::WorkerGuard {
    let log_dir = dirs::home_dir()
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join(".ava")
        .join("logs");
    std::fs::create_dir_all(&log_dir).ok();

    // Non-blocking file writer with daily rotation.
    // Prefix "ava" produces files like `ava.2026-03-15.log`.
    let file_appender = tracing_appender::rolling::daily(&log_dir, "ava.log");
    let (file_writer, guard) = tracing_appender::non_blocking(file_appender);

    // File filter: RUST_LOG if set, otherwise AVA crates at debug + third-party at warn
    let file_filter = tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| {
        tracing_subscriber::EnvFilter::new(
            "info,ava_agent=debug,ava_llm=debug,ava_tui=debug,ava_tools=debug,\
             ava_praxis=debug,ava_config=debug,ava_session=debug,ava_context=debug,\
             ava_permissions=info,ava_mcp=info,ava_auth=info,ava_platform=info",
        )
    });

    // Format: [2026-03-15T18:30:00Z] [INFO] [crate::module] message
    let file_layer = tracing_subscriber::fmt::layer()
        .with_writer(file_writer)
        .with_target(true)
        .with_file(true)
        .with_line_number(true)
        .with_ansi(false)
        .with_timer(tracing_subscriber::fmt::time::UtcTime::rfc_3339())
        .with_filter(file_filter);

    if is_tui {
        // TUI mode: file only — no stderr output
        tracing_subscriber::registry().with(file_layer).init();
    } else {
        // Headless/CLI mode: file + stderr
        // --verbose/-v flag overrides RUST_LOG for stderr:
        //   -v  → info
        //   -vv → debug
        //   -vvv+ → trace
        let stderr_filter = if verbose > 0 {
            let level = match verbose {
                1 => "info",
                2 => "debug",
                _ => "trace",
            };
            tracing_subscriber::EnvFilter::new(level)
        } else {
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn"))
        };

        let stderr_layer = tracing_subscriber::fmt::layer()
            .with_writer(std::io::stderr)
            .with_target(false)
            .compact()
            .with_filter(stderr_filter);

        tracing_subscriber::registry()
            .with(file_layer)
            .with(stderr_layer)
            .init();
    }

    tracing::info!(
        "AVA logging initialized — tui={is_tui}, log dir: {}",
        log_dir.display()
    );

    guard
}
