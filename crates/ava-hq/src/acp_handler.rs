use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::acp::{AcpMethod, AcpRequest, AcpResponse};
use crate::artifact_store::ArtifactStore;
use crate::events::HqEvent;
use crate::mailbox::{Mailbox, PeerMessage, PeerMessageKind};
use crate::spec::{SpecDocument, SpecStore};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CreateSpecRequest {
    pub title: String,
    pub requirements: String,
    pub design: String,
    pub tasks: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SendPeerMessageRequest {
    pub from_worker: Uuid,
    pub to_worker: Uuid,
    pub kind: String,
    pub body: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReadMailboxRequest {
    pub worker_id: Uuid,
}

#[derive(Debug, Default)]
pub struct AcpHandler {
    pub specs: SpecStore,
    pub artifacts: ArtifactStore,
    pub mailbox: Mailbox,
}

impl AcpHandler {
    pub fn handle(&mut self, request: AcpRequest) -> (AcpResponse, Option<HqEvent>) {
        let method_name = format!("{:?}", request.method);
        let (response, event) = match request.method {
            AcpMethod::CreateSpec => self.handle_create_spec(&request.payload_json),
            AcpMethod::ListSpecs => self.handle_list_specs(),
            AcpMethod::ListArtifacts => self.handle_list_artifacts(),
            AcpMethod::SendPeerMessage => self.handle_send_peer_message(&request.payload_json),
            AcpMethod::ReadMailbox => self.handle_read_mailbox(&request.payload_json),
        };
        let success = response.ok;

        if event.is_none() {
            (
                response,
                Some(HqEvent::AcpRequestHandled {
                    method: method_name,
                    success,
                }),
            )
        } else {
            (response, event)
        }
    }

    fn handle_create_spec(&mut self, payload_json: &str) -> (AcpResponse, Option<HqEvent>) {
        let Ok(payload) = serde_json::from_str::<CreateSpecRequest>(payload_json) else {
            return (AcpResponse::err("invalid CreateSpec payload"), None);
        };

        let spec = SpecDocument::new(
            payload.title,
            payload.requirements,
            payload.design,
            payload.tasks,
        );
        let spec_id = self.specs.create(spec.clone());

        let response = AcpResponse::ok(
            serde_json::json!({
                "spec_id": spec_id,
                "title": spec.title,
                "status": "Draft"
            })
            .to_string(),
        );

        (
            response,
            Some(HqEvent::SpecCreated {
                spec_id,
                title: spec.title,
            }),
        )
    }

    fn handle_list_specs(&self) -> (AcpResponse, Option<HqEvent>) {
        let specs = self
            .specs
            .list()
            .iter()
            .map(|spec| {
                serde_json::json!({
                    "id": spec.id,
                    "title": spec.title,
                    "status": format!("{:?}", spec.status),
                    "tasks": spec.tasks.len()
                })
            })
            .collect::<Vec<_>>();

        (
            AcpResponse::ok(serde_json::json!({ "specs": specs }).to_string()),
            None,
        )
    }

    fn handle_list_artifacts(&self) -> (AcpResponse, Option<HqEvent>) {
        let artifacts = self
            .artifacts
            .list()
            .iter()
            .map(|artifact| {
                serde_json::json!({
                    "id": artifact.id,
                    "kind": format!("{:?}", artifact.kind),
                    "title": artifact.title,
                    "producer": artifact.producer,
                    "spec_id": artifact.spec_id,
                })
            })
            .collect::<Vec<_>>();

        (
            AcpResponse::ok(serde_json::json!({ "artifacts": artifacts }).to_string()),
            None,
        )
    }

    fn handle_send_peer_message(&mut self, payload_json: &str) -> (AcpResponse, Option<HqEvent>) {
        let Ok(payload) = serde_json::from_str::<SendPeerMessageRequest>(payload_json) else {
            return (AcpResponse::err("invalid SendPeerMessage payload"), None);
        };

        let kind = match payload.kind.as_str() {
            "task_update" => PeerMessageKind::TaskUpdate,
            "dependency_notice" => PeerMessageKind::DependencyNotice,
            "blocker" => PeerMessageKind::Blocker,
            "review_request" => PeerMessageKind::ReviewRequest,
            other => PeerMessageKind::Custom(other.to_string()),
        };

        let message = PeerMessage::new(payload.from_worker, payload.to_worker, kind, payload.body);
        let message_id = message.id;
        let kind_text = format!("{:?}", message.kind);

        self.mailbox.send(message);

        (
            AcpResponse::ok(serde_json::json!({ "message_id": message_id }).to_string()),
            Some(HqEvent::PeerMessageSent {
                message_id,
                from_worker: payload.from_worker,
                to_worker: payload.to_worker,
                kind: kind_text,
            }),
        )
    }

    fn handle_read_mailbox(&mut self, payload_json: &str) -> (AcpResponse, Option<HqEvent>) {
        let Ok(payload) = serde_json::from_str::<ReadMailboxRequest>(payload_json) else {
            return (AcpResponse::err("invalid ReadMailbox payload"), None);
        };

        let mut messages = Vec::new();
        while let Some(msg) = self.mailbox.receive(payload.worker_id) {
            messages.push(serde_json::json!({
                "id": msg.id,
                "from_worker": msg.from_worker,
                "kind": format!("{:?}", msg.kind),
                "body": msg.body,
            }));
        }

        (
            AcpResponse::ok(serde_json::json!({ "messages": messages }).to_string()),
            None,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::acp::AcpMethod;

    #[test]
    fn acp_handler_creates_and_lists_specs() {
        let mut handler = AcpHandler::default();
        let create_req = AcpRequest {
            method: AcpMethod::CreateSpec,
            payload_json: serde_json::json!({
                "title": "Auth spec",
                "requirements": "Secure sessions",
                "design": "Token validator module",
                "tasks": ["Implement validator"]
            })
            .to_string(),
        };

        let (create_res, _) = handler.handle(create_req);
        assert!(create_res.ok);

        let (list_res, _) = handler.handle(AcpRequest {
            method: AcpMethod::ListSpecs,
            payload_json: "{}".to_string(),
        });
        assert!(list_res.ok);
        assert!(list_res.payload_json.contains("Auth spec"));
    }

    #[test]
    fn acp_handler_roundtrips_mailbox_messages() {
        let mut handler = AcpHandler::default();
        let a = Uuid::new_v4();
        let b = Uuid::new_v4();

        let (send_res, event) = handler.handle(AcpRequest {
            method: AcpMethod::SendPeerMessage,
            payload_json: serde_json::json!({
                "from_worker": a,
                "to_worker": b,
                "kind": "blocker",
                "body": "Need API contract details"
            })
            .to_string(),
        });
        assert!(send_res.ok);
        assert!(matches!(event, Some(HqEvent::PeerMessageSent { .. })));

        let (read_res, _) = handler.handle(AcpRequest {
            method: AcpMethod::ReadMailbox,
            payload_json: serde_json::json!({ "worker_id": b }).to_string(),
        });
        assert!(read_res.ok);
        assert!(read_res.payload_json.contains("Need API contract details"));
    }
}
