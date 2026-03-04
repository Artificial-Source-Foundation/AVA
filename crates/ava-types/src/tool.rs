//! Tool-related types

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Tool {
    pub name: String,
    pub description: String,
    pub parameters: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ToolCall {
    pub id: String,
    pub name: String,
    pub arguments: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ToolResult {
    pub call_id: String,
    pub content: String,
    pub is_error: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tool_serialization() {
        let tool = Tool {
            name: "read_file".to_string(),
            description: "Read a file".to_string(),
            parameters: serde_json::json!({}),
        };
        let json = serde_json::to_string(&tool).unwrap();
        assert!(json.contains("read_file"));
        assert!(json.contains("Read a file"));
    }

    #[test]
    fn test_tool_roundtrip() {
        let tool = Tool {
            name: "write_file".to_string(),
            description: "Write to a file".to_string(),
            parameters: serde_json::json!({
                "path": {"type": "string"},
                "content": {"type": "string"}
            }),
        };
        let json = serde_json::to_string(&tool).unwrap();
        let deserialized: Tool = serde_json::from_str(&json).unwrap();
        assert_eq!(tool, deserialized);
    }

    #[test]
    fn test_tool_result_creation() {
        let result = ToolResult {
            call_id: "call_123".to_string(),
            content: "File contents".to_string(),
            is_error: false,
        };
        assert_eq!(result.call_id, "call_123");
        assert_eq!(result.content, "File contents");
        assert!(!result.is_error);
    }
}
