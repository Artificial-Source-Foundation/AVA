/**
 * Tool Call Utilities
 *
 * Pure functions for grouping, labeling, and categorizing tool calls.
 * No JSX — shared by ToolCallCard, ToolCallGroup, and ActiveToolIndicator.
 */

import type { ToolCall, ToolCallStatus } from '../../types'

// ============================================================================
// Grouping
// ============================================================================

export interface ToolCallGroupData {
  id: string
  toolName: string
  calls: ToolCall[]
  isActive: boolean
  isError: boolean
}

/**
 * Group consecutive same-name tool calls.
 * Preserves order — if types interleave, they form separate groups.
 */
export function groupToolCalls(toolCalls: ToolCall[]): ToolCallGroupData[] {
  const groups: ToolCallGroupData[] = []

  for (const call of toolCalls) {
    const last = groups[groups.length - 1]
    if (last && last.toolName === call.name) {
      last.calls.push(call)
      if (call.status === 'running' || call.status === 'pending') last.isActive = true
      if (call.status === 'error') last.isError = true
    } else {
      groups.push({
        id: call.id,
        toolName: call.name,
        calls: [call],
        isActive: call.status === 'running' || call.status === 'pending',
        isError: call.status === 'error',
      })
    }
  }

  return groups
}

// ============================================================================
// Verb pairs (active → past)
// ============================================================================

const VERB_PAIRS: Record<string, [string, string]> = {
  read_file: ['Reading', 'Read'],
  read: ['Reading', 'Read'],
  write_file: ['Writing', 'Wrote'],
  write: ['Writing', 'Wrote'],
  create_file: ['Creating', 'Created'],
  create: ['Creating', 'Created'],
  edit: ['Editing', 'Edited'],
  apply_patch: ['Patching', 'Patched'],
  multiedit: ['Editing', 'Edited'],
  delete_file: ['Deleting', 'Deleted'],
  delete: ['Deleting', 'Deleted'],
  bash: ['Running', 'Ran'],
  grep: ['Searching', 'Searched'],
  codesearch: ['Searching', 'Searched'],
  glob: ['Finding', 'Found'],
  ls: ['Listing', 'Listed'],
  websearch: ['Searching', 'Searched'],
  webfetch: ['Fetching', 'Fetched'],
}

/**
 * Group header label: "Reading 3 files..." (active) or "Read 3 files" (done).
 */
export function getGroupLabel(group: ToolCallGroupData): string {
  const [active, past] = VERB_PAIRS[group.toolName] ?? ['Processing', 'Processed']
  const count = group.calls.length
  const noun = count === 1 ? 'file' : 'files'

  // Some tools don't deal with files
  const isFileTool = [
    'read_file',
    'read',
    'write_file',
    'write',
    'create_file',
    'create',
    'edit',
    'apply_patch',
    'multiedit',
    'delete_file',
    'delete',
  ].includes(group.toolName)

  const subject = isFileTool ? `${count} ${noun}` : `${count} calls`

  if (group.isActive) return `${active} ${subject}...`
  return `${past} ${subject}`
}

// ============================================================================
// Error categorization
// ============================================================================

export type ToolErrorCategory =
  | 'not_found'
  | 'permission'
  | 'timeout'
  | 'execution'
  | 'denied'
  | 'unknown'

export function categorizeToolError(_name: string, error?: string): ToolErrorCategory {
  if (!error) return 'unknown'
  const lower = error.toLowerCase()
  if (lower.includes('not found') || lower.includes('enoent') || lower.includes('no such file'))
    return 'not_found'
  if (lower.includes('permission') || lower.includes('eacces') || lower.includes('eperm'))
    return 'permission'
  if (lower.includes('timeout') || lower.includes('timed out') || lower.includes('etimedout'))
    return 'timeout'
  if (lower.includes('denied') || lower.includes('blocked') || lower.includes('not allowed'))
    return 'denied'
  if (lower.includes('exit code') || lower.includes('error') || lower.includes('failed'))
    return 'execution'
  return 'unknown'
}

const ERROR_LABELS: Record<ToolErrorCategory, string> = {
  not_found: 'Not Found',
  permission: 'Permission Denied',
  timeout: 'Timed Out',
  execution: 'Execution Error',
  denied: 'Blocked',
  unknown: 'Error',
}

