import type { IShell } from '@ava/core-v2/platform'
import { NativeSandbox, type RuntimePlatform } from './native-sandbox.js'
import type { Sandbox, SandboxConfig, SandboxResult } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

class DockerSandbox implements Sandbox {
  name: 'docker' = 'docker'

  constructor(private readonly shell: IShell) {}

  async run(config: SandboxConfig, code: string): Promise<SandboxResult> {
    return runInSandbox(this.shell, config, code)
  }
}

class NoopSandbox implements Sandbox {
  name: 'noop' = 'noop'

  async run(_config: SandboxConfig): Promise<SandboxResult> {
    return {
      stdout: '',
      stderr: 'No sandbox runtime is available (native and docker unavailable)',
      exitCode: 1,
      durationMs: 0,
      timedOut: false,
    }
  }
}

/** Build a docker run command from config and code. */
export function buildDockerCommand(config: SandboxConfig, code: string): string {
  const flags: string[] = ['--rm']

  flags.push(`--memory=${config.maxMemoryMB}m`)
  if (!config.networkEnabled) {
    flags.push('--network=none')
  }

  for (const mount of config.mountPaths) {
    flags.push(`-v "${mount}:${mount}:ro"`)
  }

  const escapedCode = code.replace(/'/g, "'\\''")
  return `docker run ${flags.join(' ')} ${config.image} sh -c '${escapedCode}'`
}

/** Run code in a sandboxed Docker container. */
export async function runInSandbox(
  shell: IShell,
  config: SandboxConfig = DEFAULT_SANDBOX_CONFIG,
  code: string
): Promise<SandboxResult> {
  const command = buildDockerCommand(config, code)
  const startTime = Date.now()

  try {
    const timeoutPromise = new Promise<never>((_, reject) => {
      setTimeout(() => reject(new Error('TIMEOUT')), config.timeout)
    })

    const result = await Promise.race([shell.exec(command), timeoutPromise])

    return {
      stdout: result.stdout,
      stderr: result.stderr,
      exitCode: result.exitCode,
      durationMs: Date.now() - startTime,
      timedOut: false,
    }
  } catch (error) {
    const timedOut = error instanceof Error && error.message === 'TIMEOUT'
    return {
      stdout: '',
      stderr: timedOut ? 'Execution timed out' : String(error),
      exitCode: timedOut ? 124 : 1,
      durationMs: Date.now() - startTime,
      timedOut,
    }
  }
}

/** Check if Docker is available. */
export async function isDockerAvailable(shell: IShell): Promise<boolean> {
  try {
    const result = await shell.exec('docker --version')
    return result.exitCode === 0
  } catch {
    return false
  }
}

export async function createSandboxRuntime(
  shell: IShell,
  platform: RuntimePlatform = process.platform
): Promise<Sandbox> {
  const native = new NativeSandbox(shell, platform)
  if (await native.isAvailable()) {
    return native
  }

  if (await isDockerAvailable(shell)) {
    return new DockerSandbox(shell)
  }

  return new NoopSandbox()
}
