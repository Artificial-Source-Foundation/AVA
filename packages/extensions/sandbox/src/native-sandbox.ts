import type { IShell } from '@ava/core-v2/platform'
import type { Sandbox, SandboxConfig, SandboxResult } from './types.js'

export type RuntimePlatform = 'linux' | 'darwin' | 'win32' | (string & {})

function escapeShell(value: string): string {
  return value.replace(/'/g, "'\\''")
}

export function buildNativeCommand(
  platform: RuntimePlatform,
  config: SandboxConfig,
  code: string
): string {
  const escapedCode = escapeShell(code)

  if (platform === 'linux') {
    const networkFlag = config.networkEnabled ? '' : '--unshare-net'
    const mountFlags = config.mountPaths.map((mount) => `--ro-bind "${mount}" "${mount}"`).join(' ')
    return [
      'bwrap',
      '--die-with-parent',
      '--unshare-all',
      networkFlag,
      '--proc /proc',
      '--dev /dev',
      '--tmpfs /tmp',
      mountFlags,
      // landlock is best-effort and relies on kernel support plus bwrap build options
      "sh -c 'export AVA_LANDLOCK=1;",
      `${escapedCode}'`,
    ]
      .filter(Boolean)
      .join(' ')
  }

  if (platform === 'darwin') {
    const profile =
      '(version 1) (deny default) (allow file-read*) (allow process*) (allow sysctl-read)'
    return `sandbox-exec -p '${profile}' sh -c '${escapedCode}'`
  }

  return `sh -c '${escapedCode}'`
}

export class NativeSandbox implements Sandbox {
  name: 'native' = 'native'

  constructor(
    private readonly shell: IShell,
    private readonly platform: RuntimePlatform = process.platform
  ) {}

  async isAvailable(): Promise<boolean> {
    try {
      if (this.platform === 'linux') {
        const result = await this.shell.exec('bwrap --version')
        return result.exitCode === 0
      }

      if (this.platform === 'darwin') {
        const result = await this.shell.exec('sandbox-exec -h')
        return result.exitCode === 0
      }
    } catch {
      return false
    }

    return false
  }

  async run(config: SandboxConfig, code: string): Promise<SandboxResult> {
    const command = buildNativeCommand(this.platform, config, code)
    const startTime = Date.now()

    try {
      const timeoutPromise = new Promise<never>((_, reject) => {
        setTimeout(() => reject(new Error('TIMEOUT')), config.timeout)
      })
      const result = await Promise.race([this.shell.exec(command), timeoutPromise])

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
}
