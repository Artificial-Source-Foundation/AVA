# Epic 14: Memory System

> Long-term memory, RAG, persistent knowledge

---

## Goal

Build persistent memory for long-term knowledge retention, cross-session learning, and retrieval-augmented generation.

---

## Prerequisites

- Epic 12 (Codebase) - Embedding utilities
- Epic 13 (Config) - Memory settings

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 14.1 | Memory Types | Define memory schemas, vector store | ~250 |
| 14.2 | Episodic Memory | Session summaries, decisions | ~300 |
| 14.3 | Semantic Memory | Facts, concepts, embeddings | ~400 |
| 14.4 | Procedural Memory | Learned patterns, tool usage | ~250 |
| 14.5 | Memory Manager | Consolidation, decay, manager | ~200 |

**Total:** ~1400 lines

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    MemoryManager                         │
│  (coordinates all memory subsystems)                     │
└─────────────────────────────────────────────────────────┘
         │
         ├─► EpisodicMemory (session summaries, decisions)
         │
         ├─► SemanticMemory (facts, concepts, embeddings)
         │
         ├─► ProceduralMemory (learned patterns, tool usage)
         │
         └─► WorkingMemory (current context, active tasks)
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│              VectorStore (SQLite + embeddings)           │
└─────────────────────────────────────────────────────────┘
```

---

## Memory Types

### Episodic Memory
- Session summaries
- Decisions made
- Outcomes (success/failure)
- Timestamped events

### Semantic Memory
- Learned facts
- Concepts and relationships
- Source tracking
- Confidence scores

### Procedural Memory
- Successful patterns
- Tool usage sequences
- Context-action mappings
- Success rate tracking

---

## Key Interfaces

```typescript
interface MemoryEntry {
  id: string
  type: 'episodic' | 'semantic' | 'procedural'
  content: string
  embedding?: number[]
  metadata: {
    timestamp: number
    importance: number
    accessCount: number
    lastAccessed: number
    tags: string[]
  }
}

interface MemoryManager {
  remember(entry: Omit<MemoryEntry, 'id' | 'metadata'>): Promise<string>
  recall(query: MemoryQuery): Promise<MemoryEntry[]>
  recallSimilar(text: string, limit?: number): Promise<MemoryEntry[]>
  reinforce(id: string): Promise<void>
  forget(id: string): Promise<void>
  consolidate(): Promise<void>
}
```

---

## Key Features

### Vector Similarity Search
```typescript
// SQLite with vector extension
async findSimilar(embedding: number[], limit: number): Promise<MemoryEntry[]> {
  return this.db.all(`
    SELECT *,
      vector_cosine(embedding, ?) as similarity
    FROM memories
    WHERE embedding IS NOT NULL
    ORDER BY similarity DESC
    LIMIT ?
  `, [embedding, limit])
}
```

### Memory Consolidation
```typescript
async consolidate(): Promise<void> {
  // 1. Decay old, low-importance memories
  await this.decayOldMemories()

  // 2. Merge similar semantic memories
  await this.mergeSimilarFacts()

  // 3. Promote frequently accessed memories
  await this.promoteActiveMemories()
}
```

### Memory Decay Formula
```
importance(t) = importance(0) * e^(-λt) + access_boost

where:
- t = time since creation
- λ = decay rate
- access_boost = f(accessCount, lastAccessed)
```

---

## File Structure

```
packages/core/src/memory/
├── index.ts           # Re-exports
├── types.ts           # Memory interfaces
├── store.ts           # SQLite vector store
├── embedding.ts       # Text embedding utilities
├── episodic.ts        # Episodic memory
├── semantic.ts        # Semantic memory
├── procedural.ts      # Procedural memory
├── manager.ts         # MemoryManager implementation
└── consolidation.ts   # Decay/merge logic
```

---

## Acceptance Criteria

- [ ] Session summaries automatically recorded
- [ ] Facts learned from interactions
- [ ] Patterns tracked with success rates
- [ ] Vector similarity search works
- [ ] Memory consolidation runs without data loss
- [ ] Recall integrates with agent context
- [ ] Export/import memory as JSON
