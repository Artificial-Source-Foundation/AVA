use ava_hq::{bootstrap_hq_memory, HqMemoryBootstrapOptions};
use color_eyre::Result;

use crate::config::cli::HqCommand;

pub async fn run_hq_command(cmd: HqCommand) -> Result<()> {
    match cmd {
        HqCommand::Init {
            director_model,
            force,
        } => run_hq_init(director_model, force).await,
    }
}

async fn run_hq_init(director_model: Option<String>, force: bool) -> Result<()> {
    let project_root = std::env::current_dir()?;
    let result = bootstrap_hq_memory(
        &project_root,
        &HqMemoryBootstrapOptions {
            director_model,
            force,
        },
    )
    .await?;

    if result.reused_existing {
        println!("HQ memory already exists at {}", result.hq_root);
        println!("Use `ava hq init --force` to rewrite the starter files.");
        return Ok(());
    }

    println!("Initialized HQ memory for {}", result.project_name);
    println!("Project root: {}", result.project_root);
    println!("HQ root: {}", result.hq_root);
    println!();
    println!("Detected stack:");
    for item in &result.stack_summary {
        println!("- {item}");
    }
    println!();
    println!("Created files:");
    for path in &result.created_files {
        println!("- {path}");
    }

    Ok(())
}
