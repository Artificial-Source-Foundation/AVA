use ava_types::{Message, Role};
use serde_json::{json, Value};

fn map_role(role: &Role) -> &'static str {
    match role {
        Role::System => "system",
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
    }
}

pub fn map_messages_openai(messages: &[Message]) -> Vec<Value> {
    messages
        .iter()
        .map(|message| {
            // Assistant messages with native tool calls
            if message.role == Role::Assistant && !message.tool_calls.is_empty() {
                let tool_calls: Vec<Value> = message
                    .tool_calls
                    .iter()
                    .map(|tc| {
                        json!({
                            "id": tc.id,
                            "type": "function",
                            "function": {
                                "name": tc.name,
                                "arguments": tc.arguments.to_string(),
                            }
                        })
                    })
                    .collect();
                // Always include content (null when empty) — some providers
                // reject assistant messages with tool_calls but no content field.
                let content: Value = if message.content.is_empty() {
                    Value::Null
                } else {
                    json!(message.content)
                };
                return json!({
                    "role": "assistant",
                    "content": content,
                    "tool_calls": tool_calls,
                });
            }

            // Tool result messages
            if message.role == Role::Tool {
                let tool_call_id = message
                    .tool_call_id
                    .as_deref()
                    .filter(|id| !id.is_empty())
                    .unwrap_or("unknown");
                return json!({
                    "role": "tool",
                    "content": message.content,
                    "tool_call_id": tool_call_id,
                });
            }

            json!({
                "role": map_role(&message.role),
                "content": message.content,
            })
        })
        .collect()
}

pub fn map_messages_anthropic(messages: &[Message]) -> (Option<String>, Vec<Value>) {
    let mut system_parts = Vec::new();
    let mut mapped = Vec::new();

    for message in messages {
        if message.role == Role::System {
            system_parts.push(message.content.clone());
            continue;
        }

        // Assistant messages with native tool calls
        if message.role == Role::Assistant && !message.tool_calls.is_empty() {
            let mut content: Vec<Value> = Vec::new();
            if !message.content.is_empty() {
                content.push(json!({"type": "text", "text": message.content}));
            }
            for tc in &message.tool_calls {
                content.push(json!({
                    "type": "tool_use",
                    "id": tc.id,
                    "name": tc.name,
                    "input": tc.arguments,
                }));
            }
            mapped.push(json!({"role": "assistant", "content": content}));
            continue;
        }

        // Tool result messages
        if message.role == Role::Tool {
            let content = vec![json!({
                "type": "tool_result",
                "tool_use_id": message.tool_call_id.as_deref().unwrap_or(""),
                "content": message.content,
            })];
            mapped.push(json!({"role": "user", "content": content}));
            continue;
        }

        let role = if matches!(message.role, Role::Assistant) {
            "assistant"
        } else {
            "user"
        };

        mapped.push(json!({
            "role": role,
            "content": message.content,
        }));
    }

    let system = if system_parts.is_empty() {
        None
    } else {
        Some(system_parts.join("\n"))
    };

    (system, mapped)
}

pub fn map_messages_gemini_parts(messages: &[Message]) -> (Option<Value>, Vec<Value>) {
    let mut system_parts = Vec::new();
    let mut mapped = Vec::new();

    for message in messages {
        if message.role == Role::System {
            system_parts.push(json!({"text": message.content}));
            continue;
        }

        mapped.push(json!({
            "role": if matches!(message.role, Role::Assistant) { "model" } else { "user" },
            "parts": [{"text": message.content}],
        }));
    }

    let system_instruction = if system_parts.is_empty() {
        None
    } else {
        Some(json!({"parts": system_parts}))
    };

    (system_instruction, mapped)
}
