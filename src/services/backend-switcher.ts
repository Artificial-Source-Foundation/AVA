import { useSettings } from '../stores/settings'
import type { AgentBackend } from '../stores/settings/settings-types'
import { rustTools } from './rust-bridge'

export type BackendType = 'typescript' | 'rust'

export function resolveBackendType(agentBackend: AgentBackend): BackendType {
  return agentBackend === 'core' ? 'rust' : 'typescript'
}

export function getBackendType(): BackendType {
  const { settings } = useSettings()
  return resolveBackendType(settings().agentBackend)
}

export async function isRustBackendAvailable(): Promise<boolean> {
  try {
    await rustTools.list()
    return true
  } catch {
    return false
  }
}
