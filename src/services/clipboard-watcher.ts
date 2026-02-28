/**
 * Clipboard Watcher Service
 *
 * Polls navigator.clipboard.readText() to detect clipboard changes.
 * Calls the provided callback when new text is detected.
 */

const POLL_INTERVAL_MS = 2000

const CODE_PATTERNS = [
  /\bfunction\s+\w+/,
  /\bconst\s+\w+\s*=/,
  /\blet\s+\w+\s*=/,
  /\bvar\s+\w+\s*=/,
  /\bimport\s+/,
  /\bexport\s+(default\s+)?/,
  /\bclass\s+\w+/,
  /\binterface\s+\w+/,
  /\btype\s+\w+\s*=/,
  /[{}]\s*$/m,
  /=>\s*[{(]/,
  /\bdef\s+\w+/,
  /\bpub\s+fn\s+/,
  /\bfn\s+\w+/,
]

/** Check if text likely contains code */
export function looksLikeCode(text: string): boolean {
  if (!text || text.length < 10) return false
  return CODE_PATTERNS.some((pattern) => pattern.test(text))
}

export interface ClipboardWatcher {
  start(): void
  stop(): void
}

/**
 * Create a clipboard watcher that polls for changes.
 * @param onClipboardChange Called with new text when clipboard content changes
 */
export function createClipboardWatcher(
  onClipboardChange: (text: string) => void
): ClipboardWatcher {
  let intervalId: ReturnType<typeof setInterval> | undefined
  let lastValue = ''

  const poll = async () => {
    try {
      const text = await navigator.clipboard.readText()
      if (text && text !== lastValue) {
        lastValue = text
        onClipboardChange(text)
      }
    } catch {
      // Permission denied or clipboard API unavailable — silently ignore
    }
  }

  return {
    start() {
      if (intervalId !== undefined) return
      // Capture current clipboard value as baseline (don't fire for existing content)
      navigator.clipboard
        .readText()
        .then((text) => {
          lastValue = text
        })
        .catch(() => {})
      intervalId = setInterval(poll, POLL_INTERVAL_MS)
    },
    stop() {
      if (intervalId !== undefined) {
        clearInterval(intervalId)
        intervalId = undefined
      }
    },
  }
}
