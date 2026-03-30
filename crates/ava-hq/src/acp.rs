use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum AcpMethod {
    CreateSpec,
    ListSpecs,
    ListArtifacts,
    SendPeerMessage,
    ReadMailbox,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AcpRequest {
    pub method: AcpMethod,
    pub payload_json: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AcpResponse {
    pub ok: bool,
    pub payload_json: String,
    pub error: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AcpError {
    pub message: String,
}

impl AcpError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl AcpResponse {
    pub fn ok(payload_json: impl Into<String>) -> Self {
        Self {
            ok: true,
            payload_json: payload_json.into(),
            error: None,
        }
    }

    pub fn err(message: impl Into<String>) -> Self {
        let message = message.into();
        Self {
            ok: false,
            payload_json: String::new(),
            error: Some(message),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acp_response_helpers_shape_expected_result() {
        let ok = AcpResponse::ok("{}");
        assert!(ok.ok);
        assert!(ok.error.is_none());

        let err = AcpResponse::err("bad payload");
        assert!(!err.ok);
        assert_eq!(err.error.as_deref(), Some("bad payload"));
    }
}
