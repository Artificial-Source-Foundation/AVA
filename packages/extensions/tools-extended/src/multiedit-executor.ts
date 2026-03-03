export interface FileEdit {
  oldString: string
  newString: string
}

export interface MultiEditJob {
  filePath: string
  edits: FileEdit[]
}

export interface MultiEditJobResult {
  filePath: string
  success: boolean
  appliedEdits: number
  error?: string
}

export interface MultiEditExecutionResult {
  success: boolean
  succeeded: number
  failed: number
  results: MultiEditJobResult[]
}

export type MultiEditApplyJob = (job: MultiEditJob) => Promise<MultiEditJobResult>

class Semaphore {
  private readonly waiters: Array<() => void> = []
  private active = 0

  constructor(private readonly limit: number) {}

  async acquire(): Promise<void> {
    if (this.active < this.limit) {
      this.active += 1
      return
    }
    await new Promise<void>((resolve) => this.waiters.push(resolve))
    this.active += 1
  }

  release(): void {
    this.active -= 1
    const waiter = this.waiters.shift()
    if (waiter) waiter()
  }
}

function clampConcurrency(value: number | undefined): number {
  if (value === undefined) return 4
  return Math.max(1, Math.min(16, Math.floor(value)))
}

export async function executeMultiEditJobs(
  jobs: MultiEditJob[],
  applyJob: MultiEditApplyJob,
  concurrency?: number
): Promise<MultiEditExecutionResult> {
  const semaphore = new Semaphore(clampConcurrency(concurrency))

  const run = jobs.map(async (job, index) => {
    await semaphore.acquire()
    try {
      return { index, result: await applyJob(job) }
    } catch (error) {
      return {
        index,
        result: {
          filePath: job.filePath,
          success: false,
          appliedEdits: 0,
          error: String(error),
        } satisfies MultiEditJobResult,
      }
    } finally {
      semaphore.release()
    }
  })

  const settled = await Promise.allSettled(run)
  const ordered: MultiEditJobResult[] = jobs.map((job) => ({
    filePath: job.filePath,
    success: false,
    appliedEdits: 0,
    error: 'Job did not run',
  }))

  for (const item of settled) {
    if (item.status !== 'fulfilled') continue
    ordered[item.value.index] = item.value.result
  }

  const succeeded = ordered.filter((r) => r.success).length
  return {
    success: succeeded === ordered.length,
    succeeded,
    failed: ordered.length - succeeded,
    results: ordered,
  }
}
