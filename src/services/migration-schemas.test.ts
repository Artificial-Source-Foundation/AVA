import { beforeEach, describe, expect, it, vi } from 'vitest'
import { migrateV1, migrateV2, migrateV3 } from './migration-schemas'

// ============================================================================
// Mock Database
// ============================================================================

function createMockDb() {
  const statements: string[] = []
  const args: unknown[][] = []
  return {
    db: {
      execute: vi.fn(async (sql: string, params?: unknown[]) => {
        statements.push(sql)
        if (params) args.push(params)
      }),
    } as unknown as import('@tauri-apps/plugin-sql').default,
    statements,
    args,
  }
}

// ============================================================================
// migrateV1 – Initial schema
// ============================================================================

describe('migrateV1', () => {
  let mock: ReturnType<typeof createMockDb>

  beforeEach(() => {
    mock = createMockDb()
  })

  it('creates sessions table', async () => {
    await migrateV1(mock.db)
    const createSessions = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('sessions')
    )
    expect(createSessions).toBeDefined()
    expect(createSessions).toContain('id TEXT PRIMARY KEY')
    expect(createSessions).toContain('name TEXT NOT NULL')
    expect(createSessions).toContain('status TEXT NOT NULL')
  })

  it('creates messages table with FK to sessions', async () => {
    await migrateV1(mock.db)
    const createMessages = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('messages')
    )
    expect(createMessages).toBeDefined()
    expect(createMessages).toContain('session_id TEXT NOT NULL')
    expect(createMessages).toContain('FOREIGN KEY (session_id) REFERENCES sessions(id)')
    expect(createMessages).toContain('ON DELETE CASCADE')
  })

  it('creates agents table with FK to sessions', async () => {
    await migrateV1(mock.db)
    const createAgents = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('agents')
    )
    expect(createAgents).toBeDefined()
    expect(createAgents).toContain('model TEXT NOT NULL')
  })

  it('creates file_changes table with FKs', async () => {
    await migrateV1(mock.db)
    const createFC = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('file_changes')
    )
    expect(createFC).toBeDefined()
    expect(createFC).toContain('FOREIGN KEY (agent_id) REFERENCES agents(id)')
  })

  it('creates indexes for messages and sessions', async () => {
    await migrateV1(mock.db)
    const indexStatements = mock.statements.filter((s) => s.includes('CREATE INDEX'))
    expect(indexStatements.length).toBeGreaterThanOrEqual(4)
    expect(indexStatements.some((s) => s.includes('idx_messages_session'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_messages_created'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_agents_session'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_sessions_updated'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_sessions_status'))).toBe(true)
  })

  it('calls execute for each DDL statement', async () => {
    await migrateV1(mock.db)
    // 4 tables + 5 indexes = 9 statements
    expect(mock.db.execute).toHaveBeenCalledTimes(9)
  })
})

// ============================================================================
// migrateV2 – Projects table
// ============================================================================

describe('migrateV2', () => {
  let mock: ReturnType<typeof createMockDb>

  beforeEach(() => {
    mock = createMockDb()
  })

  it('creates projects table', async () => {
    await migrateV2(mock.db)
    const createProjects = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('projects')
    )
    expect(createProjects).toBeDefined()
    expect(createProjects).toContain('directory TEXT NOT NULL UNIQUE')
  })

  it('creates projects indexes', async () => {
    await migrateV2(mock.db)
    const indexStatements = mock.statements.filter((s) => s.includes('CREATE INDEX'))
    expect(indexStatements.some((s) => s.includes('idx_projects_directory'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_projects_updated'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_projects_last_opened'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_sessions_project'))).toBe(true)
  })

  it('adds project_id column to sessions via ALTER TABLE', async () => {
    await migrateV2(mock.db)
    const alter = mock.statements.find((s) => s.includes('ALTER TABLE sessions'))
    expect(alter).toBeDefined()
    expect(alter).toContain('project_id TEXT')
  })

  it('inserts a default project', async () => {
    await migrateV2(mock.db)
    const insert = mock.statements.find((s) => s.includes('INSERT OR IGNORE INTO projects'))
    expect(insert).toBeDefined()
    // Verify the arguments contain 'default-project' and 'Default Project'
    const insertArgs = mock.args.find((a) => a.includes('default-project'))
    expect(insertArgs).toBeDefined()
    expect(insertArgs).toContain('Default Project')
  })

  it('migrates existing sessions to the default project', async () => {
    await migrateV2(mock.db)
    const update = mock.statements.find((s) => s.includes('UPDATE sessions SET project_id'))
    expect(update).toBeDefined()
    expect(update).toContain('WHERE project_id IS NULL')
  })
})

// ============================================================================
// migrateV3 – File operations, terminal executions, memory items
// ============================================================================

describe('migrateV3', () => {
  let mock: ReturnType<typeof createMockDb>

  beforeEach(() => {
    mock = createMockDb()
  })

  it('creates file_operations table', async () => {
    await migrateV3(mock.db)
    const create = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('file_operations')
    )
    expect(create).toBeDefined()
    expect(create).toContain('file_path TEXT NOT NULL')
    expect(create).toContain('FOREIGN KEY (session_id)')
  })

  it('creates terminal_executions table', async () => {
    await migrateV3(mock.db)
    const create = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('terminal_executions')
    )
    expect(create).toBeDefined()
    expect(create).toContain('command TEXT NOT NULL')
    expect(create).toContain("status TEXT NOT NULL DEFAULT 'running'")
  })

  it('creates memory_items table', async () => {
    await migrateV3(mock.db)
    const create = mock.statements.find(
      (s) => s.includes('CREATE TABLE') && s.includes('memory_items')
    )
    expect(create).toBeDefined()
    expect(create).toContain('title TEXT NOT NULL')
    expect(create).toContain('tokens INTEGER NOT NULL DEFAULT 0')
  })

  it('creates all required indexes', async () => {
    await migrateV3(mock.db)
    const indexStatements = mock.statements.filter((s) => s.includes('CREATE INDEX'))
    expect(indexStatements.length).toBe(6)
    expect(indexStatements.some((s) => s.includes('idx_file_operations_session'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_file_operations_timestamp'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_terminal_executions_session'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_terminal_executions_started'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_memory_items_session'))).toBe(true)
    expect(indexStatements.some((s) => s.includes('idx_memory_items_created'))).toBe(true)
  })

  it('calls execute for each DDL statement', async () => {
    await migrateV3(mock.db)
    // 3 tables + 6 indexes = 9 statements
    expect(mock.db.execute).toHaveBeenCalledTimes(9)
  })
})

// ============================================================================
// Error propagation
// ============================================================================

describe('migration error propagation', () => {
  it('propagates db.execute errors from migrateV1', async () => {
    const { db } = createMockDb()
    ;(db.execute as ReturnType<typeof vi.fn>).mockRejectedValueOnce(new Error('disk full'))

    await expect(migrateV1(db)).rejects.toThrow('disk full')
  })

  it('propagates db.execute errors from migrateV2', async () => {
    const { db } = createMockDb()
    ;(db.execute as ReturnType<typeof vi.fn>).mockRejectedValueOnce(new Error('locked'))

    await expect(migrateV2(db)).rejects.toThrow('locked')
  })

  it('propagates db.execute errors from migrateV3', async () => {
    const { db } = createMockDb()
    ;(db.execute as ReturnType<typeof vi.fn>).mockRejectedValueOnce(new Error('corrupt'))

    await expect(migrateV3(db)).rejects.toThrow('corrupt')
  })
})
