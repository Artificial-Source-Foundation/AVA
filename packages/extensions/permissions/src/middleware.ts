/**
 * Permission middleware — hooks into the tool execution pipeline.
 *
 * Implements safety checks as a tool middleware:
 * - Blocks dangerous patterns (rm -rf /, .git writes)
 * - Auto-approves reads when configured
 * - Per-tool rules with glob matching (first match wins)
 * - Smart-approve for safe bash commands and trusted paths
 * - Always-approved list from user confirmations
 * - Emits permission:request events for user confirmation
 */

import type { MessageBus } from '@ava/core-v2/bus'
import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { isToolAutoApproved, type PermissionMode } from './modes.js'
import {
  classifyRisk,
  DEFAULT_SETTINGS,
  type PermissionRequest,
  type PermissionResponse,
  type PermissionSettings,
  SAFE_BASH_PATTERNS,
  type ToolPermissionRule,
} from './types.js'

let settings: PermissionSettings = { ...DEFAULT_SETTINGS }

export function updateSettings(partial: Partial<PermissionSettings>): void {
  settings = { ...settings, ...partial }
}

export function getSettings(): PermissionSettings {
  return { ...settings }
}

export function resetSettings(): void {
  settings = { ...DEFAULT_SETTINGS }
}

// ─── Glob Matching ─────────────────────────────────────────────────────────

/** Simple glob matching: supports *, **, and ? */
export function matchesGlob(value: string, pattern: string): boolean {
  const regex = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&') // escape regex special chars
    .replace(/\*\*/g, '§§') // placeholder for **
    .replace(/\*/g, '[^/]*') // * matches anything except /
    .replace(/§§/g, '.*') // ** matches anything
    .replace(/\?/g, '.') // ? matches single char
  return new RegExp(`^${regex}$`).test(value)
}

export function matchesAnyGlob(value: string, patterns: string[]): boolean {
  return patterns.some((p) => matchesGlob(value, p))
}

// ─── Per-Tool Rule Evaluation ──────────────────────────────────────────────

/** Evaluate tool rules — first matching rule wins. */
export function evaluateToolRules(
  toolName: string,
  path: string | undefined,
  rules: ToolPermissionRule[]
): ToolPermissionRule | undefined {
  for (const rule of rules) {
    if (!matchesGlob(toolName, rule.tool)) continue
    // If rule has path restrictions, check them
    if (rule.paths && rule.paths.length > 0) {
      if (!path || !matchesAnyGlob(path, rule.paths)) continue
    }
    return rule
  }
  return undefined
}

// ─── Smart-Approve Helpers ─────────────────────────────────────────────────

export function isSafeBashCommand(command: string): boolean {
  const trimmed = command.trim()
  return SAFE_BASH_PATTERNS.some((pattern) => pattern.test(trimmed))
}

export function isInTrustedPath(filePath: string, trusted: string[]): boolean {
  return matchesAnyGlob(filePath, trusted)
}

// ─── Path Checks ────────────────────────────────────────────────────────────

function isGitPath(path: string): boolean {
  return (
    path.includes('/.git/') ||
    path.includes('/.git') ||
    path.endsWith('/.git') ||
    path.startsWith('.git/') ||
    path === '.git'
  )
}

function isNodeModulesWrite(toolName: string, args: Record<string, unknown>): boolean {
  if (toolName === 'read_file' || toolName === 'glob' || toolName === 'grep') return false
  const path = (args.path ?? args.filePath ?? '') as string
  return path.includes('/node_modules/') || path.startsWith('node_modules/')
}

function isEnvFile(args: Record<string, unknown>): boolean {
  const path = (args.path ?? args.filePath ?? '') as string
  const name = path.split('/').pop() ?? ''
  return name.startsWith('.env')
}

function isDangerousCommand(args: Record<string, unknown>): boolean {
  const cmd = (args.command ?? '') as string
  return /rm\s+-rf\s+[/~]/.test(cmd) || /rm\s+-rf\s+\*/.test(cmd)
}

function isSudoCommand(args: Record<string, unknown>): boolean {
  const cmd = (args.command ?? '') as string
  return cmd.trimStart().startsWith('sudo ')
}

function isBlockedByPattern(args: Record<string, unknown>): boolean {
  if (settings.blockedPatterns.length === 0) return false
  const path = (args.path ?? args.filePath ?? '') as string
  return settings.blockedPatterns.some(
    (p) => matchesGlob(path, `**/${p}/**`) || matchesGlob(path, `**/${p}`) || path.includes(p)
  )
}

