# Data Flow

> How data flows through the Estela application

---

## Overview

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   UI Layer   │ ──► │ State Layer  │ ──► │Service Layer │
│  Components  │ ◄── │   Stores     │ ◄── │   Database   │
└──────────────┘     └──────────────┘     └──────────────┘
```

---

## Application Initialization

```
App.onMount()
    │
    ├─► initDatabase()
    │       └─► runMigrations()
    │
    ├─► loadAllSessions()
    │       └─► getSessionsWithStats() → setSessions()
    │
    └─► restoreLastSession()
            │
            ├─► switchSession(lastSessionId)
            │       └─► loadSessionMessages()
            │
            └─► createNewSession() (if no last session)
```

---

## Chat Message Flow

### Sending a Message

```
User types message
    │
    ▼
MessageInput.onSubmit()
    │
    ▼
useChat().sendMessage(content)
    │
    ├─► Validate auth (resolveAuth)
    │       │
    │       └─► Returns { provider, credentials, model }
    │
    ├─► Create/ensure session
    │       └─► session.createNewSession() if needed
    │
    ├─► Save user message to DB
    │       └─► saveMessage({ sessionId, role: 'user', content })
    │
    ├─► Add to store
    │       └─► session.addMessage(userMsg)
    │
    ├─► Create assistant placeholder
    │       └─► saveMessage({ role: 'assistant', content: '' })
    │
    └─► streamResponse()
            │
            ├─► createClient(provider)
            │
            └─► client.stream(messages, config)
                    │
                    ├─► onContent(delta)
                    │       └─► session.updateMessageContent()
                    │
                    ├─► onComplete(fullContent, tokens)
                    │       └─► updateMessage() in DB
                    │
                    └─► onError(error)
                            └─► session.setMessageError()
```

### Retry Flow

```
User clicks retry
    │
    ▼
MessageActions.onRetry()
    │
    ▼
useChat().retryMessage(assistantId)
    │
    ├─► Find preceding user message
    │
    ├─► Mark retrying: session.setRetryingMessageId()
    │
    ├─► Clear error: session.setMessageError(null)
    │
    ├─► Delete failed message: session.deleteMessage()
    │
    └─► regenerate()  (creates new assistant message)
```

### Edit Flow

```
User clicks edit
    │
    ▼
MessageActions.onEdit()
    │
    ▼
session.startEditing(messageId)
    │
    ▼
EditForm renders with current content
    │
    ▼
User saves edit
    │
    ▼
useChat().editAndResend(messageId, newContent)
    │
    ├─► Update message: session.updateMessageContent()
    │
    ├─► Save to DB: updateMessage()
    │
    ├─► Delete messages after: session.deleteMessagesAfter()
    │
    ├─► Stop editing: session.stopEditing()
    │
    └─► regenerate()
```

---

## Session Management Flow

### Create Session

```
User clicks "New Chat"
    │
    ▼
SessionList.onNewSession()
    │
    ▼
session.createNewSession(name?)
    │
    ├─► createSession() → DB
    │
    ├─► setSessions([newSession, ...existing])
    │
    ├─► setCurrentSession(newSession)
    │
    └─► setMessages([])
```

### Switch Session

```
User clicks session in list
    │
    ▼
SessionListItem.onSelect()
    │
    ▼
session.switchSession(id)
    │
    ├─► setCurrentSession(session)
    │
    ├─► getMessagesBySession(id) → DB
    │
    ├─► setMessages(messages)
    │
    └─► localStorage.setItem(LAST_SESSION, id)
```

### Archive Session

```
User clicks archive
    │
    ▼
SessionListItem.onArchive()
    │
    ▼
session.archiveSession(id)
    │
    ├─► updateSession(id, { status: 'archived' }) → DB
    │
    ├─► setSessions(filter out archived)
    │
    └─► if current session archived:
            │
            ├─► switchSession(remaining[0])
            │
            └─► or createNewSession()
```

---

## Credential Flow

### Provider Resolution

```
resolveAuth(model)
    │
    ├─► Check for OAuth token (Codex)
    │       └─► If valid & not expired → return OAuth
    │
    ├─► Check for direct API key (Anthropic/OpenAI)
    │       └─► getCredentials(provider)
    │               └─► If exists → return API key
    │
    └─► Check for OpenRouter key (fallback)
            └─► getCredentials('openrouter')
                    └─► If exists → return OpenRouter
```

### Priority Order

```
1. OAuth token (Claude.ai)
   └─► Preferred for Claude models

2. Direct API key
   └─► Anthropic: claude-* models
   └─► OpenAI: gpt-* models

3. OpenRouter gateway
   └─► Fallback for any model
```

---

## LLM Streaming Flow

```
createClient(provider)
    │
    └─► Returns LLMClient interface
            │
            └─► stream(messages, config, signal)
                    │
                    ├─► Build request (provider-specific)
                    │
                    ├─► fetch() with streaming
                    │
                    └─► AsyncGenerator yields:
                            │
                            ├─► { content: "delta text" }
                            │
                            ├─► { error: StreamError }
                            │
                            └─► { done: true, usage: { totalTokens } }
```

---

## State Synchronization

### Store → Database

| Action | Store Update | DB Write |
|--------|--------------|----------|
| Send message | addMessage() | saveMessage() |
| Update content | updateMessageContent() | updateMessage() |
| Delete message | deleteMessage() | (via store only) |
| Create session | setSessions() | createSession() |
| Switch session | setCurrentSession() | - |
| Rename session | update in sessions() | updateSession() |
| Archive session | filter from sessions() | updateSession() |

### Database → Store

| Event | DB Read | Store Update |
|-------|---------|--------------|
| App init | getSessionsWithStats() | setSessions() |
| Session switch | getMessagesBySession() | setMessages() |
| Restore last | getSession() | setCurrentSession() |

---

## Error Handling Flow

```
Error occurs
    │
    ├─► Stream error
    │       │
    │       └─► setError(error)
    │               │
    │               └─► session.setMessageError(msgId, error)
    │                       │
    │                       └─► UI shows retry button
    │
    ├─► Auth error
    │       │
    │       └─► Show "Add API key in Settings"
    │
    └─► Network error
            │
            └─► Show error with retry option
```

---

## Reactive Updates

SolidJS signals provide automatic UI updates:

```typescript
// Store (session.ts)
const [messages, setMessages] = createSignal<Message[]>([]);

// Component (MessageList.tsx)
<For each={session.messages()}>
  {(message) => <MessageBubble message={message} />}
</For>

// When setMessages() is called, MessageList re-renders automatically
```
