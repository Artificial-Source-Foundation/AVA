//! Message and role types

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::tool::{ToolCall, ToolResult};

/// Supported image media types for multimodal content.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum ImageMediaType {
    #[serde(rename = "image/png")]
    Png,
    #[serde(rename = "image/jpeg")]
    Jpeg,
    #[serde(rename = "image/gif")]
    Gif,
    #[serde(rename = "image/webp")]
    WebP,
}

impl ImageMediaType {
    /// Detect media type from file extension (case-insensitive).
    pub fn from_extension(ext: &str) -> Option<Self> {
        match ext.to_lowercase().as_str() {
            "png" => Some(Self::Png),
            "jpg" | "jpeg" => Some(Self::Jpeg),
            "gif" => Some(Self::Gif),
            "webp" => Some(Self::WebP),
            _ => None,
        }
    }

    /// Return the MIME type string (e.g. "image/png").
    pub fn as_mime(&self) -> &'static str {
        match self {
            Self::Png => "image/png",
            Self::Jpeg => "image/jpeg",
            Self::Gif => "image/gif",
            Self::WebP => "image/webp",
        }
    }

    /// Check if a file extension is a supported image format.
    pub fn is_supported_extension(ext: &str) -> bool {
        Self::from_extension(ext).is_some()
    }
}

impl std::fmt::Display for ImageMediaType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_mime())
    }
}

/// An image attachment for multimodal messages.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ImageContent {
    /// Base64-encoded image data.
    pub data: String,
    /// Media type of the image.
    pub media_type: ImageMediaType,
}

impl ImageContent {
    /// Create a new ImageContent from base64 data and media type.
    pub fn new(data: impl Into<String>, media_type: ImageMediaType) -> Self {
        Self {
            data: data.into(),
            media_type,
        }
    }

