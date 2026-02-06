# MCP OAuth Flow: Plan + State Diagram

> Add OAuth-based MCP server auth and richer connection states

---

## Goals

- Support MCP OAuth for hosted servers
- Persist credentials per server
- Expose clear server states to UI

---

## Proposed States

```ts
export type MCPServerStatus =
  | 'disconnected'
  | 'connecting'
  | 'connected'
  | 'needs_auth'
  | 'needs_client_registration'
  | 'failed'
```

---

## OAuth Flow (High-Level)

1. **Discovery**: Server advertises OAuth details (auth URL, token URL, scopes, client registration info).
2. **Needs Auth**: Client marks server `needs_auth`.
3. **Start Auth**:
   - Generate PKCE verifier + challenge.
   - Launch system browser to auth URL with redirect URI.
4. **Finish Auth**:
   - Receive auth code via local callback or deep link.
   - Exchange code for access token (and refresh token).
   - Persist tokens in credentials store.
5. **Reconnect**: Retry MCP connection with Authorization header.

---

## Interfaces

**File**: `packages/core/src/mcp/types.ts`

```ts
export interface MCPOAuthConfig {
  authUrl: string
  tokenUrl: string
  clientId?: string
  registrationUrl?: string
  scopes?: string[]
  redirectUri: string
}

export interface MCPServerConfig {
  // ...existing
  oauth?: MCPOAuthConfig
}
```

---

## Implementation Touchpoints

- `packages/core/src/mcp/client.ts`
  - add OAuth handshake and token storage
- `packages/core/src/auth/`
  - reuse PKCE helpers
- `packages/core/src/mcp/discovery.ts`
  - parse OAuth metadata from server info
- `packages/core/src/mcp/types.ts`
  - expand status enum + oauth config

---

## UI Wiring

- Server card shows:
  - status badge
  - action button: `Connect`, `Authenticate`, `Retry`
- If `needs_client_registration`, open registration URL

---

## Open Questions

- Callback transport: custom URI scheme vs local HTTP server
- Token refresh policy (on connect vs lazy refresh)
- Storing refresh tokens (encrypted vs plaintext)
