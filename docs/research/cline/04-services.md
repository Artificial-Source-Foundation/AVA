# Cline Services Layer

> Analysis of Cline's service abstractions for browser, MCP, telemetry, and authentication

---

## Overview

Cline's services layer implements modular, singleton-based services for cross-cutting concerns. Services are organized into logical domains with emphasis on cross-platform compatibility (VSCode, JetBrains, CLI).

---

## Service Catalog

### 1. Authentication Services

**AuthService** (`auth/AuthService.ts`)
- Manages user authentication and token lifecycle
- Token refresh with deduplication via `_refreshPromise`
- Organization support with active org tracking
- Auth status change broadcasting to subscribers

**ClineAuthProvider**
- OAuth 2.0 PKCE flow with Cline's identity provider
- Returns `ClineAuthInfo` with user data and organization context

### 2. Browser Automation Services

**BrowserSession** (`browser/BrowserSession.ts`)

| Feature | Description |
|---------|-------------|
| Local mode | Launches headless Chrome via puppeteer-core |
| Remote mode | Connects to Chrome via WebSocket at localhost:9222 |
| Endpoint caching | 1-hour TTL for WebSocket endpoints |
| Auto-discovery | Discovers Chrome instances on localhost |
| Screenshot formats | WebP (default) and PNG fallback |
| Telemetry | Tracks action count, session duration, errors |

**Key Methods:**
- `launchBrowser()` - Starts Chrome (local or remote)
- `navigateToUrl()` - Navigate with network/DOM stability checks
- `click()`, `type()`, `scrollDown()`, `scrollUp()` - Page interactions
- `doAction()` - Generic action wrapper with error logging

**BrowserDiscovery**
- Auto-discovery of Chrome instances on localhost:9222
- Connection testing via Chrome DevTools Protocol

### 3. MCP Services

**McpHub** (`mcp/McpHub.ts`)

**Features:**
- Lifecycle management for MCP server connections
- File watcher on `mcp-settings.json` for real-time updates
- Environment variable expansion: `${env:VAR_NAME}` syntax
- OAuth support for SSE and HTTP transports
- Remote config restrictions for enterprise allowlisting

**Transport Types:**
1. **stdio**: Child process with custom environment
2. **SSE**: Server-Sent Events with OAuth and auto-reconnect
3. **streamableHttp**: HTTP POST with GET fallback

**McpOAuthManager**
- Token storage and auth lifecycle
- Handles token refresh and validation

### 4. Telemetry Services

**TelemetryService** (`telemetry/TelemetryService.ts`)

**Architecture:**
- Multi-provider pattern: PostHog and OpenTelemetry simultaneously
- Category-based event filtering
- 100+ event capture methods

**Event Categories:**
- Task lifecycle (created, completed)
- Token usage and cache tracking
- Tool usage and MCP tool calls
- Browser automation events
- Hook execution with status tracking
- Auth flows

**Metrics:**
- `cline.turns.total`, `cline.api.ttft.seconds`
- Per-task aggregation for turns, tool calls, errors
- Cache hit ratio tracking

### 5. Error Services

**ErrorService** (`error/ErrorService.ts`)
- Centralized error logging and tracking
- Abstract provider pattern
- Error transformation to `ClineError` with model/provider context

### 6. Feature Flags Services

**FeatureFlagsService** (`feature-flags/FeatureFlagsService.ts`)
- Runtime feature control independent of telemetry
- 1-hour cache TTL
- Flags: `WEBTOOLS`, `WORKTREES`, `ONBOARDING_MODELS`

### 7. Account Services

**ClineAccountService** (`account/ClineAccountService.ts`)
- Cline API integration for account/billing
- Balance, usage transactions, payment history
- Organization credits and account switching
- Audio transcription API

### 8. Banner Services

**BannerService** (`banner/BannerService.ts`)
- Contextual banners/promotions
- Backend filtering by IDE, version, audience
- Client-side filtering by API provider
- Currently disabled to prevent blocking

### 9. Supporting Services

| Service | Purpose |
|---------|---------|
| VoiceTranscriptionService | Audio transcription with language hints |
| AudioRecordingService | Platform-specific audio recording |
| SharedUriHandler | URI parsing and validation |
| ClineTempManager | Temporary file/directory management |
| Tree-sitter Services | Language parsing for 12+ languages |

---

## Key Architectural Patterns

### Singleton Pattern
```typescript
// AuthService, ErrorService, ClineAccountService, BannerService
class Service {
  private static instance: Service
  static getInstance(): Service
  static initialize(context): void
}
```

### Provider Pattern
- Telemetry: PostHog + OpenTelemetry factories
- Error: Abstract error providers
- Feature flags: Abstract flag providers

### Streaming Subscriptions
- Auth status changes broadcast to multiple webview subscribers
- Real-time MCP notifications via handler registry

### File Watching
- MCP settings file watched via chokidar
- Debounced with 100ms stability threshold
- Cline-specific settings skip server restart

---

## Notable Features for AVA

### 1. Remote Browser Support
Robust remote Chrome debugging via WebSocket + auto-discovery.

### 2. MCP OAuth Support
Full OAuth flow with state validation, token refresh, auto-reconnect.

### 3. Advanced Telemetry
100+ event types with metrics aggregation, cache tracking, error isolation.

### 4. Multi-Provider Telemetry
Dual tracking (PostHog + OpenTelemetry simultaneously).

### 5. Voice Transcription
Native dictation with platform-specific recording.

### 6. Dynamic Feature Flags
Backend-driven feature control with onboarding model payloads.

### 7. Billing/Account Integration
Complete account service (balance, transactions, org switching).

### 8. Organization Support
Multi-org with active org tracking, org-specific billing.

### 9. MCP Notification Handling
Real-time message notifications from MCP servers.

### 10. Browser Session Telemetry
Tracks action count, session duration, errors per task ULID.
