import { dispatchCompute } from '@ava/core-v2'

export interface RaceStrategy {
  name: string
  apply(content: string, oldText: string, newText: string): Promise<RaceResult | null>
}

export interface RaceResult {
  content: string
  strategy: string
  confidence: number
}

interface RaceFailure extends Error {
  strategy?: string
}

interface RustValidationResult {
  valid: boolean
}

function withTimeout<T>(promise: Promise<T>, ms: number, label: string): Promise<T> {
  let timeoutId: ReturnType<typeof setTimeout> | null = null
  return new Promise<T>((resolve, reject) => {
    timeoutId = setTimeout(() => {
      reject(new Error(`${label} timed out after ${ms}ms`))
    }, ms)

    promise
      .then((value) => resolve(value))
      .catch((error) => reject(error))
      .finally(() => {
        if (timeoutId) {
          clearTimeout(timeoutId)
        }
      })
  })
}

async function validateResult(content: string, newText: string): Promise<boolean> {
  if (!content.includes(newText)) {
    return false
  }

  try {
    const syntax = await dispatchCompute<RustValidationResult | null>(
      'validation_validate_edit',
      { content },
      async () => null
    )
    if (!syntax) {
      return true
    }
    return syntax.valid
  } catch {
    return true
  }
}

export async function raceEditStrategies(
  content: string,
  oldText: string,
  newText: string,
  strategies: RaceStrategy[],
  signal?: AbortSignal
): Promise<RaceResult> {
  const timeoutMs = 5_000
  if (strategies.length === 0) {
    throw new Error('No race strategies configured')
  }

  const attempted = strategies.map((strategy) => strategy.name)
  const controller = new AbortController()
  const onAbort = (): void => {
    controller.abort()
  }
  signal?.addEventListener('abort', onAbort)

  try {
    if (controller.signal.aborted || signal?.aborted) {
      throw new Error('Edit race aborted')
    }

    return await new Promise<RaceResult>((resolve, reject) => {
      let done = false
      let finishedCount = 0
      const failures: string[] = []

      const onFailure = (name: string, error: unknown): void => {
        if (done) {
          return
        }

        const message = error instanceof Error ? error.message : String(error)
        failures.push(`${name}: ${message}`)
        finishedCount += 1

        if (finishedCount === strategies.length) {
          done = true
          reject(
            new Error(
              `All race strategies failed: ${attempted.join(', ')}. ${failures.join(' | ')}`
            )
          )
        }
      }

      for (const strategy of strategies) {
        void withTimeout(strategy.apply(content, oldText, newText), timeoutMs, strategy.name)
          .then(async (result) => {
            if (done) {
              return
            }

            if (!result) {
              onFailure(strategy.name, new Error(`${strategy.name} produced no result`))
              return
            }

            const valid = await validateResult(result.content, newText)
            if (!valid) {
              onFailure(strategy.name, new Error(`${strategy.name} produced invalid syntax`))
              return
            }

            done = true
            controller.abort()
            resolve({
              content: result.content,
              strategy: result.strategy || strategy.name,
              confidence: result.confidence,
            })
          })
          .catch((error: unknown) => {
            const raceFailure = error as RaceFailure
            const name = raceFailure.strategy ?? strategy.name
            onFailure(name, raceFailure)
          })
      }
    })
  } finally {
    signal?.removeEventListener('abort', onAbort)
  }
}
