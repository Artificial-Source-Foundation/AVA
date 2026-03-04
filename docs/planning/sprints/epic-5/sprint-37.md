# Sprint 37: Frontend Updates

**Epic:** Frontend Integration (Epic 5)  
**Duration:** 2 weeks  
**Goal:** Update TS frontend to use Rust backend

## Stories

### Story 5.4: Tool Hooks
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
`src/hooks/useTools.ts`:

```typescript
import { invoke } from '@tauri-apps/api/tauri';

export interface ToolCall {
  name: string;
  arguments: unknown;
}

export interface ToolResult {
  content: string;
  is_error: boolean;
}

export async function executeTool(call: ToolCall): Promise<ToolResult> {
  return await invoke('execute_tool', {
    tool: call.name,
    args: call.arguments
  });
}

export async function listTools(): Promise<ToolInfo[]> {
  return await invoke('list_tools');
}

// React hook
export function useTools() {
  const [tools, setTools] = useState<ToolInfo[]>([]);
  
  useEffect(() => {
    listTools().then(setTools);
  }, []);
  
  const execute = async (call: ToolCall) => {
    return await executeTool(call);
  };
  
  return { tools, execute };
}
```

**Acceptance Criteria:**
- [ ] Tools hook works
- [ ] Calls Rust backend
- [ ] Lists all 35 tools

---

### Story 5.5: Agent Hook
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
`src/hooks/useAgent.ts`:

```typescript
import { invoke } from '@tauri-apps/api/tauri';
import { listen } from '@tauri-apps/api/event';

export interface AgentEvent {
  type: 'token' | 'tool_call' | 'tool_result' | 'progress' | 'complete' | 'error';
  content?: string;
  name?: string;
  args?: unknown;
  is_error?: boolean;
  message?: string;
  session?: Session;
}

export function useAgent() {
  const [isRunning, setIsRunning] = useState(false);
  const [events, setEvents] = useState<AgentEvent[]>([]);
  
  const run = async (goal: string) => {
    setIsRunning(true);
    setEvents([]);
    
    try {
      const session = await invoke('agent_run', { goal });
      return session;
    } finally {
      setIsRunning(false);
    }
  };
  
  const stream = async (
    goal: string,
    onEvent: (event: AgentEvent) => void
  ) => {
    setIsRunning(true);
    
    // Listen for events
    const unlisten = await listen('agent-event', (event) => {
      onEvent(event.payload as AgentEvent);
    });
    
    try {
      await invoke('agent_stream', { goal });
    } finally {
      unlisten();
      setIsRunning(false);
    }
  };
  
  return { run, stream, isRunning, events };
}
```

**Acceptance Criteria:**
- [ ] Agent hook works
- [ ] Streaming events work
- [ ] Progress visible

---

### Story 5.6: UI Updates
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
- Per-hunk review UI
- Streaming display
- Progress indicators

```tsx
// Per-hunk review
export function DiffReview({ changes }: { changes: Change[] }) {
  return (
    <div className="diff-review">
      {changes.map((change, i) => (
        <div key={i} className={`change ${change.type}`}>
          <pre>{change.content}</pre>
          <button onClick={() => acceptChange(i)}>Accept</button>
          <button onClick={() => rejectChange(i)}>Reject</button>
        </div>
      ))}
    </div>
  );
}

// Streaming tokens
export function StreamingOutput({ tokens }: { tokens: string[] }) {
  return (
    <div className="streaming-output">
      {tokens.map((token, i) => (
        <span key={i}>{token}</span>
      ))}
    </div>
  );
}
```

**Acceptance Criteria:**
- [ ] Per-hunk UI works
- [ ] Streaming displays
- [ ] Progress shown

---

## Sprint Goal

**Success Criteria:**
- [ ] Frontend uses Rust backend
- [ ] Tools execute via Rust
- [ ] Agent runs in Rust

**Next:** Sprint 38 - Testing & Cleanup