    /// Read an image file, detect its format, and return an ImageContent.
    /// Returns an error if the file cannot be read or has an unsupported format.
    pub fn from_file(path: &std::path::Path) -> std::result::Result<Self, String> {
        use base64::Engine;

        let ext = path
            .extension()
            .and_then(|e| e.to_str())
            .ok_or_else(|| format!("No file extension: {}", path.display()))?;

        let media_type = ImageMediaType::from_extension(ext).ok_or_else(|| {
            format!(
                "Unsupported image format '.{ext}'. Supported: png, jpg, jpeg, gif, webp"
            )
        })?;

        let bytes = std::fs::read(path)
            .map_err(|e| format!("Failed to read image file '{}': {e}", path.display()))?;

        let data = base64::engine::general_purpose::STANDARD.encode(&bytes);

        Ok(Self { data, media_type })
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Message {
    pub id: Uuid,
    pub role: Role,
    pub content: String,
    pub timestamp: DateTime<Utc>,
    pub tool_calls: Vec<ToolCall>,
    pub tool_results: Vec<ToolResult>,
    /// For Role::Tool messages, links back to the tool_call that produced this result.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_call_id: Option<String>,
    /// Image attachments for multimodal messages.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub images: Vec<ImageContent>,
}

impl Message {
    pub fn new(role: Role, content: impl Into<String>) -> Self {
        Self {
            id: Uuid::new_v4(),
            role,
            content: content.into(),
            timestamp: Utc::now(),
            tool_calls: Vec::new(),
            tool_results: Vec::new(),
            tool_call_id: None,
            images: Vec::new(),
        }
    }

    pub fn with_tool_calls(mut self, tool_calls: Vec<ToolCall>) -> Self {
        self.tool_calls = tool_calls;
        self
    }

    pub fn with_tool_results(mut self, tool_results: Vec<ToolResult>) -> Self {
        self.tool_results = tool_results;
        self
    }

    pub fn with_tool_call_id(mut self, id: impl Into<String>) -> Self {
        self.tool_call_id = Some(id.into());
        self
    }

    pub fn with_images(mut self, images: Vec<ImageContent>) -> Self {
        self.images = images;
        self
    }

    /// Whether this message has any image attachments.
    pub fn has_images(&self) -> bool {
        !self.images.is_empty()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum Role {
    System,
    User,
    Assistant,
    Tool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_message_creation() {
        let message = Message::new(Role::User, "Test message");
        assert_eq!(message.role, Role::User);
        assert_eq!(message.content, "Test message");
        assert!(message.tool_calls.is_empty());
        assert!(message.tool_results.is_empty());
    }

    #[test]
    fn test_message_with_tool_calls() {
        let tool_call = ToolCall {
            id: "call_1".to_string(),
            name: "read_file".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };
        let message = Message::new(Role::Assistant, "I'll read that file")
            .with_tool_calls(vec![tool_call.clone()]);
        assert_eq!(message.tool_calls.len(), 1);
        assert_eq!(message.tool_calls[0], tool_call);
    }

    #[test]
    fn test_role_serialization() {
        let roles = vec![Role::System, Role::User, Role::Assistant, Role::Tool];
        for role in roles {
            let json = serde_json::to_string(&role).unwrap();
            let deserialized: Role = serde_json::from_str(&json).unwrap();
            assert_eq!(role, deserialized);
        }
    }

    // ── Image types tests ──

    #[test]
    fn test_image_media_type_from_extension() {
        assert_eq!(ImageMediaType::from_extension("png"), Some(ImageMediaType::Png));
        assert_eq!(ImageMediaType::from_extension("PNG"), Some(ImageMediaType::Png));
        assert_eq!(ImageMediaType::from_extension("jpg"), Some(ImageMediaType::Jpeg));
        assert_eq!(ImageMediaType::from_extension("jpeg"), Some(ImageMediaType::Jpeg));
        assert_eq!(ImageMediaType::from_extension("JPEG"), Some(ImageMediaType::Jpeg));
        assert_eq!(ImageMediaType::from_extension("gif"), Some(ImageMediaType::Gif));
        assert_eq!(ImageMediaType::from_extension("webp"), Some(ImageMediaType::WebP));
        assert_eq!(ImageMediaType::from_extension("bmp"), None);
        assert_eq!(ImageMediaType::from_extension("svg"), None);
        assert_eq!(ImageMediaType::from_extension(""), None);
    }

    #[test]
    fn test_image_media_type_as_mime() {
        assert_eq!(ImageMediaType::Png.as_mime(), "image/png");
        assert_eq!(ImageMediaType::Jpeg.as_mime(), "image/jpeg");
        assert_eq!(ImageMediaType::Gif.as_mime(), "image/gif");
        assert_eq!(ImageMediaType::WebP.as_mime(), "image/webp");
    }

    #[test]
    fn test_image_media_type_is_supported_extension() {
        assert!(ImageMediaType::is_supported_extension("png"));
        assert!(ImageMediaType::is_supported_extension("jpg"));
        assert!(ImageMediaType::is_supported_extension("jpeg"));
        assert!(ImageMediaType::is_supported_extension("gif"));
        assert!(ImageMediaType::is_supported_extension("webp"));
        assert!(!ImageMediaType::is_supported_extension("bmp"));
        assert!(!ImageMediaType::is_supported_extension("tiff"));
    }

    #[test]
    fn test_image_media_type_display() {
        assert_eq!(ImageMediaType::Png.to_string(), "image/png");
        assert_eq!(ImageMediaType::Jpeg.to_string(), "image/jpeg");
    }

    #[test]
    fn test_image_content_new() {
        let img = ImageContent::new("base64data", ImageMediaType::Png);
        assert_eq!(img.data, "base64data");
        assert_eq!(img.media_type, ImageMediaType::Png);
    }

    #[test]
    fn test_image_content_from_file_supported() {
        // Create a temp file with a .png extension
        let dir = std::env::temp_dir();
        let path = dir.join("test_image_input.png");
        std::fs::write(&path, b"fake png data").unwrap();

        let result = ImageContent::from_file(&path);
        assert!(result.is_ok());
        let img = result.unwrap();
        assert_eq!(img.media_type, ImageMediaType::Png);
        // Verify it's valid base64
        use base64::Engine;
        let decoded = base64::engine::general_purpose::STANDARD.decode(&img.data).unwrap();
        assert_eq!(decoded, b"fake png data");

        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn test_image_content_from_file_unsupported_format() {
        let dir = std::env::temp_dir();
        let path = dir.join("test_image_input.bmp");
        std::fs::write(&path, b"fake bmp data").unwrap();

        let result = ImageContent::from_file(&path);
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("Unsupported image format"));

        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn test_image_content_from_file_missing() {
        let path = std::path::Path::new("/nonexistent/path/image.png");
        let result = ImageContent::from_file(path);
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("Failed to read"));
    }

    #[test]
    fn test_image_content_from_file_no_extension() {
        let dir = std::env::temp_dir();
        let path = dir.join("no_extension_file");
        std::fs::write(&path, b"data").unwrap();

        let result = ImageContent::from_file(&path);
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("No file extension"));

        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn test_image_content_serialization() {
        let img = ImageContent::new("abc123", ImageMediaType::Jpeg);
        let json = serde_json::to_string(&img).unwrap();
        let deserialized: ImageContent = serde_json::from_str(&json).unwrap();
        assert_eq!(img, deserialized);
    }

    #[test]
    fn test_message_with_images() {
        let msg = Message::new(Role::User, "describe this image")
            .with_images(vec![
                ImageContent::new("img1data", ImageMediaType::Png),
                ImageContent::new("img2data", ImageMediaType::Jpeg),
            ]);
        assert!(msg.has_images());
        assert_eq!(msg.images.len(), 2);
    }

    #[test]
    fn test_message_without_images() {
        let msg = Message::new(Role::User, "plain text");
        assert!(!msg.has_images());
        assert!(msg.images.is_empty());
    }

    #[test]
    fn test_message_images_not_serialized_when_empty() {
        let msg = Message::new(Role::User, "plain text");
        let json = serde_json::to_string(&msg).unwrap();
        assert!(!json.contains("images"));
    }

    #[test]
    fn test_message_images_serialized_when_present() {
        let msg = Message::new(Role::User, "with image")
            .with_images(vec![ImageContent::new("data", ImageMediaType::Gif)]);
        let json = serde_json::to_string(&msg).unwrap();
        assert!(json.contains("images"));
        assert!(json.contains("image/gif"));
    }
}
