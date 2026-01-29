import Database from "@tauri-apps/plugin-sql";
import type { Session, Message, Agent, FileChange } from "../types";

let db: Database | null = null;

// Initialize database connection
export async function initDatabase(): Promise<Database> {
  if (db) return db;

  db = await Database.load("sqlite:estela.db");

  // Run migrations (in production, use proper migration system)
  // For now, the schema is created on first run

  return db;
}

// Session operations
export async function createSession(name: string): Promise<Session> {
  const database = await initDatabase();
  const id = crypto.randomUUID();
  const now = Date.now();

  await database.execute(
    "INSERT INTO sessions (id, name, created_at, updated_at) VALUES (?, ?, ?, ?)",
    [id, name, now, now]
  );

  return { id, name, createdAt: now, updatedAt: now, status: "active" };
}

export async function getSessions(): Promise<Session[]> {
  const database = await initDatabase();
  const rows = await database.select<Session[]>(
    "SELECT * FROM sessions ORDER BY updated_at DESC"
  );
  return rows;
}

// Message operations
export async function saveMessage(message: Omit<Message, "id" | "createdAt">): Promise<Message> {
  const database = await initDatabase();
  const id = crypto.randomUUID();
  const createdAt = Date.now();

  await database.execute(
    `INSERT INTO messages (id, session_id, role, content, agent_id, created_at, tokens_used, metadata)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [id, message.sessionId, message.role, message.content, message.agentId || null, createdAt, message.tokensUsed || 0, JSON.stringify(message.metadata || {})]
  );

  return { ...message, id, createdAt };
}

export async function getMessages(sessionId: string): Promise<Message[]> {
  const database = await initDatabase();
  const rows = await database.select<Message[]>(
    "SELECT * FROM messages WHERE session_id = ? ORDER BY created_at ASC",
    [sessionId]
  );
  return rows;
}

// Agent operations
export async function createAgent(agent: Omit<Agent, "id" | "createdAt">): Promise<Agent> {
  const database = await initDatabase();
  const id = crypto.randomUUID();
  const createdAt = Date.now();

  await database.execute(
    `INSERT INTO agents (id, session_id, type, status, model, created_at, assigned_files, task_description)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [id, agent.sessionId, agent.type, agent.status, agent.model, createdAt, JSON.stringify(agent.assignedFiles || []), agent.taskDescription || null]
  );

  return { ...agent, id, createdAt };
}

export async function updateAgentStatus(id: string, status: Agent["status"], result?: Agent["result"]): Promise<void> {
  const database = await initDatabase();
  const completedAt = status === "completed" || status === "error" ? Date.now() : null;

  await database.execute(
    "UPDATE agents SET status = ?, completed_at = ?, result = ? WHERE id = ?",
    [status, completedAt, result ? JSON.stringify(result) : null, id]
  );
}

// File change operations
export async function saveFileChange(change: Omit<FileChange, "id" | "createdAt" | "reverted">): Promise<FileChange> {
  const database = await initDatabase();
  const id = crypto.randomUUID();
  const createdAt = Date.now();

  await database.execute(
    `INSERT INTO file_changes (id, session_id, agent_id, file_path, change_type, old_content, new_content, created_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    [id, change.sessionId, change.agentId, change.filePath, change.changeType, change.oldContent || null, change.newContent || null, createdAt]
  );

  return { ...change, id, createdAt, reverted: false };
}
