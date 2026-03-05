use std::collections::HashMap;

use ava_types::{AvaError, Result};
use serde_json::Value;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt, BufReader};

pub fn encode_message(payload: &str) -> String {
    format!("Content-Length: {}\r\n\r\n{}", payload.len(), payload)
}

pub fn decode_message(frame: &str) -> Result<String> {
    let (headers, body) = frame
        .split_once("\r\n\r\n")
        .ok_or_else(|| AvaError::ValidationError("missing header delimiter".to_string()))?;

    let headers = parse_headers(headers)?;
    let len = headers
        .get("content-length")
        .ok_or_else(|| AvaError::ValidationError("missing content-length header".to_string()))?
        .parse::<usize>()
        .map_err(|error| AvaError::ValidationError(format!("invalid content-length: {error}")))?;

    if body.len() != len {
        return Err(AvaError::ValidationError(format!(
            "body length mismatch: expected {len}, got {}",
            body.len()
        )));
    }

    Ok(body.to_string())
}

fn parse_headers(raw: &str) -> Result<HashMap<String, String>> {
    let mut headers = HashMap::new();
    for line in raw.lines() {
        let (key, value) = line
            .split_once(':')
            .ok_or_else(|| AvaError::ValidationError(format!("invalid header line: {line}")))?;
        headers.insert(key.trim().to_ascii_lowercase(), value.trim().to_string());
    }
    Ok(headers)
}

pub struct MCPTransport<R, W>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    reader: BufReader<R>,
    writer: W,
}

impl<R, W> MCPTransport<R, W>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    pub fn new(reader: R, writer: W) -> Self {
        Self {
            reader: BufReader::new(reader),
            writer,
        }
    }

    pub async fn send(&mut self, value: Value) -> Result<()> {
        let payload = serde_json::to_string(&value)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;
        let frame = encode_message(&payload);
        self.writer
            .write_all(frame.as_bytes())
            .await
            .map_err(AvaError::from)?;
        self.writer.flush().await.map_err(AvaError::from)?;
        Ok(())
    }

    pub async fn receive(&mut self) -> Result<Value> {
        let mut header_bytes = Vec::new();
        let mut byte = [0_u8; 1];

        loop {
            let n = self.reader.read(&mut byte).await.map_err(AvaError::from)?;
            if n == 0 {
                return Err(AvaError::ValidationError(
                    "unexpected EOF while reading headers".to_string(),
                ));
            }

            header_bytes.push(byte[0]);
            if header_bytes.ends_with(b"\r\n\r\n") {
                break;
            }
        }

        let header_text = String::from_utf8(header_bytes)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;
        let headers = parse_headers(header_text.trim_end_matches("\r\n\r\n"))?;
        let len = headers
            .get("content-length")
            .ok_or_else(|| AvaError::ValidationError("missing content-length header".to_string()))?
            .parse::<usize>()
            .map_err(|error| {
                AvaError::ValidationError(format!("invalid content-length header: {error}"))
            })?;

        let mut body = vec![0_u8; len];
        self.reader
            .read_exact(&mut body)
            .await
            .map_err(AvaError::from)?;

        let body = String::from_utf8(body)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;
        serde_json::from_str(&body).map_err(|error| AvaError::SerializationError(error.to_string()))
    }
}
