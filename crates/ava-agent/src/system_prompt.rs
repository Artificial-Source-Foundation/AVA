use ava_types::Tool;

/// Build a system prompt that tells the LLM it's an AI coding agent with tools.
///
/// For providers with native tool calling, this prompt is shorter (tool defs
/// are sent via the API). For text-only providers, tool schemas are embedded
/// in the prompt with the JSON envelope format.
pub fn build_system_prompt(tools: &[Tool], native_tools: bool) -> String {
    let mut prompt = String::with_capacity(2048);

    prompt.push_str(
        "You are AVA, an AI coding assistant. You help users with software engineering tasks \
         by reading files, writing code, running commands, and searching codebases.\n\n",
    );

    prompt.push_str("## Rules\n");
    prompt.push_str("- Read files before modifying them.\n");
    prompt.push_str("- Prefer editing existing files over creating new ones.\n");
    prompt.push_str("- Prefer native tools (read, write, edit, glob, grep) over bash equivalents when possible. Native tools are faster, sandboxed, and produce structured output.\n");
    prompt.push_str("- Run tests after making changes when possible.\n");
    prompt.push_str(
        "- When your task is complete, call the `attempt_completion` tool with a `result` \
         describing what you did.\n",
    );
    prompt.push_str("- If you have nothing more to do, call `attempt_completion`.\n\n");

    if native_tools {
        // Provider sends tool definitions via API — just list names for awareness.
        prompt.push_str("## Available Tools\n");
        for tool in tools {
            prompt.push_str(&format!("- **{}**: {}\n", tool.name, tool.description));
        }
        prompt.push('\n');
    } else {
        // Text-only provider — embed full schemas and specify the JSON envelope.
        prompt.push_str("## Tools\n\n");
        prompt.push_str(
            "To call tools, respond with ONLY a JSON object in this exact format:\n\
             ```json\n\
             {\"tool_calls\": [{\"name\": \"tool_name\", \"arguments\": {\"param\": \"value\"}}]}\n\
             ```\n\n\
             Do NOT mix tool calls with natural text. Either respond with a JSON tool call \
             or with natural text, never both.\n\n",
        );

        for tool in tools {
            prompt.push_str(&format!("### {}\n", tool.name));
            prompt.push_str(&format!("{}\n", tool.description));
            prompt.push_str(&format!(
                "Parameters: {}\n\n",
                serde_json::to_string(&tool.parameters).unwrap_or_else(|_| "{}".to_string())
            ));
        }
    }

    // Always include attempt_completion since it's a virtual tool.
    if !tools.iter().any(|t| t.name == "attempt_completion") {
        prompt.push_str("### attempt_completion\n");
        prompt.push_str(
            "Call this when you have completed the task. \
             Parameters: {\"result\": \"description of what you did\"}\n",
        );
    }

    prompt
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn mock_tools() -> Vec<Tool> {
        vec![
            Tool {
                name: "read".to_string(),
                description: "Read a file from disk".to_string(),
                parameters: json!({
                    "type": "object",
                    "properties": {
                        "path": {"type": "string", "description": "File path"}
                    },
                    "required": ["path"]
                }),
            },
            Tool {
                name: "bash".to_string(),
                description: "Execute a shell command".to_string(),
                parameters: json!({
                    "type": "object",
                    "properties": {
                        "command": {"type": "string", "description": "Command to run"}
                    },
                    "required": ["command"]
                }),
            },
        ]
    }

    #[test]
    fn text_prompt_contains_tool_names_and_schemas() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, false);

        assert!(prompt.contains("read"));
        assert!(prompt.contains("bash"));
        assert!(prompt.contains("Read a file from disk"));
        assert!(prompt.contains("Execute a shell command"));
        assert!(prompt.contains("tool_calls"));
        assert!(prompt.contains("attempt_completion"));
        assert!(prompt.contains("\"path\""));
    }

    #[test]
    fn native_prompt_lists_tools_without_json_envelope() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, true);

        assert!(prompt.contains("read"));
        assert!(prompt.contains("bash"));
        // Should NOT contain the JSON envelope instructions
        assert!(!prompt.contains("```json"));
        assert!(prompt.contains("attempt_completion"));
    }

    #[test]
    fn prompt_skips_attempt_completion_instructions_when_registered() {
        let tools = vec![Tool {
            name: "attempt_completion".to_string(),
            description: "Signal task completion".to_string(),
            parameters: json!({}),
        }];
        let prompt = build_system_prompt(&tools, false);
        // Should only appear once (as part of the tool list, not the fallback section)
        let count = prompt.matches("### attempt_completion").count();
        assert_eq!(count, 1);
    }

    #[test]
    fn prompt_is_concise() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, false);
        // Should be well under 2000 tokens (~8000 chars) for 2 tools
        assert!(prompt.len() < 4000, "prompt too long: {} chars", prompt.len());
    }
}
