use ava_tui::app::App;
use clap::Parser;
use color_eyre::Result;
use ava_tui::config::cli::CliArgs;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_target(false)
        .compact()
        .init();

    let cli = CliArgs::parse();
    let mut app = App::new(cli).await?;
    app.run().await
}
