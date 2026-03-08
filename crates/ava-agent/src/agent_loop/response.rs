use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::time::Instant;

use ava_types::{AvaError, Result, TokenUsage, ToolCall};
use serde::Deserialize;
use serde_json::Value;
use tracing::warn;
use uuid::Uuid;

use super::AgentLoop;

#[derive(Debug, Deserialize)]
pub(super) struct ToolCallEnvelope {
    name: String,
    #[serde(default)]
    arguments: Value,
    #[serde(default)]
    id: Option<String>,
}

pub(super) fn parse_tool_calls(content: &str) -> Result<Vec<ToolCall>> {
    let value = match serde_json::from_str::<Value>(content) {
        Ok(value) => value,
        Err(_) => return Ok(Vec::new()),
    };

    let calls = if let Some(raw_calls) = value.get("tool_calls") {
        serde_json::from_value::<Vec<ToolCallEnvelope>>(raw_calls.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?
    } else if let Some(raw_call) = value.get("tool_call") {
        vec![serde_json::from_value::<ToolCallEnvelope>(raw_call.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?]
    } else {
        Vec::new()
    };

    Ok(calls
        .into_iter()
        .map(|call| ToolCall {
            id: call.id.unwrap_or_else(|| Uuid::new_v4().to_string()),
            name: call.name,
            arguments: call.arguments,
        })
        .collect())
}

impl AgentLoop {
    /// Generate a response, using native tool calling when the provider supports it.
    /// Returns (response_text, tool_calls, usage).
    pub(super) async fn generate_response(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        // Dedup guard: skip if identical request within 2s
        let messages = self.context.get_messages();
        let hash = {
            let mut hasher = DefaultHasher::new();
            if let Some(last) = messages.last() {
                last.content.hash(&mut hasher);
            }
            hasher.finish()
        };
        if let (Some(prev_hash), Some(prev_time)) =
            (self.last_request_hash, self.last_request_time)
        {
            if hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                warn!("Skipping duplicate request (same content within 2s)");
                return Ok((String::new(), vec![], None));
            }
        }

        let result = if self.llm.supports_tools() {
            let tool_defs = self.tools.list_tools();
            let response = self
                .llm
                .generate_with_tools(messages, &tool_defs)
                .await?;
            Ok((response.content, response.tool_calls, response.usage))
        } else {
            let response = self.llm.generate(messages).await?;
            let tool_calls = parse_tool_calls(&response)?;
            Ok((response, tool_calls, None))
        };

        self.last_request_hash = Some(hash);
        self.last_request_time = Some(Instant::now());

        result
    }
}
