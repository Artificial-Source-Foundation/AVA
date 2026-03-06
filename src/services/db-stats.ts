/**
 * Database Project Usage Stats
 *
 * Aggregate queries for project-level usage analytics.
 */

import { initDatabase } from './db-init'

// ============================================================================
// Types
// ============================================================================

export interface ProjectUsageStats {
  totalTokens: number
  totalCost: number
  sessionCount: number
  messageCount: number
}

export interface ModelBreakdownEntry {
  model: string
  usageCount: number
  totalTokens: number
  totalCost: number
}

export interface DailyUsageEntry {
  date: string
  tokens: number
  cost: number
  messages: number
}

// ============================================================================
// Queries
// ============================================================================

/** Aggregate usage stats across all sessions for a project */
export async function getProjectUsageStats(projectId: string): Promise<ProjectUsageStats> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost,
      COUNT(DISTINCT s.id) as session_count,
      COUNT(m.id) as message_count
    FROM sessions s
    LEFT JOIN messages m ON m.session_id = s.id
    WHERE s.project_id = ?`,
    [projectId]
  )
  const row = rows[0] || {}
  return {
    totalTokens: (row.total_tokens as number) || 0,
    totalCost: (row.total_cost as number) || 0,
    sessionCount: (row.session_count as number) || 0,
    messageCount: (row.message_count as number) || 0,
  }
}

/** Group usage by model for a project */
export async function getModelBreakdown(projectId: string): Promise<ModelBreakdownEntry[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      COALESCE(m.model, 'unknown') as model,
      COUNT(*) as usage_count,
      COALESCE(SUM(m.tokens_used), 0) as total_tokens,
      COALESCE(SUM(m.cost_usd), 0) as total_cost
    FROM messages m
    JOIN sessions s ON s.id = m.session_id
    WHERE s.project_id = ? AND m.model IS NOT NULL
    GROUP BY m.model
    ORDER BY total_tokens DESC`,
    [projectId]
  )
  return rows.map((row) => ({
    model: row.model as string,
    usageCount: (row.usage_count as number) || 0,
    totalTokens: (row.total_tokens as number) || 0,
    totalCost: (row.total_cost as number) || 0,
  }))
}

/** Daily usage aggregates for the last N days */
export async function getDailyUsage(projectId: string, days = 30): Promise<DailyUsageEntry[]> {
  const database = await initDatabase()
  const cutoff = Date.now() - days * 24 * 60 * 60 * 1000
  const rows = await database.select<Array<Record<string, unknown>>>(
    `SELECT
      date(m.created_at / 1000, 'unixepoch') as date,
      COALESCE(SUM(m.tokens_used), 0) as tokens,
      COALESCE(SUM(m.cost_usd), 0) as cost,
      COUNT(*) as messages
    FROM messages m
    JOIN sessions s ON s.id = m.session_id
    WHERE s.project_id = ? AND m.created_at >= ?
    GROUP BY date(m.created_at / 1000, 'unixepoch')
    ORDER BY date ASC`,
    [projectId, cutoff]
  )
  return rows.map((row) => ({
    date: row.date as string,
    tokens: (row.tokens as number) || 0,
    cost: (row.cost as number) || 0,
    messages: (row.messages as number) || 0,
  }))
}
