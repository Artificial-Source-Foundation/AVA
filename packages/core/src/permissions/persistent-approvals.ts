/**
 * Persistent Approval Storage
 *
 * Stores "always allow" approvals across sessions
 * Saves to .ava/permissions.json
 */

import { getPlatform } from '../platform.js'

// ============================================================================
// Types
// ============================================================================

/**
 * A persistent approval rule
 */
export interface PersistentApproval {
  /** Tool name */
  tool: string
  /** Optional path pattern (for file operations) */
  pathPattern?: string
  /** Optional command pattern (for bash) */
  commandPattern?: string
  /** When this approval was created */
  createdAt: number
  /** When this approval was last used */
  lastUsed?: number
  /** Number of times this approval has been used */
  useCount: number
}

/**
 * Stored approvals file format
 */
export interface PersistentApprovalsData {
  /** Version for migration */
  version: number
  /** Last modified timestamp */
  lastModified: number
  /** Tool-level approvals */
  toolApprovals: Record<string, PersistentApproval[]>
  /** Session-scoped approvals (cleared on session end) */
  sessionApprovals: Record<string, PersistentApproval[]>
}

// ============================================================================
// Constants
// ============================================================================

const CURRENT_VERSION = 1
const PERMISSIONS_FILE = '.ava/permissions.json'

// ============================================================================
// In-Memory Cache
// ============================================================================

let cachedData: PersistentApprovalsData | null = null
let currentWorkspaceRoot: string | null = null

// ============================================================================
// File Operations
// ============================================================================

/**
 * Get the full path to the permissions file
 */
function getPermissionsFilePath(workspaceRoot: string): string {
  return `${workspaceRoot}/${PERMISSIONS_FILE}`
}

/**
 * Create default approvals data
 */
function createDefaultData(): PersistentApprovalsData {
  return {
    version: CURRENT_VERSION,
    lastModified: Date.now(),
    toolApprovals: {},
    sessionApprovals: {},
  }
}

/**
 * Load approvals from file
 */
export async function loadPersistentApprovals(
  workspaceRoot: string
): Promise<PersistentApprovalsData> {
  // Return cached if same workspace
  if (cachedData && currentWorkspaceRoot === workspaceRoot) {
    return cachedData
  }

  const platform = getPlatform()
  const filePath = getPermissionsFilePath(workspaceRoot)

  try {
    const exists = await platform.fs.exists(filePath)
    if (!exists) {
      cachedData = createDefaultData()
      currentWorkspaceRoot = workspaceRoot
      return cachedData
    }

    const content = await platform.fs.readFile(filePath)
    const data = JSON.parse(content) as PersistentApprovalsData

    // Migration if needed
    if (data.version !== CURRENT_VERSION) {
      // Future: handle migrations
    }

    cachedData = data
    currentWorkspaceRoot = workspaceRoot
    return cachedData
  } catch (error) {
    console.error('Failed to load persistent approvals:', error)
    cachedData = createDefaultData()
    currentWorkspaceRoot = workspaceRoot
    return cachedData
  }
}

/**
 * Save approvals to file
 */
export async function savePersistentApprovals(
  workspaceRoot: string,
  data: PersistentApprovalsData
): Promise<void> {
  const platform = getPlatform()
  const filePath = getPermissionsFilePath(workspaceRoot)

  // Ensure directory exists
  const dirPath = `${workspaceRoot}/.ava`
  const dirExists = await platform.fs.exists(dirPath)
  if (!dirExists) {
    // Create the directory by creating and writing a placeholder
    // Most platforms will create parent directories automatically
  }

  data.lastModified = Date.now()
  const content = JSON.stringify(data, null, 2)

  await platform.fs.writeFile(filePath, content)
  cachedData = data
  currentWorkspaceRoot = workspaceRoot
}

// ============================================================================
// Approval Management
// ============================================================================

/**
 * Add a persistent "always allow" approval
 */
export async function addPersistentApproval(
  workspaceRoot: string,
  tool: string,
  options: {
    pathPattern?: string
    commandPattern?: string
    sessionOnly?: boolean
  } = {}
): Promise<PersistentApproval> {
  const data = await loadPersistentApprovals(workspaceRoot)
  const storage = options.sessionOnly ? data.sessionApprovals : data.toolApprovals

  // Create the approval
  const approval: PersistentApproval = {
    tool,
    pathPattern: options.pathPattern,
    commandPattern: options.commandPattern,
    createdAt: Date.now(),
    useCount: 0,
  }

  // Add to storage
  if (!storage[tool]) {
    storage[tool] = []
  }

  // Check for duplicates
  const existing = storage[tool].find(
    (a) => a.pathPattern === approval.pathPattern && a.commandPattern === approval.commandPattern
  )
  if (existing) {
    return existing
  }

  storage[tool].push(approval)

  // Save
  if (!options.sessionOnly) {
    await savePersistentApprovals(workspaceRoot, data)
  }

  return approval
}

/**
 * Remove a persistent approval
 */
