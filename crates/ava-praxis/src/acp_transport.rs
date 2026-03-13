use crate::acp::{AcpRequest, AcpResponse};
use crate::acp_handler::AcpHandler;
use crate::events::PraxisEvent;

#[derive(Debug, Default)]
pub struct InProcessAcpTransport {
    handler: AcpHandler,
    events: Vec<PraxisEvent>,
}

impl InProcessAcpTransport {
    pub fn new(handler: AcpHandler) -> Self {
        Self {
            handler,
            events: Vec::new(),
        }
    }

    pub fn request(&mut self, request: AcpRequest) -> AcpResponse {
        let (response, event) = self.handler.handle(request);
        if let Some(event) = event {
            self.events.push(event);
        }
        response
    }

    pub fn take_events(&mut self) -> Vec<PraxisEvent> {
        std::mem::take(&mut self.events)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::acp::AcpMethod;

    #[test]
    fn acp_transport_records_emitted_events() {
        let handler = AcpHandler::default();
        let mut transport = InProcessAcpTransport::new(handler);

        let response = transport.request(AcpRequest {
            method: AcpMethod::CreateSpec,
            payload_json: serde_json::json!({
                "title": "T",
                "requirements": "R",
                "design": "D",
                "tasks": ["x"]
            })
            .to_string(),
        });
        assert!(response.ok);

        let events = transport.take_events();
        assert!(!events.is_empty());
    }
}
