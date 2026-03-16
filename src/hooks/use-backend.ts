import { useRustAgent } from './use-rust-agent'

export type BackendType = 'rust'

export function useBackend() {
  const agent = useRustAgent()
  return {
    ...agent,
    backendType: () => 'rust' as BackendType,
  }
}
