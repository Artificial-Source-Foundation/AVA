/**
 * Process registry — singleton Map tracking background shell processes.
 */

import type { ChildProcess } from 'node:child_process'

export interface BackgroundProcess {
  pid: number
  command: string
  stdout: string[]
  stderr: string[]
  startTime: number
  exitCode: number | null
  process: ChildProcess
}

/** Maximum lines retained per stream (stdout/stderr). */
export const MAX_BUFFER_LINES = 1000

const processes = new Map<number, BackgroundProcess>()

export function registerProcess(proc: BackgroundProcess): void {
  processes.set(proc.pid, proc)
}

export function getProcess(pid: number): BackgroundProcess | undefined {
  return processes.get(pid)
}

export function removeProcess(pid: number): void {
  processes.delete(pid)
}

export function listProcesses(): BackgroundProcess[] {
  return [...processes.values()]
}

export function cleanupAll(): void {
  for (const proc of processes.values()) {
    try {
      proc.process.kill()
    } catch {
      // Process may already be dead
    }
  }
  processes.clear()
}

/** Reset registry (for testing only). */
export function _resetRegistry(): void {
  processes.clear()
}