export async function removePersistentApproval(
  workspaceRoot: string,
  tool: string,
  options: {
    pathPattern?: string
    commandPattern?: string
    sessionOnly?: boolean
  } = {}
): Promise<boolean> {
  const data = await loadPersistentApprovals(workspaceRoot)
  const storage = options.sessionOnly ? data.sessionApprovals : data.toolApprovals

  if (!storage[tool]) {
    return false
  }

  const originalLength = storage[tool].length
  storage[tool] = storage[tool].filter(
    (a) => a.pathPattern !== options.pathPattern || a.commandPattern !== options.commandPattern
  )

  if (storage[tool].length === originalLength) {
    return false
  }

  if (!options.sessionOnly) {
    await savePersistentApprovals(workspaceRoot, data)
  }

  return true
}

/**
 * Check if a tool operation has persistent approval
 */
export async function hasPersistentApproval(
  workspaceRoot: string,
  tool: string,
  context: {
    path?: string
    command?: string
  } = {}
): Promise<{ approved: boolean; approval?: PersistentApproval }> {
  const data = await loadPersistentApprovals(workspaceRoot)

  // Check both persistent and session approvals
  const allApprovals = [...(data.toolApprovals[tool] ?? []), ...(data.sessionApprovals[tool] ?? [])]

  for (const approval of allApprovals) {
    // If no patterns, approve all uses of this tool
    if (!approval.pathPattern && !approval.commandPattern) {
      approval.lastUsed = Date.now()
      approval.useCount++
      return { approved: true, approval }
    }

    // Check path pattern
    if (approval.pathPattern && context.path) {
      if (matchPattern(context.path, approval.pathPattern)) {
        approval.lastUsed = Date.now()
        approval.useCount++
        return { approved: true, approval }
      }
    }

    // Check command pattern
    if (approval.commandPattern && context.command) {
      if (matchPattern(context.command, approval.commandPattern)) {
        approval.lastUsed = Date.now()
        approval.useCount++
        return { approved: true, approval }
      }
    }
  }

  return { approved: false }
}

/**
 * Get all persistent approvals for a tool
 */
export async function getToolApprovals(
  workspaceRoot: string,
  tool: string
): Promise<PersistentApproval[]> {
  const data = await loadPersistentApprovals(workspaceRoot)
  return [...(data.toolApprovals[tool] ?? []), ...(data.sessionApprovals[tool] ?? [])]
}

/**
 * Get all persistent approvals
 */
export async function getAllApprovals(
  workspaceRoot: string
): Promise<{ tool: PersistentApproval[]; session: PersistentApproval[] }> {
  const data = await loadPersistentApprovals(workspaceRoot)

  const toolApprovals: PersistentApproval[] = []
  const sessionApprovals: PersistentApproval[] = []

  for (const approvals of Object.values(data.toolApprovals)) {
    toolApprovals.push(...approvals)
  }

  for (const approvals of Object.values(data.sessionApprovals)) {
    sessionApprovals.push(...approvals)
  }

  return { tool: toolApprovals, session: sessionApprovals }
}

/**
 * Clear all session-scoped approvals
 */
export async function clearSessionApprovals(workspaceRoot: string): Promise<void> {
  const data = await loadPersistentApprovals(workspaceRoot)
  data.sessionApprovals = {}
  // Don't save - session approvals are in-memory only
}

/**
 * Clear all persistent approvals
 */
export async function clearAllApprovals(workspaceRoot: string): Promise<void> {
  const data = createDefaultData()
  await savePersistentApprovals(workspaceRoot, data)
}

// ============================================================================
// Pattern Matching
// ============================================================================

/**
 * Match a value against a pattern (supports glob-like wildcards)
 */
function matchPattern(value: string, pattern: string): boolean {
  // Exact match
  if (value === pattern) {
    return true
  }

  // Convert glob to regex
  const regexStr = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    .replace(/\*\*/g, '<<<DOUBLE_STAR>>>')
    .replace(/\*/g, '[^/]*')
    .replace(/<<<DOUBLE_STAR>>>/g, '.*')
    .replace(/\?/g, '.')

  try {
    const regex = new RegExp(`^${regexStr}$`)
    return regex.test(value)
  } catch {
    return false
  }
}

// ============================================================================
// Integration with Auto-Approval
// ============================================================================

/**
 * Extended auto-approval check that includes persistent approvals
 */
export async function checkApprovalWithPersistence(
  workspaceRoot: string,
  tool: string,
  context: {
    path?: string
    command?: string
  }
): Promise<{ approved: boolean; reason: string; persistent?: boolean }> {
  // First check persistent approvals
  const persistent = await hasPersistentApproval(workspaceRoot, tool, context)
  if (persistent.approved) {
    return {
      approved: true,
      reason: `Persistent approval: ${persistent.approval?.tool}`,
      persistent: true,
    }
  }

  // Fall back to standard auto-approval (imported dynamically to avoid circular dep)
  const { shouldAutoApprove } = await import('./auto-approve.js')
  const result = shouldAutoApprove(tool, 'write', context)

  return {
    approved: result.approved,
    reason: result.reason,
    persistent: false,
  }
}
