# Memory & Persistence

## File-Based Persistence (Markdown Blocks)

Pattern from `agent-memory`:

```typescript
import * as yaml from "yaml";
import * as fs from "fs/promises";

interface MemoryBlock {
  label: string;
  description: string;
  value: string;
  limit: number;
  readOnly: boolean;
}

async function saveBlock(dir: string, block: MemoryBlock): Promise<void> {
  const filePath = path.join(dir, `${block.label}.md`);

  const frontmatter = yaml.dump({
    label: block.label,
    description: block.description,
    limit: block.limit,
    read_only: block.readOnly,
  });

  const content = `---\n${frontmatter}---\n${block.value.trim()}\n`;

  // Atomic write: temp file + rename
  const tempPath = `${filePath}.tmp`;
  await fs.writeFile(tempPath, content, "utf-8");
  await fs.rename(tempPath, filePath);
}

async function loadBlock(filePath: string): Promise<MemoryBlock | null> {
  try {
    const content = await fs.readFile(filePath, "utf-8");
    const match = content.match(/^---\n([\s\S]*?)\n---\n([\s\S]*)$/);

    if (!match) return null;

    const frontmatter = yaml.parse(match[1]);
    const value = match[2].trim();

    return {
      label: frontmatter.label,
      description: frontmatter.description ?? "",
      value,
      limit: frontmatter.limit ?? 5000,
      readOnly: frontmatter.read_only ?? false,
    };
  } catch {
    return null;
  }
}
```

---

## SQLite with WAL Mode

Pattern from `worktree`:

```typescript
import { Database } from "bun:sqlite";

async function initDatabase(dbPath: string): Promise<Database> {
  const db = new Database(dbPath);

  // WAL mode for concurrent access
  db.exec("PRAGMA journal_mode=WAL");
  db.exec("PRAGMA busy_timeout=5000");

  // Create tables
  db.exec(`
    CREATE TABLE IF NOT EXISTS sessions (
      id TEXT PRIMARY KEY,
      data TEXT NOT NULL,
      created_at TEXT NOT NULL,
      updated_at TEXT NOT NULL
    )
  `);

  return db;
}

// Graceful shutdown
function registerCleanup(db: Database): void {
  const cleanup = () => {
    try {
      db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
      db.close();
    } catch { /* best effort */ }
  };

  process.once("SIGTERM", cleanup);
  process.once("SIGINT", cleanup);
  process.once("beforeExit", cleanup);
}
```

---

## Vector Database Pattern

Pattern from `opencode-mem`:

```typescript
import { drizzle } from "drizzle-orm/bun-sqlite";

// F32_BLOB for vector storage
const memories = sqliteTable("memories", {
  id: text("id").primaryKey(),
  content: text("content").notNull(),
  embedding: blob("embedding", { mode: "buffer" }), // Float32Array as buffer
  created_at: integer("created_at"),
});

async function searchSimilar(
  db: Database,
  queryVector: Float32Array,
  limit: number
): Promise<SearchResult[]> {
  // Convert Float32Array to buffer for SQLite
  const queryBuffer = new Uint8Array(queryVector.buffer);

  const results = db.prepare(`
    SELECT id, content,
           vec_distance_cosine(embedding, ?) as distance
    FROM memories
    ORDER BY distance ASC
    LIMIT ?
  `).all(queryBuffer, limit);

  return results.map(r => ({
    id: r.id,
    content: r.content,
    similarity: 1 - r.distance,
  }));
}
```

---

## Logfmt Append-Only Pattern

Pattern from `simple-memory`:

```typescript
interface LogEntry {
  ts: string;
  type: string;
  scope: string;
  content: string;
  tags?: string[];
}

function formatLogEntry(entry: LogEntry): string {
  let line = `ts=${entry.ts} type=${entry.type} scope=${entry.scope}`;
  line += ` content="${entry.content.replace(/"/g, '\\"')}"`;
  if (entry.tags?.length) {
    line += ` tags=${entry.tags.join(",")}`;
  }
  return line;
}

async function appendLog(logDir: string, entry: LogEntry): Promise<void> {
  const date = new Date().toISOString().split("T")[0];
  const filePath = path.join(logDir, `${date}.logfmt`);

  const line = formatLogEntry(entry) + "\n";

  // Append-only write
  await fs.appendFile(filePath, line, "utf-8");
}

function parseLogLine(line: string): LogEntry | null {
  const tsMatch = line.match(/ts=([^\s]+)/);
  const typeMatch = line.match(/type=([^\s]+)/);
  const scopeMatch = line.match(/scope=([^\s]+)/);
  const contentMatch = line.match(/content="([^"]*(?:\\"[^"]*)*)"/);

  if (!tsMatch || !typeMatch || !scopeMatch) return null;

  return {
    ts: tsMatch[1],
    type: typeMatch[1],
    scope: scopeMatch[1],
    content: contentMatch?.[1]?.replace(/\\"/g, '"') ?? "",
  };
}
```

---

## Choosing a Pattern

| Pattern | Best For | Trade-offs |
|---------|----------|------------|
| **Markdown blocks** | Human-readable, versioned | Slower for large data |
| **SQLite WAL** | Structured queries, concurrent | More complex setup |
| **Vectors** | Semantic search | Requires embeddings |
| **Logfmt** | Append-only audit trails | Hard to query |

---

## Source Reference

- `agent-memory/src/` - Markdown blocks
- `opencode-mem/src/` - Vector database
- `simple-memory/` - Logfmt pattern
- `worktree/src/lib/db.ts` - SQLite WAL
