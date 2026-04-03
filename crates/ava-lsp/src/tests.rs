use std::fs;

use ava_config::{LspConfig, LspMode, LspServerConfig};
use tempfile::tempdir;

use crate::manager::LspManager;

fn fake_server_script() -> &'static str {
    r#"
let buffer = Buffer.alloc(0);

function send(message) {
  const body = Buffer.from(JSON.stringify(message), 'utf8');
  process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
  process.stdout.write(body);
}

function onMessage(message) {
  if (message.method === 'initialize') {
    send({ jsonrpc: '2.0', id: message.id, result: { capabilities: {} } });
    return;
  }
  if (message.method === 'shutdown') {
    send({ jsonrpc: '2.0', id: message.id, result: null });
    return;
  }
  if (message.method === 'textDocument/didOpen' || message.method === 'textDocument/didChange') {
    const uri = message.params.textDocument.uri;
    send({
      jsonrpc: '2.0',
      method: 'textDocument/publishDiagnostics',
      params: {
        uri,
        diagnostics: [{
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 4 } },
          severity: 1,
          source: 'fake-lsp',
          message: 'simulated error'
        }]
      }
    });
    return;
  }
  if (message.method === 'textDocument/definition') {
    const uri = message.params.textDocument.uri;
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: [{
        uri,
        range: { start: { line: 1, character: 2 }, end: { line: 1, character: 6 } }
      }]
    });
    return;
  }
  if (message.method === 'textDocument/hover') {
    send({ jsonrpc: '2.0', id: message.id, result: { contents: 'hover text' } });
    return;
  }
  if (message.method === 'textDocument/documentSymbol') {
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: [{
        name: 'demo',
        kind: 12,
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 4 } },
        selectionRange: { start: { line: 0, character: 0 }, end: { line: 0, character: 4 } }
      }]
    });
    return;
  }
  if (message.method === 'workspace/symbol') {
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: [{
        name: 'demo',
        kind: 12,
        location: {
          uri: 'file:///tmp/demo.rs',
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 4 } }
        }
      }]
    });
    return;
  }
  send({ jsonrpc: '2.0', id: message.id, result: null });
}

process.stdin.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n');
    if (headerEnd === -1) break;
    const header = buffer.slice(0, headerEnd).toString('utf8');
    const match = header.match(/Content-Length: (\d+)/i);
    if (!match) {
      buffer = Buffer.alloc(0);
      break;
    }
    const length = Number(match[1]);
    const total = headerEnd + 4 + length;
    if (buffer.length < total) break;
    const body = buffer.slice(headerEnd + 4, total).toString('utf8');
    buffer = buffer.slice(total);
    onMessage(JSON.parse(body));
  }
});
"#
}

#[tokio::test]
async fn manager_handles_fake_server_diagnostics_and_definition() {
    let temp = tempdir().expect("tempdir");
    let workspace = temp.path().join("workspace");
    fs::create_dir_all(&workspace).expect("workspace");
    let file_path = workspace.join("main.rs");
    fs::write(&file_path, "fn demo() {}\n").expect("source file");
    let script_path = temp.path().join("fake-lsp.js");
    fs::write(&script_path, fake_server_script()).expect("script");

    let manager = LspManager::new(
        workspace.clone(),
        LspConfig {
            mode: LspMode::OnDemand,
            idle_timeout_secs: 60,
            diagnostics_wait_ms: 500,
            max_active_servers: 1,
            max_open_files_per_server: 4,
            servers: vec![LspServerConfig {
                name: "rust".to_string(),
                enabled: true,
                command: "node".to_string(),
                args: vec![script_path.display().to_string()],
                file_extensions: vec!["rs".to_string()],
            }],
        },
        true,
    );

    let diagnostics = manager.diagnostics(&file_path).await.expect("diagnostics");
    assert_eq!(diagnostics.len(), 1);
    assert_eq!(diagnostics[0].severity, "error");
    assert_eq!(diagnostics[0].message, "simulated error");

    let locations = manager
        .definition(&file_path, 0, 0)
        .await
        .expect("definition");
    assert_eq!(locations.len(), 1);
    assert_eq!(locations[0].line, 2);
    assert_eq!(locations[0].column, 3);

    let hover = manager.hover(&file_path, 0, 0).await.expect("hover");
    assert_eq!(hover.as_deref(), Some("hover text"));

    let snapshot = manager.snapshot().await;
    assert!(snapshot.enabled);
    assert_eq!(snapshot.summary.errors, 1);
    assert_eq!(snapshot.active_server_count, 1);
}
