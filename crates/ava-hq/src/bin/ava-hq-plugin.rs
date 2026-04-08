use ava_hq::plugin_host::{handle_command, handle_route, initialize_response, PluginContext};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::io::{self, BufRead, Write};

#[derive(Debug, Deserialize)]
struct JsonRpcRequest {
    #[allow(dead_code)]
    jsonrpc: String,
    id: Option<Value>,
    method: String,
    params: Option<Value>,
}

#[derive(Debug, Serialize)]
struct JsonRpcResponse {
    jsonrpc: &'static str,
    id: Value,
    #[serde(skip_serializing_if = "Option::is_none")]
    result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<JsonRpcError>,
}

#[derive(Debug, Serialize)]
struct JsonRpcError {
    code: i64,
    message: String,
}

fn main() {
    if let Err(error) = run() {
        eprintln!("ava-hq-plugin failed: {error}");
    }
}

fn run() -> Result<(), String> {
    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut reader = io::BufReader::new(stdin.lock());
    let mut writer = io::BufWriter::new(stdout.lock());
    let mut context = PluginContext::default();

    loop {
        let Some(request) = read_request(&mut reader)? else {
            break;
        };

        match request.method.as_str() {
            "initialize" => {
                context = serde_json::from_value(request.params.unwrap_or(Value::Null))
                    .map_err(|error| format!("invalid initialize payload: {error}"))?;
                write_result(
                    &mut writer,
                    request.id,
                    serde_json::to_value(initialize_response()).map_err(|error| {
                        format!("failed to serialize initialize response: {error}")
                    })?,
                )?;
            }
            "app.command" => {
                let params = request.params.unwrap_or(Value::Null);
                let command = params
                    .get("command")
                    .and_then(Value::as_str)
                    .ok_or_else(|| "missing app.command command name".to_string())?;
                let payload = params.get("payload").cloned().unwrap_or(Value::Null);
                match handle_command(&context, command, payload) {
                    Ok(result) => write_result(
                        &mut writer,
                        request.id,
                        serde_json::to_value(result).map_err(|error| {
                            format!("failed to serialize app.command result: {error}")
                        })?,
                    )?,
                    Err(message) => {
                        write_error(&mut writer, request.id, -32001, &message)?;
                    }
                }
            }
            "app.route" => {
                let params = request.params.unwrap_or(Value::Null);
                let method = params
                    .get("method")
                    .and_then(Value::as_str)
                    .ok_or_else(|| "missing app.route method".to_string())?;
                let path = params
                    .get("path")
                    .and_then(Value::as_str)
                    .ok_or_else(|| "missing app.route path".to_string())?;
                let query = params.get("query").cloned().unwrap_or(Value::Null);
                let body = params.get("body").cloned();
                match handle_route(&context, method, path, query, body) {
                    Ok(result) => write_result(
                        &mut writer,
                        request.id,
                        serde_json::to_value(result).map_err(|error| {
                            format!("failed to serialize app.route result: {error}")
                        })?,
                    )?,
                    Err(message) => {
                        write_error(&mut writer, request.id, -32002, &message)?;
                    }
                }
            }
            "shutdown" => break,
            other => {
                if request.id.is_some() {
                    write_error(
                        &mut writer,
                        request.id,
                        -32601,
                        &format!("unsupported method '{other}'"),
                    )?;
                }
            }
        }
    }

    writer
        .flush()
        .map_err(|error| format!("flush failed: {error}"))?;
    Ok(())
}

fn read_request(reader: &mut impl BufRead) -> Result<Option<JsonRpcRequest>, String> {
    let Some(content_length) = read_content_length(reader)? else {
        return Ok(None);
    };
    let mut body = vec![0_u8; content_length];
    reader
        .read_exact(&mut body)
        .map_err(|error| format!("failed to read request body: {error}"))?;
    serde_json::from_slice(&body)
        .map(Some)
        .map_err(|error| format!("failed to decode request body: {error}"))
}

fn read_content_length(reader: &mut impl BufRead) -> Result<Option<usize>, String> {
    let mut content_length = None;

    loop {
        let mut line = String::new();
        let bytes_read = reader
            .read_line(&mut line)
            .map_err(|error| format!("failed to read request headers: {error}"))?;

        if bytes_read == 0 {
            return if content_length.is_some() {
                Err("unexpected EOF while reading request headers".to_string())
            } else {
                Ok(None)
            };
        }

        if line == "\r\n" {
            break;
        }

        let trimmed = line.trim_end();
        let Some((key, value)) = trimmed.split_once(':') else {
            return Err(format!("invalid header line: {trimmed}"));
        };

        if key.eq_ignore_ascii_case("content-length") {
            content_length = Some(
                value
                    .trim()
                    .parse::<usize>()
                    .map_err(|error| format!("invalid content-length header: {error}"))?,
            );
        }
    }

    content_length
        .ok_or_else(|| "missing content-length header".to_string())
        .map(Some)
}

fn write_result(writer: &mut impl Write, id: Option<Value>, result: Value) -> Result<(), String> {
    let response = JsonRpcResponse {
        jsonrpc: "2.0",
        id: id.unwrap_or(Value::Null),
        result: Some(result),
        error: None,
    };
    write_response(writer, &response)
}

fn write_error(
    writer: &mut impl Write,
    id: Option<Value>,
    code: i64,
    message: &str,
) -> Result<(), String> {
    let response = JsonRpcResponse {
        jsonrpc: "2.0",
        id: id.unwrap_or(Value::Null),
        result: None,
        error: Some(JsonRpcError {
            code,
            message: message.to_string(),
        }),
    };
    write_response(writer, &response)
}

fn write_response(writer: &mut impl Write, response: &JsonRpcResponse) -> Result<(), String> {
    let payload = serde_json::to_vec(response)
        .map_err(|error| format!("failed to serialize response: {error}"))?;
    write!(writer, "Content-Length: {}\r\n\r\n", payload.len())
        .map_err(|error| format!("failed to write response headers: {error}"))?;
    writer
        .write_all(&payload)
        .map_err(|error| format!("failed to write response body: {error}"))?;
    writer
        .flush()
        .map_err(|error| format!("failed to flush response: {error}"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn read_content_length_parses_standard_header() {
        let input = b"Content-Length: 42\r\n\r\n";
        let mut reader = io::BufReader::new(&input[..]);
        let length = read_content_length(&mut reader).expect("header should parse");
        assert_eq!(length, Some(42));
    }

    #[test]
    fn write_result_emits_json_rpc_frame() {
        let mut buffer = Vec::new();
        write_result(&mut buffer, Some(json!(1)), json!({"ok": true}))
            .expect("response should serialize");
        let output = String::from_utf8(buffer).expect("frame should be utf-8");
        assert!(output.contains("Content-Length:"));
        assert!(output.contains("\"jsonrpc\":\"2.0\""));
        assert!(output.contains("\"ok\":true"));
    }
}
