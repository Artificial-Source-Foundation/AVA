use ava_tui::app::App;
use ava_tui::config::cli::{CliArgs, Command};
use ava_tui::headless::run_headless;
use ava_tui::review::run_review;
use clap::Parser;
use color_eyre::Result;
use std::io::IsTerminal;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_target(false)
        .compact()
        .init();

    let cli = CliArgs::parse();

    // Subcommand routing
    if let Some(Command::Review(args)) = cli.command.clone() {
        return run_review(args).await;
    }

    if cli.headless || cli.json || !std::io::stdout().is_terminal() {
        return run_headless(cli).await;
    }

    let mut app = App::new(cli).await?;
    app.run().await
}
