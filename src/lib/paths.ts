/**
 * Delta9 Path Utilities
 *
 * Centralized path management for Delta9 files.
 */

import { existsSync, mkdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { homedir } from 'node:os'

// =============================================================================
// Constants
// =============================================================================

/** Delta9 state directory name */
export const DELTA9_DIR = '.delta9'

/** Mission state file */
export const MISSION_FILE = 'mission.json'

/** Human-readable mission markdown */
export const MISSION_MD = 'mission.md'

/** History log file */
export const HISTORY_FILE = 'history.jsonl'

/** Project-level config file */
export const CONFIG_FILE = 'config.json'

/** Memory file */
export const MEMORY_FILE = 'memory.json'

/** Checkpoints directory */
export const CHECKPOINTS_DIR = 'checkpoints'

/** Global config directory */
export const GLOBAL_CONFIG_DIR = join(homedir(), '.config', 'opencode')

/** Global Delta9 config file */
export const GLOBAL_CONFIG_FILE = 'delta9.json'

// =============================================================================
// Path Resolvers
// =============================================================================

/**
 * Get the Delta9 directory path for a project
 */
export function getDelta9Dir(cwd: string): string {
  return resolve(cwd, DELTA9_DIR)
}

/**
 * Get the mission.json path
 */
export function getMissionPath(cwd: string): string {
  return join(getDelta9Dir(cwd), MISSION_FILE)
}

/**
 * Get the mission.md path
 */
export function getMissionMdPath(cwd: string): string {
  return join(getDelta9Dir(cwd), MISSION_MD)
}

/**
 * Get the history.jsonl path
 */
export function getHistoryPath(cwd: string): string {
  return join(getDelta9Dir(cwd), HISTORY_FILE)
}

/**
 * Get the project config path
 */
export function getProjectConfigPath(cwd: string): string {
  return join(getDelta9Dir(cwd), CONFIG_FILE)
}

/**
 * Get the memory.json path
 */
export function getMemoryPath(cwd: string): string {
  return join(getDelta9Dir(cwd), MEMORY_FILE)
}

/**
 * Get the checkpoints directory path
 */
export function getCheckpointsDir(cwd: string): string {
  return join(getDelta9Dir(cwd), CHECKPOINTS_DIR)
}

/**
 * Get a specific checkpoint path
 */
export function getCheckpointPath(cwd: string, checkpointName: string): string {
  return join(getCheckpointsDir(cwd), checkpointName)
}

/**
 * Get the global config path
 */
export function getGlobalConfigPath(): string {
  return join(GLOBAL_CONFIG_DIR, GLOBAL_CONFIG_FILE)
}

// =============================================================================
// Directory Management
// =============================================================================

/**
 * Ensure the Delta9 directory exists
 */
export function ensureDelta9Dir(cwd: string): void {
  const dir = getDelta9Dir(cwd)
  if (!existsSync(dir)) {
    mkdirSync(dir, { recursive: true })
  }
}

/**
 * Ensure the checkpoints directory exists
 */
export function ensureCheckpointsDir(cwd: string): void {
  const dir = getCheckpointsDir(cwd)
  if (!existsSync(dir)) {
    mkdirSync(dir, { recursive: true })
  }
}

/**
 * Ensure the global config directory exists
 */
export function ensureGlobalConfigDir(): void {
  if (!existsSync(GLOBAL_CONFIG_DIR)) {
    mkdirSync(GLOBAL_CONFIG_DIR, { recursive: true })
  }
}

// =============================================================================
// Existence Checks
// =============================================================================

/**
 * Check if Delta9 is initialized in a project
 */
export function isDelta9Initialized(cwd: string): boolean {
  return existsSync(getDelta9Dir(cwd))
}

/**
 * Check if a mission exists
 */
export function missionExists(cwd: string): boolean {
  return existsSync(getMissionPath(cwd))
}

/**
 * Check if project config exists
 */
export function projectConfigExists(cwd: string): boolean {
  return existsSync(getProjectConfigPath(cwd))
}

/**
 * Check if global config exists
 */
export function globalConfigExists(): boolean {
  return existsSync(getGlobalConfigPath())
}

/**
 * Check if a checkpoint exists
 */
export function checkpointExists(cwd: string, checkpointName: string): boolean {
  return existsSync(getCheckpointPath(cwd, checkpointName))
}
