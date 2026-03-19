/**
 * Plan persistence — saves and loads plans to/from `.ava/plans/`.
 *
 * Uses Tauri's `fs` plugin in desktop mode, falls back to localStorage
 * in web mode (plans are small JSON, so this is fine).
 */

import { isTauri } from '@tauri-apps/api/core'
import { log } from '../lib/logger'
import type { PlanData } from '../types/rust-ipc'

/** Persisted plan with metadata. */
export interface PersistedPlan {
  /** File path (desktop) or localStorage key (web). */
  path: string
  /** The session this plan was created in. */
  sessionId: string
  /** ISO timestamp of when the plan was saved. */
  savedAt: string
  /** The plan data itself. */
  plan: PlanData
}

const PLANS_DIR = '.ava/plans'
const LS_PREFIX = 'ava:plan:'

/** Generate a slug from the plan summary (first 40 chars, lowercased, dashes). */
function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '')
    .slice(0, 40)
}

/** Save a plan. Returns the storage path/key. */
export async function savePlan(plan: PlanData, sessionId: string): Promise<string> {
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
  const slug = slugify(plan.summary)
  const filename = `${timestamp}-${slug}.json`

  const persisted: PersistedPlan = {
    path: '',
    sessionId,
    savedAt: new Date().toISOString(),
    plan,
  }

  if (isTauri()) {
    try {
      const { mkdir, writeTextFile, BaseDirectory } = await import('@tauri-apps/plugin-fs')
      await mkdir(PLANS_DIR, { baseDir: BaseDirectory.Home, recursive: true })
      const filePath = `${PLANS_DIR}/${filename}`
      persisted.path = filePath
      await writeTextFile(filePath, JSON.stringify(persisted, null, 2), {
        baseDir: BaseDirectory.Home,
      })
      log.info('plan', 'Plan saved', { path: filePath })
      return filePath
    } catch (err) {
      log.error('plan', 'Failed to save plan via Tauri fs', { error: String(err) })
      // Fall through to localStorage
    }
  }

  // Web mode / fallback: use localStorage
  const key = `${LS_PREFIX}${filename}`
  persisted.path = key
  try {
    localStorage.setItem(key, JSON.stringify(persisted))
    log.info('plan', 'Plan saved to localStorage', { key })
  } catch (err) {
    log.error('plan', 'Failed to save plan to localStorage', { error: String(err) })
  }
  return key
}

/** Load all saved plans, most recent first. */
export async function loadPlans(): Promise<PersistedPlan[]> {
  const plans: PersistedPlan[] = []

  if (isTauri()) {
    try {
      const { readDir, readTextFile, BaseDirectory } = await import('@tauri-apps/plugin-fs')
      const entries = await readDir(PLANS_DIR, { baseDir: BaseDirectory.Home })
      for (const entry of entries) {
        if (!entry.name?.endsWith('.json')) continue
        try {
          const content = await readTextFile(`${PLANS_DIR}/${entry.name}`, {
            baseDir: BaseDirectory.Home,
          })
          const parsed = JSON.parse(content) as PersistedPlan
          plans.push(parsed)
        } catch {
          // Skip malformed files
        }
      }
    } catch {
      // Directory doesn't exist yet — that's fine
    }
  } else {
    // Web mode: scan localStorage
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i)
      if (!key?.startsWith(LS_PREFIX)) continue
      try {
        const raw = localStorage.getItem(key)
        if (raw) {
          plans.push(JSON.parse(raw) as PersistedPlan)
        }
      } catch {
        // Skip malformed entries
      }
    }
  }

  // Sort by savedAt descending (most recent first)
  plans.sort((a, b) => b.savedAt.localeCompare(a.savedAt))
  return plans
}

/** Load a specific plan by its storage path/key. */
export async function loadPlan(path: string): Promise<PersistedPlan | null> {
  if (isTauri()) {
    try {
      const { readTextFile, BaseDirectory } = await import('@tauri-apps/plugin-fs')
      const content = await readTextFile(path, { baseDir: BaseDirectory.Home })
      return JSON.parse(content) as PersistedPlan
    } catch {
      return null
    }
  }

  // Web mode: path is a localStorage key
  try {
    const raw = localStorage.getItem(path)
    return raw ? (JSON.parse(raw) as PersistedPlan) : null
  } catch {
    return null
  }
}
