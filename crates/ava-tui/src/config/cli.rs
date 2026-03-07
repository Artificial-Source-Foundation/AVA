use clap::Parser;

#[derive(Debug, Clone, Parser)]
#[command(name = "ava", about = "AVA - AI coding assistant")]
pub struct CliArgs {
    /// Goal to execute immediately
    pub goal: Option<String>,

    /// Resume last session
    #[arg(short = 'c', long = "continue")]
    pub resume: bool,

    /// Resume a specific session id
    #[arg(long)]
    pub session: Option<String>,

    /// Model to use
    #[arg(long, short)]
    pub model: Option<String>,

    /// Provider to use
    #[arg(long)]
    pub provider: Option<String>,

    /// Maximum agent turns
    #[arg(long, default_value_t = 20)]
    pub max_turns: usize,

    /// Auto-approve all tools
    #[arg(long)]
    pub yolo: bool,

    /// Theme name
    #[arg(long, default_value = "default")]
    pub theme: String,
}
