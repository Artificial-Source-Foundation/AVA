use std::collections::HashMap;

use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

use crate::error::{LspError, Result};

pub fn encode_message(payload: &str) -> String {
    format!("Content-Length: {}\r\n\r\n{}", payload.len(), payload)
}

pub fn decode_message(frame: &str) -> Result<String> {
    let (headers, body) = frame
        .split_once("\r\n\r\n")
        .ok_or_else(|| LspError::Protocol("missing header delimiter".to_string()))?;

    let headers = parse_headers(headers)?;
    let len = headers
        .get("content-length")
        .ok_or_else(|| LspError::Protocol("missing content-length header".to_string()))?
        .parse::<usize>()
        .map_err(|e| LspError::Protocol(format!("invalid content-length: {e}")))?;

    if body.len() != len {
        return Err(LspError::Protocol(format!(
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
            .ok_or_else(|| LspError::Protocol(format!("invalid header line: {line}")))?;
        headers.insert(key.trim().to_ascii_lowercase(), value.trim().to_string());
    }
    Ok(headers)
}

pub async fn write_frame<W: AsyncWrite + Unpin>(writer: &mut W, payload: &str) -> Result<()> {
    let frame = encode_message(payload);
    writer.write_all(frame.as_bytes()).await?;
    writer.flush().await?;
    Ok(())
}

pub async fn read_frame<R: AsyncRead + Unpin>(reader: &mut R) -> Result<String> {
    let mut header_bytes = Vec::new();
    let mut byte = [0_u8; 1];
    loop {
        let n = reader.read(&mut byte).await?;
        if n == 0 {
            return Err(LspError::Protocol(
                "unexpected EOF while reading headers".to_string(),
            ));
        }
        header_bytes.push(byte[0]);
        if header_bytes.ends_with(b"\r\n\r\n") {
            break;
        }
    }

    let header_text = String::from_utf8(header_bytes)
        .map_err(|e| LspError::Protocol(format!("invalid header bytes: {e}")))?;
    let headers = parse_headers(header_text.trim_end_matches("\r\n\r\n"))?;
    let len = headers
        .get("content-length")
        .ok_or_else(|| LspError::Protocol("missing content-length header".to_string()))?
        .parse::<usize>()
        .map_err(|e| LspError::Protocol(format!("invalid content-length: {e}")))?;

    let mut body = vec![0_u8; len];
    reader.read_exact(&mut body).await?;
    String::from_utf8(body).map_err(|e| LspError::Protocol(format!("invalid UTF-8 body: {e}")))
}

#[cfg(test)]
mod tests {
    use tokio::io::duplex;

    use super::*;

    #[test]
    fn encodes_lsp_frame() {
        let payload = r#"{"jsonrpc":"2.0","id":1}"#;
        let frame = encode_message(payload);
        assert!(frame.starts_with("Content-Length:"));
        assert!(frame.ends_with(payload));
    }

    #[test]
    fn decodes_valid_frame() {
        let payload = r#"{"jsonrpc":"2.0","id":1}"#;
        let frame = encode_message(payload);
        let decoded = decode_message(&frame).unwrap();
        assert_eq!(decoded, payload);
    }

    #[test]
    fn decode_rejects_bad_length() {
        let frame = "Content-Length: 3\r\n\r\nabcdef";
        let err = decode_message(frame).unwrap_err();
        assert!(format!("{err}").contains("mismatch"));
    }

    #[tokio::test]
    async fn async_round_trip_frame() {
        let payload = r#"{"jsonrpc":"2.0","id":1,"result":null}"#;
        let (mut a, mut b) = duplex(1024);

        let write_task = tokio::spawn(async move {
            write_frame(&mut a, payload).await.unwrap();
        });

        let read_payload = read_frame(&mut b).await.unwrap();
        write_task.await.unwrap();
        assert_eq!(read_payload, payload);
    }
}
