//! Tauri commands for querying registered tools.

use ava_control_plane::sessions::{load_prompt_context, SessionPromptContext};
use serde::Deserialize;
use serde::Serialize;
use tauri::State;
use tokio::task;
use uuid::Uuid;

use crate::bridge::DesktopBridge;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentToolInfo {
    pub name: String,
    pub description: String,
    pub source: String,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolIntrospectionContext {
    #[serde(default)]
    pub session_id: Option<String>,
    #[serde(default)]
    pub goal: Option<String>,
    #[serde(default)]
    pub history: Vec<ToolIntrospectionMessageContext>,
    #[serde(default)]
    pub images: Vec<ToolIntrospectionImageContext>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolIntrospectionMessageContext {
    pub role: String,
    pub content: String,
    #[serde(default)]
    pub agent_visible: Option<bool>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolIntrospectionImageContext {
    pub data: String,
    pub media_type: String,
}

fn map_context_role(role: &str) -> Option<ava_types::Role> {
    match role {
        "user" => Some(ava_types::Role::User),
        "assistant" => Some(ava_types::Role::Assistant),
        "system" => Some(ava_types::Role::System),
        _ => None,
    }
}

fn map_context_history(history: Vec<ToolIntrospectionMessageContext>) -> Vec<ava_types::Message> {
    history
        .into_iter()
        .filter_map(|msg| {
            let role = map_context_role(&msg.role)?;
            let mut mapped = ava_types::Message::new(role, msg.content);
            if let Some(agent_visible) = msg.agent_visible {
                mapped.agent_visible = agent_visible;
            }
            Some(mapped)
        })
        .collect()
}

fn map_image_media_type(media_type: &str) -> Option<ava_types::ImageMediaType> {
    match media_type {
        "image/png" => Some(ava_types::ImageMediaType::Png),
        "image/jpeg" => Some(ava_types::ImageMediaType::Jpeg),
        "image/gif" => Some(ava_types::ImageMediaType::Gif),
        "image/webp" => Some(ava_types::ImageMediaType::WebP),
        _ => None,
    }
}

fn map_context_images(images: Vec<ToolIntrospectionImageContext>) -> Vec<ava_types::ImageContent> {
    images
        .into_iter()
        .filter_map(|image| {
            Some(ava_types::ImageContent::new(
                image.data,
                map_image_media_type(&image.media_type)?,
            ))
        })
        .collect()
}

async fn derive_context_from_session(
    bridge: &DesktopBridge,
    session_id: &str,
) -> Result<
    Option<(
        String,
        Vec<ava_types::Message>,
        Vec<ava_types::ImageContent>,
    )>,
    String,
> {
    let uuid = Uuid::parse_str(session_id).map_err(|e| format!("invalid session ID: {e}"))?;
    let session_manager = bridge.stack.session_manager.clone();
    let session = task::spawn_blocking(move || session_manager.get(uuid))
        .await
        .map_err(|e| e.to_string())?
        .map_err(|e| e.to_string())?;

    Ok(session.map(|session| {
        let SessionPromptContext {
            goal,
            history,
            images,
        } = load_prompt_context(&session);
        (goal, history, images)
    }))
}

/// List all tools currently registered in the agent's tool registry.
#[tauri::command]
pub async fn list_agent_tools(
    bridge: State<'_, DesktopBridge>,
    context: Option<ToolIntrospectionContext>,
) -> Result<Vec<AgentToolInfo>, String> {
    let (goal, history, images) = match context {
        Some(context) => {
            let ToolIntrospectionContext {
                session_id,
                goal,
                history,
                images,
            } = context;
            let has_explicit_context = goal.is_some() || !history.is_empty() || !images.is_empty();
            let explicit_goal = goal.unwrap_or_default();
            let explicit_history = map_context_history(history);
            let explicit_images = map_context_images(images);

            if has_explicit_context {
                (explicit_goal, explicit_history, explicit_images)
            } else if let Some(session_id) = session_id.as_deref() {
                if let Some((goal, history, images)) =
                    derive_context_from_session(&bridge, session_id).await?
                {
                    (goal, history, images)
                } else {
                    (explicit_goal, explicit_history, explicit_images)
                }
            } else {
                (explicit_goal, explicit_history, explicit_images)
            }
        }
        None => (String::new(), Vec::new(), Vec::new()),
    };

    let tools = bridge
        .stack
        .effective_tools_for_interactive_run(&goal, &history, &images)
        .await;
    Ok(tools
        .into_iter()
        .map(|(def, source)| AgentToolInfo {
            name: def.name,
            description: def.description,
            source: source.to_string(),
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::{
        load_prompt_context, map_context_history, map_context_images, SessionPromptContext,
        ToolIntrospectionImageContext, ToolIntrospectionMessageContext,
    };
    use ava_types::{ImageMediaType, Role};

    #[test]
    fn preserves_agent_visibility_when_mapping_history() {
        let history = map_context_history(vec![ToolIntrospectionMessageContext {
            role: "user".to_string(),
            content: "delegate this".to_string(),
            agent_visible: Some(false),
        }]);

        assert_eq!(history.len(), 1);
        assert_eq!(history[0].role, Role::User);
        assert!(!history[0].agent_visible);
    }

    #[test]
    fn maps_supported_images_and_skips_unknown_media_types() {
        let images = map_context_images(vec![
            ToolIntrospectionImageContext {
                data: "abc".to_string(),
                media_type: "image/png".to_string(),
            },
            ToolIntrospectionImageContext {
                data: "ignored".to_string(),
                media_type: "image/unknown".to_string(),
            },
        ]);

        assert_eq!(images.len(), 1);
        assert_eq!(images[0].data, "abc");
        assert_eq!(images[0].media_type, ImageMediaType::Png);
    }

    #[test]
    fn session_prompt_context_uses_latest_user_turn() {
        let mut session = ava_types::Session::new();
        session.add_message(ava_types::Message::new(ava_types::Role::System, "system"));
        session.add_message(ava_types::Message::new(ava_types::Role::User, "first"));
        session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "reply"));
        let final_user =
            ava_types::Message::new(ava_types::Role::User, "latest").with_images(vec![
                ava_types::ImageContent::new("img", ava_types::ImageMediaType::Png),
            ]);
        session.add_message(final_user.clone());

        assert_eq!(
            load_prompt_context(&session),
            SessionPromptContext {
                goal: "latest".to_string(),
                history: session.messages[..3].to_vec(),
                images: final_user.images,
            }
        );
    }
}
