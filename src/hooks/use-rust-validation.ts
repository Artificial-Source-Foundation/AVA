import { createSignal } from 'solid-js'
import { rustValidation } from '../services/rust-bridge'
import type { RetryOutcome, RustValidationResult } from '../types/rust-ipc'

export function useRustValidation() {
  const [loading, setLoading] = createSignal(false)
  const [error, setError] = createSignal<string | null>(null)
  const [lastValidation, setLastValidation] = createSignal<RustValidationResult | null>(null)
  const [lastRetryOutcome, setLastRetryOutcome] = createSignal<RetryOutcome | null>(null)

  const validate = async (content: string): Promise<RustValidationResult> => {
    setLoading(true)
    setError(null)
    try {
      const result = await rustValidation.validateEdit(content)
      setLastValidation(result)
      return result
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
      throw error_
    } finally {
      setLoading(false)
    }
  }

  const validateWithRetry = async (
    content: string,
    fixes: string[],
    maxAttempts = 3
  ): Promise<RetryOutcome> => {
    setLoading(true)
    setError(null)
    try {
      const outcome = await rustValidation.validateWithRetry(content, fixes, maxAttempts)
      setLastRetryOutcome(outcome)
      setLastValidation(outcome.result)
      return outcome
    } catch (error_) {
      setError(error_ instanceof Error ? error_.message : String(error_))
      throw error_
    } finally {
      setLoading(false)
    }
  }

  return { loading, error, lastValidation, lastRetryOutcome, validate, validateWithRetry }
}
