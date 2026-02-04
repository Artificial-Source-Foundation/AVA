# Epic 12: Codebase Understanding

> Codebase context and repo map

---

## Goal

Build deep understanding of the codebase structure, relationships, and patterns to enable more intelligent code modifications.

---

## Prerequisites

- Epic 5 (Context) - Session state, token tracking

---

## Reference Implementations

| Feature | Source | Stars |
|---------|--------|-------|
| Repo map | Aider | 25k+ |
| Tree-sitter AST | OpenCode | 70k+ |
| Symbol indexing | Cursor | - |

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 12.1 | File Index | Index all files with metadata | ~250 |
| 12.2 | Symbol Extraction | Parse exports, classes, functions | ~400 |
| 12.3 | Dependency Graph | Map imports/exports relationships | ~300 |
| 12.4 | Repo Map Generation | Create compact codebase summary | ~250 |

**Total:** ~1200 lines

---

## Architecture

```
Codebase
    │
    ▼
┌─────────────────┐
│  File Indexer   │ ──► File paths, sizes, mtimes
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Symbol Extractor│ ──► Functions, classes, exports
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Dependency Graph│ ──► Import/export relationships
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Repo Map      │ ──► Compact summary for LLM
└─────────────────┘
```

---

## Key Features

### File Index
```typescript
interface FileIndex {
  path: string
  size: number
  mtime: number
  language: string
  symbols: Symbol[]
}

interface Symbol {
  name: string
  type: 'function' | 'class' | 'variable' | 'type' | 'export'
  line: number
  exported: boolean
}
```

### Repo Map (Aider-style)
```typescript
function generateRepoMap(index: FileIndex[]): string {
  let map = '# Repository Structure\n\n'

  // Group by directory
  const byDir = groupBy(index, f => dirname(f.path))

  for (const [dir, files] of Object.entries(byDir)) {
    map += `## ${dir}/\n`

    for (const file of files) {
      map += `\n### ${basename(file.path)}\n`

      // List exports
      const exports = file.symbols.filter(s => s.exported)
      if (exports.length > 0) {
        map += 'Exports:\n'
        for (const sym of exports) {
          map += `- ${sym.type} ${sym.name}\n`
        }
      }
    }
  }

  return map
}
```

### Smart File Selection
```typescript
// Select relevant files for a task
async function selectRelevantFiles(
  task: string,
  index: FileIndex[],
  maxTokens: number
): Promise<string[]> {
  // 1. Keyword matching
  const keywords = extractKeywords(task)
  let candidates = index.filter(f =>
    keywords.some(k => f.path.includes(k) || f.symbols.some(s => s.name.includes(k)))
  )

  // 2. Dependency expansion
  candidates = expandDependencies(candidates, index)

  // 3. Fit within token budget
  return fitToTokenBudget(candidates, maxTokens)
}
```

---

## Acceptance Criteria

- [ ] All source files indexed with symbols
- [ ] Import/export graph accurately represents dependencies
- [ ] Repo map fits within context limits
- [ ] Relevant files auto-selected for tasks
- [ ] Index updates on file changes
