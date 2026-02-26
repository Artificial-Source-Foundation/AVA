/**
 * Hook runner — manages registered hooks and executes them.
 */

import type { HookConfig, HookContext, HookResult, HookType, RegisteredHook } from './types.js'
import { DEFAULT_HOOK_CONFIG } from './types.js'

let hookConfig: HookConfig = { ...DEFAULT_HOOK_CONFIG }
const hooks = new Map<HookType, RegisteredHook[]>()

export function setHookConfig(partial: Partial<HookConfig>): void {
  hookConfig = { ...hookConfig, ...partial }
}

export function getHookConfig(): HookConfig {
  return { ...hookConfig }
}

export function registerHook(hook: RegisteredHook): () => void {
  let list = hooks.get(hook.type)
  if (!list) {
    list = []
    hooks.set(hook.type, list)
  }
  list.push(hook)

  return () => {
    const current = hooks.get(hook.type)
    if (current) {
      const idx = current.indexOf(hook)
      if (idx !== -1) current.splice(idx, 1)
    }
  }
}

export function hasHooks(type: HookType): boolean {
  const list = hooks.get(type)
  return !!list && list.length > 0
}

export function getRegisteredHooks(): ReadonlyMap<HookType, readonly RegisteredHook[]> {
  return hooks
}

export function resetHooks(): void {
  hooks.clear()
  hookConfig = { ...DEFAULT_HOOK_CONFIG }
}

export function mergeHookResults(results: HookResult[]): HookResult {
  const merged: HookResult = {}

  for (const r of results) {
    if (r.cancel) merged.cancel = true
    if (r.errorMessage) merged.errorMessage = r.errorMessage
    if (r.contextModification) {
      merged.contextModification = merged.contextModification
        ? `${merged.contextModification}\n${r.contextModification}`
        : r.contextModification
    }
  }

  return merged
}

export async function runHooks(type: HookType, context: HookContext): Promise<HookResult> {
  const list = hooks.get(type)
  if (!list || list.length === 0) return {}

  const results: HookResult[] = []

  for (const hook of list) {
    try {
      const result = await Promise.race([
        hook.handler(context),
        new Promise<HookResult>((_, reject) =>
          setTimeout(() => reject(new Error(`Hook "${hook.name}" timed out`)), hookConfig.timeout)
        ),
      ])

      results.push(result)

      // Stop on cancel for PreToolUse
      if (type === 'PreToolUse' && result.cancel) break
    } catch (err) {
      if (!hookConfig.continueOnError) throw err
    }
  }

  return mergeHookResults(results)
}
