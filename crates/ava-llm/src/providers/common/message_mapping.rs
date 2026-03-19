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

            // If user message has images, use content parts format
            if message.role == Role::User && !message.images.is_empty() {
                let mut content: Vec<Value> = Vec::new();
                if !message.content.is_empty() {
                    content.push(json!({"type": "text", "text": message.content}));
                }
                for img in &message.images {
                    let data_url = format!("data:{};base64,{}", img.media_type.as_mime(), img.data);
                    content.push(json!({
                        "type": "image_url",
                        "image_url": { "url": data_url }
                    }));
                }
                return json!({
                    "role": map_role(&message.role),
                    "content": content,
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

        // If user message has images, use content blocks format
        if message.role == Role::User && !message.images.is_empty() {
            let mut content: Vec<Value> = Vec::new();
            if !message.content.is_empty() {
                content.push(json!({"type": "text", "text": message.content}));
            }
            for img in &message.images {
                content.push(json!({
                    "type": "image",
                    "source": {
                        "type": "base64",
                        "media_type": img.media_type.as_mime(),
                        "data": img.data,
                    }
                }));
            }
            mapped.push(json!({"role": role, "content": content}));
            continue;
        }

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

        // Assistant messages with native tool calls -> functionCall parts
        if message.role == Role::Assistant && !message.tool_calls.is_empty() {
            let mut parts: Vec<Value> = Vec::new();
            if !message.content.is_empty() {
                parts.push(json!({"text": message.content}));
            }
            for tc in &message.tool_calls {
                let mut fc = json!({
                    "name": tc.name,
                    "args": tc.arguments,
                });
                if !tc.id.is_empty() {
                    fc["id"] = json!(tc.id);
                }
                parts.push(json!({"functionCall": fc}));
            }
            mapped.push(json!({
                "role": "model",
                "parts": parts,
            }));
            continue;
        }

        // Tool result messages -> functionResponse parts
        if message.role == Role::Tool {
            let tool_call_id = message.tool_call_id.as_deref().unwrap_or("");
            let response_value = serde_json::from_str::<Value>(&message.content)
                .unwrap_or_else(|_| json!({"result": message.content}));
            let mut fr = json!({
                "name": tool_call_id,
                "response": response_value,
            });
            if !tool_call_id.is_empty() {
                fr["id"] = json!(tool_call_id);
            }
            mapped.push(json!({
                "role": "user",
                "parts": [{"functionResponse": fr}],
            }));
            continue;
        }

        // If user message has images, add image parts
        if message.role == Role::User && !message.images.is_empty() {
            let mut parts: Vec<Value> = Vec::new();
            if !message.content.is_empty() {
                parts.push(json!({"text": message.content}));
            }
            for img in &message.images {
                parts.push(json!({
                    "inlineData": {
                        "mimeType": img.media_type.as_mime(),
                        "data": img.data,
                    }
                }));
            }
            mapped.push(json!({
                "role": "user",
                "parts": parts,
            }));
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

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::ToolCall;

    fn msg(role: Role, content: &str) -> Message {
        Message::new(role, content)
    }

    fn assistant_with_tool_calls(content: &str, calls: Vec<ToolCall>) -> Message {
        Message::new(Role::Assistant, content).with_tool_calls(calls)
    }

    fn tool_result(content: &str, call_id: &str) -> Message {
        Message::new(Role::Tool, content).with_tool_call_id(call_id)
    }

    fn sample_tool_call() -> ToolCall {
        ToolCall {
            id: "call_1".to_string(),
            name: "read_file".to_string(),
            arguments: json!({"path": "/tmp/test"}),
        }
    }

    // ── map_messages_openai ──

    #[test]
    fn openai_simple_user_message() {
        let messages = vec![msg(Role::User, "hello")];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
        assert_eq!(mapped[0]["content"], "hello");
    }

    #[test]
    fn openai_system_and_assistant() {
        let messages = vec![
            msg(Role::System, "You are helpful"),
            msg(Role::Assistant, "Hi!"),
        ];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped.len(), 2);
        assert_eq!(mapped[0]["role"], "system");
        assert_eq!(mapped[1]["role"], "assistant");
    }

    #[test]
    fn openai_assistant_with_tool_calls() {
        let messages = vec![assistant_with_tool_calls(
            "Reading file",
            vec![sample_tool_call()],
        )];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped[0]["role"], "assistant");
        assert_eq!(mapped[0]["content"], "Reading file");
        let tc = &mapped[0]["tool_calls"];
        assert!(tc.is_array());
        assert_eq!(tc[0]["id"], "call_1");
        assert_eq!(tc[0]["type"], "function");
        assert_eq!(tc[0]["function"]["name"], "read_file");
    }

    #[test]
    fn openai_assistant_with_tool_calls_empty_content() {
        let messages = vec![assistant_with_tool_calls("", vec![sample_tool_call()])];
        let mapped = map_messages_openai(&messages);
        assert!(mapped[0]["content"].is_null());
    }

    #[test]
    fn openai_tool_result_message() {
        let messages = vec![tool_result("file contents", "call_1")];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped[0]["role"], "tool");
        assert_eq!(mapped[0]["content"], "file contents");
        assert_eq!(mapped[0]["tool_call_id"], "call_1");
    }

    #[test]
    fn openai_tool_result_no_call_id() {
        let messages = vec![msg(Role::Tool, "result")];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped[0]["tool_call_id"], "unknown");
    }

    #[test]
    fn openai_empty_messages() {
        assert!(map_messages_openai(&[]).is_empty());
    }

    #[test]
    fn openai_full_conversation() {
        let messages = vec![
            msg(Role::System, "system prompt"),
            msg(Role::User, "do something"),
            assistant_with_tool_calls("", vec![sample_tool_call()]),
            tool_result("done", "call_1"),
            msg(Role::Assistant, "All done."),
        ];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped.len(), 5);
        assert_eq!(mapped[0]["role"], "system");
        assert_eq!(mapped[1]["role"], "user");
        assert_eq!(mapped[2]["role"], "assistant");
        assert!(mapped[2]["tool_calls"].is_array());
        assert_eq!(mapped[3]["role"], "tool");
        assert_eq!(mapped[4]["role"], "assistant");
    }

    // ── map_messages_anthropic ──

    #[test]
    fn anthropic_extracts_system() {
        let messages = vec![msg(Role::System, "You are helpful"), msg(Role::User, "hi")];
        let (system, mapped) = map_messages_anthropic(&messages);
        assert_eq!(system.unwrap(), "You are helpful");
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
    }

    #[test]
    fn anthropic_multiple_system_messages_joined() {
        let messages = vec![
            msg(Role::System, "Part 1"),
            msg(Role::System, "Part 2"),
            msg(Role::User, "hi"),
        ];
        let (system, mapped) = map_messages_anthropic(&messages);
        assert_eq!(system.unwrap(), "Part 1\nPart 2");
        assert_eq!(mapped.len(), 1);
    }

    #[test]
    fn anthropic_no_system() {
        let messages = vec![msg(Role::User, "hi")];
        let (system, mapped) = map_messages_anthropic(&messages);
        assert!(system.is_none());
        assert_eq!(mapped.len(), 1);
    }

    #[test]
    fn anthropic_assistant_with_tool_calls() {
        let messages = vec![assistant_with_tool_calls("text", vec![sample_tool_call()])];
        let (_, mapped) = map_messages_anthropic(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content[0]["type"], "text");
        assert_eq!(content[0]["text"], "text");
        assert_eq!(content[1]["type"], "tool_use");
        assert_eq!(content[1]["name"], "read_file");
    }

    #[test]
    fn anthropic_assistant_with_tool_calls_no_text() {
        let messages = vec![assistant_with_tool_calls("", vec![sample_tool_call()])];
        let (_, mapped) = map_messages_anthropic(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        // No text block when content is empty
        assert_eq!(content.len(), 1);
        assert_eq!(content[0]["type"], "tool_use");
    }

    #[test]
    fn anthropic_tool_result_mapped_to_user() {
        let messages = vec![tool_result("output", "call_1")];
        let (_, mapped) = map_messages_anthropic(&messages);
        assert_eq!(mapped[0]["role"], "user");
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content[0]["type"], "tool_result");
        assert_eq!(content[0]["tool_use_id"], "call_1");
    }

    #[test]
    fn anthropic_tool_result_no_call_id() {
        let messages = vec![msg(Role::Tool, "result")];
        let (_, mapped) = map_messages_anthropic(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content[0]["tool_use_id"], "");
    }

    #[test]
    fn anthropic_empty_messages() {
        let (system, mapped) = map_messages_anthropic(&[]);
        assert!(system.is_none());
        assert!(mapped.is_empty());
    }

    // ── map_messages_gemini_parts ──

    #[test]
    fn gemini_extracts_system_instruction() {
        let messages = vec![msg(Role::System, "Be concise"), msg(Role::User, "hi")];
        let (system, mapped) = map_messages_gemini_parts(&messages);
        let sys = system.unwrap();
        assert_eq!(sys["parts"][0]["text"], "Be concise");
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
    }

    #[test]
    fn gemini_no_system() {
        let messages = vec![msg(Role::User, "hi")];
        let (system, mapped) = map_messages_gemini_parts(&messages);
        assert!(system.is_none());
        assert_eq!(mapped.len(), 1);
    }

    #[test]
    fn gemini_assistant_maps_to_model() {
        let messages = vec![msg(Role::Assistant, "response")];
        let (_, mapped) = map_messages_gemini_parts(&messages);
        assert_eq!(mapped[0]["role"], "model");
        assert_eq!(mapped[0]["parts"][0]["text"], "response");
    }

    #[test]
    fn gemini_user_stays_user() {
        let messages = vec![msg(Role::User, "question")];
        let (_, mapped) = map_messages_gemini_parts(&messages);
        assert_eq!(mapped[0]["role"], "user");
    }

    #[test]
    fn gemini_tool_role_maps_to_user() {
        let messages = vec![msg(Role::Tool, "result")];
        let (_, mapped) = map_messages_gemini_parts(&messages);
        assert_eq!(mapped[0]["role"], "user");
    }

    #[test]
    fn gemini_multiple_system_parts() {
        let messages = vec![
            msg(Role::System, "Part A"),
            msg(Role::System, "Part B"),
            msg(Role::User, "hi"),
        ];
        let (system, _) = map_messages_gemini_parts(&messages);
        let parts = system.unwrap()["parts"].as_array().unwrap().clone();
        assert_eq!(parts.len(), 2);
        assert_eq!(parts[0]["text"], "Part A");
        assert_eq!(parts[1]["text"], "Part B");
    }

    #[test]
    fn gemini_empty_messages() {
        let (system, mapped) = map_messages_gemini_parts(&[]);
        assert!(system.is_none());
        assert!(mapped.is_empty());
    }

    #[test]
    fn gemini_full_conversation() {
        let messages = vec![
            msg(Role::System, "sys"),
            msg(Role::User, "q1"),
            msg(Role::Assistant, "a1"),
            msg(Role::User, "q2"),
        ];
        let (system, mapped) = map_messages_gemini_parts(&messages);
        assert!(system.is_some());
        assert_eq!(mapped.len(), 3);
        assert_eq!(mapped[0]["role"], "user");
        assert_eq!(mapped[1]["role"], "model");
        assert_eq!(mapped[2]["role"], "user");
    }

    // ── Image content tests ──

    use ava_types::{ImageContent, ImageMediaType};

    fn user_msg_with_image(content: &str, data: &str, media_type: ImageMediaType) -> Message {
        Message::new(Role::User, content).with_images(vec![ImageContent::new(data, media_type)])
    }

    // ── Anthropic image tests ──

    #[test]
    fn anthropic_user_message_with_image() {
        let messages = vec![user_msg_with_image(
            "describe this",
            "abc123",
            ImageMediaType::Png,
        )];
        let (_, mapped) = map_messages_anthropic(&messages);
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content.len(), 2);
        assert_eq!(content[0]["type"], "text");
        assert_eq!(content[0]["text"], "describe this");
        assert_eq!(content[1]["type"], "image");
        assert_eq!(content[1]["source"]["type"], "base64");
        assert_eq!(content[1]["source"]["media_type"], "image/png");
        assert_eq!(content[1]["source"]["data"], "abc123");
    }

    #[test]
    fn anthropic_user_message_image_only() {
        let messages = vec![Message::new(Role::User, "")
            .with_images(vec![ImageContent::new("imgdata", ImageMediaType::Jpeg)])];
        let (_, mapped) = map_messages_anthropic(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        // No text block when content is empty
        assert_eq!(content.len(), 1);
        assert_eq!(content[0]["type"], "image");
        assert_eq!(content[0]["source"]["media_type"], "image/jpeg");
    }

    #[test]
    fn anthropic_user_message_multiple_images() {
        let messages = vec![Message::new(Role::User, "compare these").with_images(vec![
            ImageContent::new("img1", ImageMediaType::Png),
            ImageContent::new("img2", ImageMediaType::WebP),
        ])];
        let (_, mapped) = map_messages_anthropic(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content.len(), 3); // text + 2 images
        assert_eq!(content[1]["source"]["media_type"], "image/png");
        assert_eq!(content[2]["source"]["media_type"], "image/webp");
    }

    // ── OpenAI image tests ──

    #[test]
    fn openai_user_message_with_image() {
        let messages = vec![user_msg_with_image(
            "what is this?",
            "abc123",
            ImageMediaType::Png,
        )];
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content.len(), 2);
        assert_eq!(content[0]["type"], "text");
        assert_eq!(content[0]["text"], "what is this?");
        assert_eq!(content[1]["type"], "image_url");
        assert_eq!(
            content[1]["image_url"]["url"],
            "data:image/png;base64,abc123"
        );
    }

    #[test]
    fn openai_user_message_image_jpeg() {
        let messages = vec![user_msg_with_image(
            "photo",
            "jpegdata",
            ImageMediaType::Jpeg,
        )];
        let mapped = map_messages_openai(&messages);
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(
            content[1]["image_url"]["url"],
            "data:image/jpeg;base64,jpegdata"
        );
    }

    // ── Gemini image tests ──

    #[test]
    fn gemini_user_message_with_image() {
        let messages = vec![user_msg_with_image(
            "analyze this",
            "abc123",
            ImageMediaType::Gif,
        )];
        let (_, mapped) = map_messages_gemini_parts(&messages);
        assert_eq!(mapped.len(), 1);
        assert_eq!(mapped[0]["role"], "user");
        let parts = mapped[0]["parts"].as_array().unwrap();
        assert_eq!(parts.len(), 2);
        assert_eq!(parts[0]["text"], "analyze this");
        assert_eq!(parts[1]["inlineData"]["mimeType"], "image/gif");
        assert_eq!(parts[1]["inlineData"]["data"], "abc123");
    }

    #[test]
    fn gemini_user_message_image_only() {
        let messages = vec![Message::new(Role::User, "")
            .with_images(vec![ImageContent::new("webpdata", ImageMediaType::WebP)])];
        let (_, mapped) = map_messages_gemini_parts(&messages);
        let parts = mapped[0]["parts"].as_array().unwrap();
        assert_eq!(parts.len(), 1); // no text part
        assert_eq!(parts[0]["inlineData"]["mimeType"], "image/webp");
    }

    // ── No images → standard format ──

    #[test]
    fn no_images_preserves_standard_format() {
        let messages = vec![msg(Role::User, "plain text")];

        // OpenAI
        let mapped = map_messages_openai(&messages);
        assert_eq!(mapped[0]["content"], "plain text");
        assert!(mapped[0]["content"].is_string()); // not an array

        // Anthropic
        let (_, mapped) = map_messages_anthropic(&messages);
        assert_eq!(mapped[0]["content"], "plain text");
        assert!(mapped[0]["content"].is_string());

        // Gemini
        let (_, mapped) = map_messages_gemini_parts(&messages);
        assert_eq!(mapped[0]["parts"][0]["text"], "plain text");
    }
}
