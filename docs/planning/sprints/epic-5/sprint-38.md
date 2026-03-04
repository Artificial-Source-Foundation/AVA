# Sprint 38: Testing & Migration

**Epic:** Frontend Integration (Epic 5)  
**Duration:** 2 weeks  
**Goal:** E2E tests, remove old code, migration guide

## Stories

### Story 5.7: End-to-End Tests
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
E2E tests:

```typescript
// tests/e2e/agent.spec.ts
test('agent can edit a file', async () => {
  // Start app
  const app = await launchApp();
  
  // Send goal
  await app.sendGoal('Create a hello world file');
  
  // Wait for completion
  await app.waitForCompletion();
  
  // Verify file created
  const content = await app.readFile('hello.txt');
  expect(content).toContain('Hello World');
});

test('edit tool works', async () => {
  const app = await launchApp();
  
  // Create file
  await app.createFile('test.txt', 'Hello');
  
  // Edit via tool
  await app.executeTool('edit', {
    path: 'test.txt',
    search: 'Hello',
    replace: 'Hi'
  });
  
  // Verify
  const content = await app.readFile('test.txt');
  expect(content).toBe('Hi');
});
```

**Acceptance Criteria:**
- [ ] E2E tests pass
- [ ] Full workflows tested
- [ ] CI integration

---

### Story 5.8: Remove TypeScript Backend
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to do:**
- Delete `packages/core/src/` (backend code)
- Keep frontend code only
- Update imports

```bash
# Delete backend
rm -rf packages/core/src/{agent,codebase,commander,config,context,diff,git,hooks,llm,lsp,mcp,memory,models,permissions,scheduler,session,validator,tools}

# Keep only:
# - packages/core/src/platform.ts (interface)
# - packages/core/src/types.ts (shared types)
```

**Acceptance Criteria:**
- [ ] No TS backend code
- [ ] Frontend still works
- [ ] Tests pass

---

### Story 5.9: Migration Guide
**Points:** 4 (AI: 2 hrs, Human: 2 hrs)

**What to write:**
`docs/MIGRATION.md`:

```markdown
# Migration Guide: TS to Rust Backend

## Breaking Changes

### Tool API
Before:
```typescript
import { executeTool } from './tools';
```

After:
```typescript
import { invoke } from '@tauri-apps/api/tauri';
const result = await invoke('execute_tool', { ... });
```

### Agent API
Before:
```typescript
import { AgentLoop } from './agent';
const agent = new AgentLoop();
```

After:
```typescript
import { invoke } from '@tauri-apps/api/tauri';
const session = await invoke('agent_run', { goal });
```

## Configuration
Update `ava.config.json`:
- New format
- New options
```

**Acceptance Criteria:**
- [ ] Guide complete
- [ ] Breaking changes documented
- [ ] Migration steps clear

---

## Epic 5 Complete!

**Success Criteria:**
- [ ] Frontend integrated
- [ ] E2E tests pass
- [ ] Old code removed
- [ ] Docs written

**Next:** Epic 6 - Ship It (Sprint 39)
