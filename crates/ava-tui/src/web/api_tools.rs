//! HTTP API handlers for tool visibility/introspection.

use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use super::api::{error_response, ErrorResponse};
use super::state::WebState;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentToolInfo {
    pub name: String,
    pub description: String,
    pub source: String,
}

#[derive(Debug, Deserialize, Default)]
pub struct ListAgentToolsRequest {
    #[serde(default)]
    pub context: Option<ToolIntrospectionContext>,
}

#[derive(Debug, Deserialize, Default)]
pub struct ToolIntrospectionContext {
    #[serde(default, alias = "sessionId")]
    pub session_id: Option<String>,
    #[serde(default)]
    pub goal: Option<String>,
    #[serde(default)]
    pub history: Vec<ToolIntrospectionMessageContext>,
    #[serde(default)]
    pub images: Vec<ToolIntrospectionImageContext>,
}

#[derive(Debug, Deserialize)]
pub struct ToolIntrospectionMessageContext {
    pub role: String,
    pub content: String,
    #[serde(default, alias = "agentVisible")]
    pub agent_visible: Option<bool>,
}

#[derive(Debug, Deserialize)]
pub struct ToolIntrospectionImageContext {
    pub data: String,
    #[serde(alias = "mediaType")]
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

fn collect_history_before_last_user(messages: &[ava_types::Message]) -> Vec<ava_types::Message> {
    if let Some(pos) = messages
        .iter()
        .rposition(|message| message.role == ava_types::Role::User)
    {
        messages[..pos].to_vec()
    } else {
        vec![]
    }
}

async fn derive_context_from_session(
    state: &WebState,
    session_id: &str,
) -> Result<
    Option<(
        String,
        Vec<ava_types::Message>,
        Vec<ava_types::ImageContent>,
    )>,
    (StatusCode, Json<ErrorResponse>),
> {
    let uuid = Uuid::parse_str(session_id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("invalid session ID: {e}"))
    })?;
    let session = state
        .inner
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(session.map(|session| {
        let goal = session
            .messages
            .iter()
            .rev()
            .find(|message| message.role == ava_types::Role::User)
            .map(|message| message.content.clone())
            .unwrap_or_default();
        let images = session
            .messages
            .iter()
            .rev()
            .find(|message| message.role == ava_types::Role::User)
            .map(|message| message.images.clone())
            .unwrap_or_default();
        let history = collect_history_before_last_user(&session.messages);
        (goal, history, images)
    }))
}

/// List the effective prompt-visible tool surface for an interactive run.
pub(crate) async fn list_agent_tools(
    State(state): State<WebState>,
    req: Option<Json<ListAgentToolsRequest>>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let req = req.map(|Json(req)| req).unwrap_or_default();
    let (goal, history, images) = match req.context {
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
                    derive_context_from_session(&state, session_id).await?
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

    let tools = state
        .inner
        .stack
        .effective_tools_for_interactive_run(&goal, &history, &images)
        .await;

    Ok(Json(
        tools
            .into_iter()
            .map(|(def, source)| AgentToolInfo {
                name: def.name,
                description: def.description,
                source: source.to_string(),
            })
            .collect::<Vec<_>>(),
    ))
}

#[cfg(test)]
mod tests {
    use super::{
        map_context_history, map_context_images, ListAgentToolsRequest,
        ToolIntrospectionImageContext, ToolIntrospectionMessageContext,
    };
    use ava_types::{ImageMediaType, Role};
    use serde_json::json;

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
    fn deserializes_snake_case_tool_introspection_payloads() {
        let payload = json!({
            "context": {
                "session_id": "sess-1",
                "history": [
                    {
                        "role": "user",
                        "content": "show tools",
                        "agent_visible": false
                    }
                ],
                "images": [
                    {
                        "data": "abc",
                        "media_type": "image/png"
                    }
                ]
            }
        });

        let request: ListAgentToolsRequest = serde_json::from_value(payload).expect("request");
        let context = request.context.expect("context");

        assert_eq!(context.session_id.as_deref(), Some("sess-1"));
        assert_eq!(context.history.len(), 1);
        assert_eq!(context.images.len(), 1);
        assert_eq!(context.history[0].agent_visible, Some(false));
    }
}
