# Type Definitions

> TypeScript types and interfaces

---

## Overview

```
src/types/
├── index.ts    # Core types (Session, Message, Agent, etc.)
└── llm.ts      # LLM-specific types (Provider, Credentials, etc.)
```

---

## Core Types (`index.ts`)

### Session

```typescript
export interface Session {
  id: string;
  name: string;
  createdAt: number;
  updatedAt: number;
  status: "active" | "archived";
  metadata?: Record<string, unknown>;
}

export interface SessionWithStats extends Session {
  messageCount: number;
  totalTokens: number;
  lastPreview?: string;
}
```

### Message

```typescript
export interface Message {
  id: string;
  sessionId: string;
  role: "user" | "assistant" | "system";
  content: string;
  tokensUsed?: number;
  model?: string;
  metadata?: MessageMetadata;
  createdAt: number;
  error?: MessageError;
}

export interface MessageMetadata {
  editedAt?: number;
  [key: string]: unknown;
}

export interface MessageError {
  type: string;
  message: string;
  retryAfter?: number;
  timestamp: number;
}
```

### Agent

```typescript
export interface Agent {
  id: string;
  sessionId: string;
  type: "commander" | "operator" | "validator";
  model: string;
  status: "idle" | "working" | "completed" | "failed";
  assignedFiles?: string[];
  tokensUsed: number;
  createdAt: number;
}
```

### File Change

```typescript
export interface FileChange {
  id: string;
  sessionId: string;
  agentId?: string;
  filePath: string;
  changeType: "create" | "edit" | "delete";
  oldContent?: string;
  newContent?: string;
  createdAt: number;
}
```

---

## LLM Types (`llm.ts`)

### Provider

```typescript
export type LLMProvider = "anthropic" | "openai" | "openrouter" | "codex";
```

### Credentials

```typescript
export interface Credentials {
  provider: LLMProvider;
  type: "api-key" | "oauth-token";
  value: string;
  expiresAt?: number;  // For OAuth tokens
  refreshToken?: string;
}
```

### Stream Types

```typescript
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
  usage?: {
    promptTokens?: number;
    completionTokens?: number;
    totalTokens?: number;
  };
}

export interface StreamError {
  type: "auth" | "rate-limit" | "network" | "server" | "unknown";
  message: string;
  retryAfter?: number;
}
```

### Client Interface

```typescript
export interface LLMClient {
  stream(
    messages: Array<{ role: string; content: string }>,
    config: StreamConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta>;
}
```

---

## Database Types

### Row Types (from SQLite)

```typescript
// Internal types matching database columns
interface SessionRow {
  id: string;
  name: string;
  created_at: number;
  updated_at: number;
  status: string;
  metadata: string | null;
}

interface MessageRow {
  id: string;
  session_id: string;
  role: string;
  content: string;
  tokens_used: number | null;
  model: string | null;
  metadata: string | null;
  created_at: number;
}
```

### Mapping Functions

```typescript
// Convert snake_case DB rows to camelCase types
function mapDbSessions(rows: SessionRow[]): Session[]
function mapDbMessages(rows: MessageRow[]): Message[]
function mapDbAgents(rows: AgentRow[]): Agent[]
```

---

## Component Props Types

### Common Pattern

```typescript
// Props interface named {Component}Props
interface MessageBubbleProps {
  message: Message;
  isEditing: boolean;
  isRetrying: boolean;
  isStreaming: boolean;
  onStartEdit: () => void;
  onSaveEdit: (content: string) => Promise<void>;
  onCancelEdit: () => void;
  onCopy: () => void;
  onRetry: () => void;
  onRegenerate: () => void;
}

interface SessionListItemProps {
  session: SessionWithStats;
  isActive: boolean;
  onSelect: () => void;
  onRename: (name: string) => void;
  onArchive: () => void;
}
```

---

## Store Types

### Session Store

```typescript
// Return type of useSession()
interface SessionStore {
  // State (signals)
  sessions: Accessor<SessionWithStats[]>;
  currentSession: Accessor<Session | null>;
  messages: Accessor<Message[]>;
  selectedModel: Accessor<string>;
  isLoadingSessions: Accessor<boolean>;
  editingMessageId: Accessor<string | null>;
  retryingMessageId: Accessor<string | null>;

  // Actions
  loadAllSessions: () => Promise<void>;
  createNewSession: (name?: string) => Promise<Session>;
  switchSession: (id: string) => Promise<void>;
  renameSession: (id: string, name: string) => Promise<void>;
  archiveSession: (id: string) => Promise<void>;
  deleteSessionPermanently: (id: string) => Promise<void>;
  addMessage: (message: Message) => void;
  updateMessageContent: (id: string, content: string) => void;
  setMessageError: (id: string, error: MessageError | null) => void;
  deleteMessage: (id: string) => void;
  deleteMessagesAfter: (id: string) => void;
  startEditing: (id: string) => void;
  stopEditing: () => void;
  setRetryingMessageId: (id: string | null) => void;
  setSelectedModel: (model: string) => void;
}
```

---

## Hook Types

### useChat Return Type

```typescript
interface UseChatReturn {
  // State
  isStreaming: Accessor<boolean>;
  error: Accessor<StreamError | null>;
  currentProvider: Accessor<LLMProvider | null>;

  // Actions
  sendMessage: (content: string, model?: string) => Promise<void>;
  cancel: () => void;
  clearError: () => void;
  retryMessage: (assistantMessageId: string) => Promise<void>;
  editAndResend: (messageId: string, newContent: string) => Promise<void>;
  regenerateResponse: (assistantMessageId: string) => Promise<void>;
}
```

---

## Type Guards

```typescript
// Example type guards for runtime validation
function isStreamError(obj: unknown): obj is StreamError {
  return (
    typeof obj === "object" &&
    obj !== null &&
    "type" in obj &&
    "message" in obj
  );
}

function isMessage(obj: unknown): obj is Message {
  return (
    typeof obj === "object" &&
    obj !== null &&
    "id" in obj &&
    "role" in obj &&
    "content" in obj
  );
}
```

---

## Import Patterns

```typescript
// Core types
import type { Session, Message, Agent, SessionWithStats } from '../types';

// LLM types
import type { LLMProvider, Credentials, StreamError, LLMClient } from '../types/llm';

// Combined
import type { Session, Message } from '../types';
import type { LLMProvider, StreamConfig } from '../types/llm';
```

---

## Future Types

Planned for later epics:

```typescript
// Tool types
interface ToolCall {
  id: string;
  name: string;
  arguments: Record<string, unknown>;
}

interface ToolResult {
  toolCallId: string;
  content: string;
  isError?: boolean;
}

// Multi-agent types
interface AgentMessage extends Message {
  agentId: string;
  targetAgentId?: string;
}

interface TaskAssignment {
  agentId: string;
  task: string;
  files: string[];
  status: "pending" | "in_progress" | "completed";
}
```
