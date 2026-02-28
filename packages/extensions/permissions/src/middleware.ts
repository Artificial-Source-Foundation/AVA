/**
 * Permission middleware — hooks into the tool execution pipeline.
 *
 * Implements safety checks as a tool middleware:
 * - Blocks dangerous patterns (rm -rf /, .git writes)
 * - Auto-approves reads when configured
 * - Emits permission:request events for user confirmation
 */

import type { MessageBus } from '@ava/core-v2/bus'
import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import {
  classifyRisk,
  DEFAULT_SETTINGS,
  type PermissionRequest,
  type PermissionResponse,
  type PermissionSettings,
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

// ─── Path Checks ────────────────────────────────────────────────────────────

function isGitPath(path: string): boolean {
  return path.includes('/.git/') || path.includes('/.git') || path.endsWith('/.git')
}

function isNodeModulesWrite(toolName: string, args: Record<string, unknown>): boolean {
  if (toolName === 'read_file' || toolName === 'glob' || toolName === 'grep') return false
  const path = (args.path ?? args.filePath ?? '') as string
  return path.includes('/node_modules/')
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
  return settings.blockedPatterns.some((p) => path.includes(p))
}

// ─── Middleware ──────────────────────────────────────────────────────────────

export function createPermissionMiddleware(bus?: MessageBus): ToolMiddleware {
  return {
    name: 'ava-permissions',
    priority: 0, // Runs first — before all other middleware

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const { toolName, args } = ctx
      const path = (args.path ?? args.filePath ?? '') as string

      // Always block: .git writes
      if (
        isGitPath(path) &&
        toolName !== 'read_file' &&
        toolName !== 'glob' &&
        toolName !== 'grep'
      ) {
        return { blocked: true, reason: 'Cannot modify .git directory' }
      }

      // Always block: node_modules writes
      if (isNodeModulesWrite(toolName, args)) {
        return { blocked: true, reason: 'Cannot write to node_modules' }
      }

      // Always block: destructive rm -rf
      if (toolName === 'bash' && isDangerousCommand(args)) {
        return { blocked: true, reason: 'Destructive rm -rf commands are blocked' }
      }

      // Blocked patterns
      if (isBlockedByPattern(args)) {
        return { blocked: true, reason: 'Path matches a blocked pattern' }
      }

      // YOLO mode: approve everything not blocked above
      if (settings.yolo) return undefined

      const risk = classifyRisk(toolName, args)

      // Auto-approve reads
      if (risk === 'low' && settings.autoApproveReads) return undefined

      // Auto-approve writes if configured
      if (risk === 'medium' && settings.autoApproveWrites) return undefined

      // Auto-approve commands if configured
      if (toolName === 'bash' && settings.autoApproveCommands) {
        if (!isSudoCommand(args)) return undefined
      }

      // For tools needing approval: use bus if available
      if (bus?.hasSubscribers('permission:request')) {
        const response = await bus.request<PermissionRequest, PermissionResponse>(
          { type: 'permission:request', toolName, args, risk },
          'permission:response',
          120_000
        )
        if (!response.approved) {
          return { blocked: true, reason: response.reason ?? 'Denied by user' }
        }
        return undefined
      }

      // No approval handler available — apply fallback blocking rules

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