// ─── Middleware ──────────────────────────────────────────────────────────────

export function createPermissionMiddleware(bus?: MessageBus): ToolMiddleware {
  return {
    name: 'ava-permissions',
    priority: 0, // Runs first — before all other middleware

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const { toolName, args } = ctx
      const path = (args.path ?? args.filePath ?? '') as string

      // 1. Always block: .git writes
      if (
        isGitPath(path) &&
        toolName !== 'read_file' &&
        toolName !== 'glob' &&
        toolName !== 'grep'
      ) {
        return { blocked: true, reason: 'Cannot modify .git directory' }
      }

      // 2. Always block: node_modules writes
      if (isNodeModulesWrite(toolName, args)) {
        return { blocked: true, reason: 'Cannot write to node_modules' }
      }

      // 3. Always block: destructive rm -rf
      if (toolName === 'bash' && isDangerousCommand(args)) {
        return { blocked: true, reason: 'Destructive rm -rf commands are blocked' }
      }

      // 4. Blocked patterns (upgraded to glob matching)
      if (isBlockedByPattern(args)) {
        return { blocked: true, reason: 'Path matches a blocked pattern' }
      }

      // 5. Permission mode check (when set, takes priority over individual booleans)
      if (settings.permissionMode) {
        const mode = settings.permissionMode as PermissionMode
        if (mode === 'suggest') {
          // Suggest mode blocks all tool execution
          return { blocked: true, reason: 'Suggest mode — tool execution disabled' }
        }
        if (isToolAutoApproved(toolName, mode)) return undefined
        // Not auto-approved by mode — fall through to bus/fallback approval
      } else {
        // 6. YOLO mode (legacy boolean): approve everything not blocked above
        if (settings.yolo) return undefined
      }

      // 7. Always-approved list check
      if (settings.alwaysApproved.length > 0 && settings.alwaysApproved.includes(toolName)) {
        return undefined
      }

      // 8. Per-tool rules (first match wins)
      if (settings.toolRules.length > 0) {
        const rule = evaluateToolRules(toolName, path || undefined, settings.toolRules)
        if (rule) {
          if (rule.action === 'allow') return undefined
          if (rule.action === 'deny') {
            return { blocked: true, reason: rule.reason ?? `Denied by tool rule for ${rule.tool}` }
          }
          // 'ask' falls through to bus approval below
        }
      }

      const risk = classifyRisk(toolName, args)

      // Skip legacy boolean checks when permission mode is set
      if (!settings.permissionMode) {
        // 9. Auto-approve reads
        if (risk === 'low' && settings.autoApproveReads) return undefined

        // 10. Smart-approve: safe bash commands + trusted paths
        if (settings.smartApprove) {
          if (toolName === 'bash') {
            const command = (args.command ?? '') as string
            if (isSafeBashCommand(command)) return undefined
          }
          if (
            path &&
            settings.trustedPaths.length > 0 &&
            isInTrustedPath(path, settings.trustedPaths)
          ) {
            return undefined
          }
        }

        // 11. Auto-approve writes if configured
        if (risk === 'medium' && settings.autoApproveWrites) return undefined

        // 12. Auto-approve commands if configured
        if (toolName === 'bash' && settings.autoApproveCommands) {
          if (!isSudoCommand(args)) return undefined
        }
      }

      // 13. Bus-based approval with alwaysApprove support
      if (bus?.hasSubscribers('permission:request')) {
        const response = await bus.request<PermissionRequest, PermissionResponse>(
          { type: 'permission:request', toolName, args, risk },
          'permission:response',
          120_000
        )
        if (!response.approved) {
          return { blocked: true, reason: response.reason ?? 'Denied by user' }
        }
        // Handle "always approve" response
        if (response.alwaysApprove && !settings.alwaysApproved.includes(toolName)) {
          settings = {
            ...settings,
            alwaysApproved: [...settings.alwaysApproved, toolName],
          }
        }
        return undefined
      }

      // 14. No approval handler — apply fallback blocking rules

      // Warn on .env files
      if (isEnvFile(args)) {
        return { blocked: true, reason: 'Accessing .env files requires confirmation' }
      }

      // Warn on sudo
      if (toolName === 'bash' && isSudoCommand(args)) {
        return { blocked: true, reason: 'sudo requires confirmation' }
      }

      return undefined
    },
  }
}
