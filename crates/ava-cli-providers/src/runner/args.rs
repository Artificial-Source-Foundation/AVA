use super::{CLIAgentRunner, RunOptions};
use crate::config::PromptMode;

impl CLIAgentRunner {
    pub(crate) fn version_parts(&self) -> (&str, Vec<String>) {
        if self.config.version_command.is_empty() {
            return (&self.config.binary, vec!["--version".to_string()]);
        }

        let mut parts = self.config.version_command.iter();
        let program = parts
            .next()
            .map(String::as_str)
            .unwrap_or(self.config.binary.as_str());
        let args = parts.cloned().collect();
        (program, args)
    }

    /// Build the command args from config + options.
    pub(crate) fn build_args(&self, options: &RunOptions) -> Vec<String> {
        let mut args = Vec::new();

        match &self.config.prompt_flag {
            PromptMode::Flag(flag) => {
                args.push(flag.clone());
                args.push(options.prompt.clone());
            }
            PromptMode::Subcommand(cmd) => {
                args.push(cmd.clone());
                args.push(options.prompt.clone());
            }
        }

        args.extend(self.config.non_interactive_flags.clone());

        if options.yolo {
            args.extend(self.config.yolo_flags.clone());
        }

        if self.config.supports_stream_json {
            if let Some(flag) = &self.config.output_format_flag {
                args.push(flag.clone());
                args.push("stream-json".to_string());
            }
        }

        if let (Some(tools), Some(flag)) = (&options.allowed_tools, &self.config.allowed_tools_flag)
        {
            args.push(flag.clone());
            args.push(tools.join(","));
        }

        if let Some(flag) = &self.config.cwd_flag {
            args.push(flag.clone());
            args.push(options.cwd.clone());
        }

        if let (Some(model), Some(flag)) = (&options.model, &self.config.model_flag) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        if let (Some(session), Some(flag)) = (&options.session_id, &self.config.session_flag) {
            args.push(flag.clone());
            args.push(session.clone());
        }

        // --- Agent SDK extensions ---
        if let (Some(turns), Some(flag)) = (options.max_turns, &self.config.max_turns_flag) {
            args.push(flag.clone());
            args.push(turns.to_string());
        }

        if let (Some(mode), Some(flag)) =
            (&options.permission_mode, &self.config.permission_mode_flag)
        {
            args.push(flag.clone());
            args.push(mode.clone());
        }

        if let (Some(prompt), Some(flag)) =
            (&options.system_prompt, &self.config.system_prompt_flag)
        {
            args.push(flag.clone());
            args.push(prompt.clone());
        }

        if let (Some(session), Some(flag)) = (&options.resume_session, &self.config.resume_flag) {
            args.push(flag.clone());
            args.push(session.clone());
        }

        if options.continue_last {
            if let Some(flag) = &self.config.continue_flag {
                args.push(flag.clone());
            }
        }

        if let (Some(tools), Some(flag)) = (
            &options.disallowed_tools,
            &self.config.disallowed_tools_flag,
        ) {
            args.push(flag.clone());
            args.push(tools.join(","));
        }

        args
    }
}
