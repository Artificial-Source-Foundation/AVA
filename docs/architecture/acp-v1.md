# ACP v1 (Agent Client Protocol)

## Goal

Define a minimal in-process protocol surface that allows external callers to interact with Praxis coordination primitives without coupling to internal Rust types.

## Scope (v1)

- Transport: in-process only (`InProcessAcpTransport`)
- Request envelope: `AcpRequest { method, payload_json }`
- Response envelope: `AcpResponse { ok, payload_json, error }`
- Methods:
  - `CreateSpec`
  - `ListSpecs`
  - `ListArtifacts`
  - `SendPeerMessage`
  - `ReadMailbox`

## Why JSON payloads

v1 intentionally uses JSON string payloads to keep the boundary stable while internal schemas evolve. This keeps ACP clients decoupled from crate internals and avoids transport-specific lock-in.

## Event model

`AcpHandler` maps protocol requests onto Praxis primitives and emits `PraxisEvent` records where relevant:

- `SpecCreated`
- `PeerMessageSent`
- `AcpRequestHandled` (fallback for methods without a dedicated domain event)

`InProcessAcpTransport` stores emitted events so callers can consume protocol-side effects.

## Non-goals

- No network transport in v1 (no HTTP/WebSocket/gRPC)
- No auth policy layer in v1
- No streaming responses in v1

## Upgrade path

Future versions can add transport adapters and auth wrappers while preserving request/response semantics and method naming.