export function getErrorLabel(category: ToolErrorCategory): string {
  return ERROR_LABELS[category]
}

// ============================================================================
// Action summary (moved from ToolCallCard for reuse)
// ============================================================================

/** Build a human-readable summary: "reading src/App.tsx", "running pwd && ls -la" */
export function summarizeAction(name: string, args: Record<string, unknown>): string {
  const path = (args.path ?? args.filePath ?? args.file_path) as string | undefined
  const shortPath = path ? path.split('/').slice(-3).join('/') : ''

  switch (name) {
    case 'read_file':
    case 'read':
      return shortPath ? `reading ${shortPath}` : 'reading file'
    case 'write_file':
    case 'write':
      return shortPath ? `writing ${shortPath}` : 'writing file'
    case 'create_file':
    case 'create':
      return shortPath ? `creating ${shortPath}` : 'creating file'
    case 'edit':
    case 'apply_patch':
    case 'multiedit':
      return shortPath ? `editing ${shortPath}` : 'editing file'
    case 'delete_file':
    case 'delete':
      return shortPath ? `deleting ${shortPath}` : 'deleting file'
    case 'bash': {
      const cmd = String(args.command ?? '')
      const short = cmd.length > 80 ? `${cmd.slice(0, 77)}...` : cmd
      return short ? `running ${short}` : 'running command'
    }
    case 'grep': {
      const pattern = args.pattern ? `/${args.pattern}/` : ''
      return pattern ? `searching for ${pattern}` : 'searching files'
    }
    case 'glob':
      return args.pattern ? `finding ${args.pattern}` : 'finding files'
    case 'codesearch':
      return args.query ? 'searching codebase' : 'searching code'
    case 'ls':
      return shortPath ? `listing ${shortPath}` : 'listing directory'
    case 'websearch':
      return args.query ? `searching web for "${args.query}"` : 'searching the web'
    case 'webfetch':
      return args.url ? `fetching ${args.url}` : 'fetching web page'
    default:
      return shortPath ? `${name} ${shortPath}` : name
  }
}

// ============================================================================
// Duration / elapsed formatting
// ============================================================================

/** Format a completed duration in ms */
export function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}

/** Format live elapsed from a start timestamp */
export function formatElapsed(startedAt: number): string {
  const elapsed = Date.now() - startedAt
  if (elapsed < 1000) return '<1s'
  return `${(elapsed / 1000).toFixed(1)}s`
}

// ============================================================================
// Language detection for syntax highlighting
// ============================================================================

const EXT_TO_LANG: Record<string, string> = {
  ts: 'typescript',
  tsx: 'typescript',
  js: 'javascript',
  jsx: 'javascript',
  py: 'python',
  rs: 'rust',
  go: 'go',
  sh: 'bash',
  bash: 'bash',
  zsh: 'bash',
  json: 'json',
  css: 'css',
  html: 'html',
  htm: 'html',
  xml: 'html',
  sql: 'sql',
  yaml: 'yaml',
  yml: 'yaml',
  toml: 'yaml',
}

const TOOL_TO_LANG: Record<string, string> = {
  bash: 'bash',
  grep: 'bash',
  ls: 'bash',
}

/** Detect language for syntax highlighting from tool name and file path */
export function detectLanguage(toolName: string, filePath?: string): string | undefined {
  if (filePath) {
    const ext = filePath.split('.').pop()?.toLowerCase()
    if (ext && EXT_TO_LANG[ext]) return EXT_TO_LANG[ext]
  }
  return TOOL_TO_LANG[toolName]
}

/** Get aggregate status for a group */
export function getGroupStatus(group: ToolCallGroupData): ToolCallStatus {
  if (group.isActive) return 'running'
  if (group.isError) return 'error'
  const allSuccess = group.calls.every((c) => c.status === 'success')
  return allSuccess ? 'success' : 'pending'
}

/** Get total duration for a group of completed calls */
export function getGroupDuration(group: ToolCallGroupData): number | null {
  if (group.isActive) return null
  const first = group.calls[0]
  const last = group.calls[group.calls.length - 1]
  if (!first || !last?.completedAt) return null
  return last.completedAt - first.startedAt
}
