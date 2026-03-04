use std::sync::atomic::{AtomicI64, Ordering};

use lsp_types::{Diagnostic, GotoDefinitionParams, GotoDefinitionResponse, InitializeParams};
use tokio::io::{AsyncRead, AsyncWrite};
use tokio::sync::broadcast;

use crate::error::{LspError, Result};
use crate::transport::{read_frame, write_frame};

pub struct LspClient {
    request_id: AtomicI64,
    diagnostics_tx: broadcast::Sender<Vec<Diagnostic>>,
}

impl Default for LspClient {
    fn default() -> Self {
        Self::new()
    }
}

impl LspClient {
    pub fn new() -> Self {
        let (diagnostics_tx, _) = broadcast::channel(64);
        Self {
            request_id: AtomicI64::new(1),
            diagnostics_tx,
        }
    }

    pub fn next_request_id(&self) -> i64 {
        self.request_id.fetch_add(1, Ordering::Relaxed)
    }

    pub fn subscribe_diagnostics(&self) -> broadcast::Receiver<Vec<Diagnostic>> {
        self.diagnostics_tx.subscribe()
    }

    pub fn publish_diagnostics(&self, diagnostics: Vec<Diagnostic>) {
        let _ = self.diagnostics_tx.send(diagnostics);
    }

    pub fn initialize_request(&self, params: InitializeParams) -> Result<String> {
        let payload = serde_json::json!({
            "jsonrpc": "2.0",
            "id": self.next_request_id(),
            "method": "initialize",
            "params": params,
        });
        Ok(payload.to_string())
    }

    pub fn goto_definition_request(&self, params: GotoDefinitionParams) -> Result<String> {
        let payload = serde_json::json!({
            "jsonrpc": "2.0",
            "id": self.next_request_id(),
            "method": "textDocument/definition",
            "params": params,
        });
        Ok(payload.to_string())
    }

    pub fn parse_goto_definition_response(
        &self,
        response_json: &str,
    ) -> Result<Option<GotoDefinitionResponse>> {
        #[derive(serde::Deserialize)]
        struct ResponseEnvelope {
            result: Option<GotoDefinitionResponse>,
            error: Option<ResponseError>,
        }

        #[derive(serde::Deserialize)]
        struct ResponseError {
            code: i64,
            message: String,
        }

        let envelope: ResponseEnvelope = serde_json::from_str(response_json)?;
        if let Some(error) = envelope.error {
            return Err(LspError::Protocol(format!(
                "json-rpc error {}: {}",
                error.code, error.message
            )));
        }
        Ok(envelope.result)
    }

    pub fn handle_notification(&self, notification_json: &str) -> Result<()> {
        #[derive(serde::Deserialize)]
        struct NotificationEnvelope {
            method: String,
            params: Option<serde_json::Value>,
        }

        let envelope: NotificationEnvelope = serde_json::from_str(notification_json)?;
        if envelope.method == "textDocument/publishDiagnostics" {
            if let Some(params) = envelope.params {
                let diagnostics = params
                    .get("diagnostics")
                    .cloned()
                    .unwrap_or_else(|| serde_json::Value::Array(vec![]));
                let parsed: Vec<Diagnostic> = serde_json::from_value(diagnostics)?;
                self.publish_diagnostics(parsed);
            }
        }
        Ok(())
    }

    pub async fn initialize_via_transport<R, W>(
        &self,
        reader: &mut R,
        writer: &mut W,
        params: InitializeParams,
    ) -> Result<String>
    where
        R: AsyncRead + Unpin,
        W: AsyncWrite + Unpin,
    {
        let payload = self.initialize_request(params)?;
        write_frame(writer, &payload).await?;
        read_frame(reader).await
    }

    pub async fn goto_definition_via_transport<R, W>(
        &self,
        reader: &mut R,
        writer: &mut W,
        params: GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>>
    where
        R: AsyncRead + Unpin,
        W: AsyncWrite + Unpin,
    {
        let payload = self.goto_definition_request(params)?;
        write_frame(writer, &payload).await?;
        let response = read_frame(reader).await?;
        self.parse_goto_definition_response(&response)
    }
}

#[cfg(test)]
mod tests {
    use lsp_types::InitializeParams;
    use tokio::io::duplex;

    use super::*;

    #[test]
    fn request_ids_increment() {
        let client = LspClient::new();
        assert_eq!(client.next_request_id(), 1);
        assert_eq!(client.next_request_id(), 2);
    }

    #[tokio::test]
    async fn diagnostics_stream_broadcasts() {
        let client = LspClient::new();
        let mut rx = client.subscribe_diagnostics();
        client.publish_diagnostics(vec![]);
        let got = rx.recv().await.unwrap();
        assert!(got.is_empty());
    }

    #[test]
    fn initialize_payload_is_json_rpc() {
        let client = LspClient::new();
        let msg = client
            .initialize_request(InitializeParams::default())
            .unwrap();
        assert!(msg.contains("\"method\":\"initialize\""));
    }

    #[test]
    fn parse_definition_response() {
        let client = LspClient::new();
        let json = serde_json::json!({
            "jsonrpc": "2.0",
            "id": 1,
            "result": {
                "uri": "file:///tmp/main.rs",
                "range": {
                    "start": {"line": 1, "character": 0},
                    "end": {"line": 1, "character": 3}
                }
            }
        })
        .to_string();
        let result = client.parse_goto_definition_response(&json).unwrap();
        assert!(result.is_some());
    }

    #[tokio::test]
    async fn initialize_over_transport_writes_and_reads() {
        let client = LspClient::new();
        let (client_side, mut server_side) = duplex(4096);

        let server = tokio::spawn(async move {
            let request = crate::transport::read_frame(&mut server_side)
                .await
                .unwrap();
            assert!(request.contains("\"method\":\"initialize\""));
            let response = serde_json::json!({
                "jsonrpc": "2.0",
                "id": 1,
                "result": { "capabilities": {} }
            })
            .to_string();
            crate::transport::write_frame(&mut server_side, &response)
                .await
                .unwrap();
        });

        let (mut client_read, mut client_write) = tokio::io::split(client_side);
        let response = client
            .initialize_via_transport(
                &mut client_read,
                &mut client_write,
                InitializeParams::default(),
            )
            .await
            .unwrap();
        assert!(response.contains("\"capabilities\""));
        server.await.unwrap();
    }
}
