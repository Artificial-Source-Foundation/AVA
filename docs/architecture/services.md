# Service Layer

> Business logic and data access

---

## Overview

```
src/services/
├── index.ts           # Barrel export
├── database.ts        # SQLite CRUD
├── migrations.ts      # Schema versioning
│
├── auth/              # Authentication
│   ├── index.ts
│   ├── credentials.ts # API key storage
│   └── oauth-codex.ts # OAuth PKCE flow
│
└── llm/               # LLM Integration
    ├── index.ts
    ├── client.ts      # Provider abstraction
    └── providers/
        ├── index.ts
        ├── anthropic.ts
        └── openrouter.ts
```

---

## Database Service

Location: `src/services/database.ts`

### Initialization

```typescript
let db: Database | null = null;

export async function initDatabase(): Promise<Database> {
  if (db) return db;
  db = await Database.load("sqlite:estela.db");
  await runMigrations(db);
  return db;
}
```

### Session Operations

```typescript
// Create
export async function createSession(name: string): Promise<Session>

// Read
export async function getSessions(): Promise<Session[]>
export async function getSessionsWithStats(): Promise<SessionWithStats[]>
export async function getSession(id: string): Promise<Session | null>

// Update
export async function updateSession(
  id: string,
  updates: Partial<Pick<Session, "name" | "status" | "metadata">>
): Promise<void>
export async function touchSession(id: string): Promise<void>

// Delete
export async function deleteSession(id: string): Promise<void>
export async function archiveSession(id: string): Promise<void>
```

### Message Operations

```typescript
// Create
export async function saveMessage(msg: Omit<Message, "id" | "createdAt">): Promise<Message>

// Read
export async function getMessagesBySession(sessionId: string): Promise<Message[]>

// Update
export async function updateMessage(
  id: string,
  updates: Partial<Pick<Message, "content" | "tokensUsed" | "metadata">>
): Promise<void>
```

### Agent Operations

```typescript
export async function createAgent(agent: Omit<Agent, "id" | "createdAt">): Promise<Agent>
export async function getAgentsBySession(sessionId: string): Promise<Agent[]>
export async function updateAgent(id: string, updates: Partial<Agent>): Promise<void>
```

---

## Migrations Service

Location: `src/services/migrations.ts`

### API

```typescript
const SCHEMA_VERSION = 1;

export async function runMigrations(db: Database): Promise<void>
```

### Migration Functions

```typescript
// Each version has its own function
async function migrateV1(db: Database): Promise<void> {
  // Create sessions table
  // Create messages table
  // Create agents table
  // Create file_changes table
  // Create indexes
}
```

### Adding a Migration

1. Increment `SCHEMA_VERSION`
2. Add `migrateVN()` function
3. Add case to migration switch

---

## Auth Service

Location: `src/services/auth/`

### Credentials

```typescript
// Get/Set credentials
export function getCredentials(provider: LLMProvider): Credentials | null
export function setCredentials(provider: LLMProvider, credentials: Credentials): void
export function clearCredentials(provider: LLMProvider): void

// Helpers
export function hasAnyCredentials(): boolean
export function listConfiguredProviders(): LLMProvider[]

// Convenience
export function setApiKey(provider: LLMProvider, key: string): void
export function getApiKey(provider: LLMProvider): string | null
export function getApiKeyWithFallback(provider: LLMProvider): string | null
```

### OAuth (Codex)

```typescript
// PKCE Flow
export function startCodexAuth(): { authUrl: string; codeVerifier: string }
export async function exchangeCodeForTokens(
  code: string,
  codeVerifier: string
): Promise<TokenResponse>
export async function refreshCodexToken(refreshToken: string): Promise<TokenResponse>

// State
export function hasPendingOAuth(): boolean
export function cancelOAuthFlow(): void
```

---

## LLM Service

Location: `src/services/llm/`

### Client Factory

```typescript
export type ResolvedAuth = {
  provider: LLMProvider;
  credentials: Credentials;
  model: string;
};

// Resolve best auth for a model
export function resolveAuth(model: string): ResolvedAuth | null

// Create provider-specific client
export async function createClient(provider: LLMProvider): Promise<LLMClient>
```

### Provider Priority

```typescript
// resolveAuth() checks in order:
1. OAuth token (if available and valid)
2. Direct API key (provider-specific)
3. OpenRouter key (gateway fallback)
```

### LLMClient Interface

```typescript
export interface LLMClient {
  stream(
    messages: Array<{ role: string; content: string }>,
    config: StreamConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta>;
}

export interface StreamConfig {
  provider: LLMProvider;
  model: string;
  authMethod: "oauth" | "api-key";
  maxTokens: number;
}

export interface StreamDelta {
  content?: string;
  error?: StreamError;
  done?: boolean;
  usage?: { totalTokens: number };
}
```

### Providers

#### Anthropic (`providers/anthropic.ts`)

```typescript
export function createAnthropicClient(apiKey: string): LLMClient
```

- Direct Anthropic API
- Streaming via SSE
- Native Claude models

#### OpenRouter (`providers/openrouter.ts`)

```typescript
export function createOpenRouterClient(apiKey: string): LLMClient
```

- OpenRouter gateway API
- Supports multiple models
- Unified API for any provider

---

## Import Patterns

### Barrel Exports

```typescript
// Import from service root
import { initDatabase, saveMessage, updateMessage } from '../services';

// Or from specific module
import { createClient, resolveAuth } from '../services/llm';

// Or from provider
import { createAnthropicClient } from '../services/llm/providers';
```

### Service Dependencies

```
┌─────────────┐     ┌─────────────┐
│   useChat   │────►│   client    │
└─────────────┘     └─────────────┘
       │                   │
       │            ┌──────┴──────┐
       │            │             │
       ▼            ▼             ▼
┌─────────────┐  ┌─────────┐  ┌─────────────┐
│  database   │  │  auth   │  │  providers  │
└─────────────┘  └─────────┘  └─────────────┘
```

---

## Error Handling

### Database Errors

```typescript
try {
  await initDatabase();
} catch (error) {
  // Handle connection/migration errors
}
```

### Stream Errors

```typescript
export interface StreamError {
  type: "auth" | "rate-limit" | "network" | "server" | "unknown";
  message: string;
  retryAfter?: number;
}
```

### Auth Errors

```typescript
// resolveAuth returns null if no valid credentials
const resolved = resolveAuth(model);
if (!resolved) {
  setError({
    type: 'auth',
    message: 'No credentials configured. Please add an API key in Settings.',
  });
  return;
}
```

---

## Future Services

Planned for later epics:

```
src/services/
├── tools/              # Tool execution
│   ├── fileEdit.ts     # str_replace, create_file
│   ├── fileRead.ts     # Read file contents
│   ├── bash.ts         # Shell execution
│   └── search.ts       # Grep/ripgrep
│
├── agents/             # Multi-agent orchestration
│   ├── commander.ts    # Commander agent
│   ├── operator.ts     # Operator agents
│   └── validator.ts    # Validator agent
│
└── documentation/      # Context management
    ├── docManager.ts   # Manage /docs folder
    └── compressor.ts   # Context compaction
```
