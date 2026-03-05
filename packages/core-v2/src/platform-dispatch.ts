export function isRunningInTauri(): boolean {
  const maybeWindow = (globalThis as { window?: unknown }).window
  return typeof maybeWindow === 'object' && maybeWindow !== null && '__TAURI__' in maybeWindow
}

export async function dispatchCompute<T>(
  rustCommand: string,
  rustArgs: Record<string, unknown>,
  tsFallback: () => Promise<T>
): Promise<T> {
  if (isRunningInTauri()) {
    const { invoke } = await import('@tauri-apps/api/core')
    return invoke<T>(rustCommand, { input: rustArgs })
  }

  return tsFallback()
}
