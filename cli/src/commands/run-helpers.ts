import type { AgentConfig, LLMProvider } from '@ava/core-v2'
import type { RunOptions } from './run-options.js'

const CLI_EXCLUDED = new Set([
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
  'lsp_references',
  'lsp_document_symbols',
  'lsp_workspace_symbols',
  'lsp_code_actions',
  'lsp_rename',
  'lsp_completions',
  'task',
  'sandbox_run',
  'pty',
  'batch',
  'multiedit',
  'apply_patch',
  'recall',
])

export function applyCliToolFilter(config: Partial<AgentConfig>, allToolNames: string[]): void {
  config.allowedTools = allToolNames.filter(
    (name) => !name.startsWith('delegate_') && !CLI_EXCLUDED.has(name)
  )
}

export function buildLegacyArgs(options: RunOptions): string[] {
  const legacyArgs = ['run', options.goal]
  if (options.provider) legacyArgs.push('--provider', options.provider)
  if (options.model) legacyArgs.push('--model', options.model)
  legacyArgs.push('--max-turns', String(options.maxTurns))
  legacyArgs.push('--timeout', String(options.maxTimeMinutes))
  legacyArgs.push('--cwd', options.cwd)
  if (options.json) legacyArgs.push('--json')
  if (options.verbose) legacyArgs.push('--verbose')
  return legacyArgs
}

export function applyMockProviderDefaults(
  options: RunOptions,
  register: (provider: LLMProvider, factory: () => unknown) => void,
  createMockClient: () => unknown
): RunOptions {
  register('mock' as LLMProvider, createMockClient)
  register('anthropic', createMockClient)
  if (!options.provider) {
    return { ...options, provider: 'mock' as LLMProvider }
  }
  return options
}

/** Try importing from source (tsx), fall back to compiled dist. */
export async function importWithFallback(srcPath: string, distPath: string): Promise<unknown> {
  try {
    return await import(srcPath)
  } catch {
    return await import(distPath)
  }
}
