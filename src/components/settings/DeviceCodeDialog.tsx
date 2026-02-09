/**
 * Device Code Dialog
 *
 * Modal for GitHub Copilot device code authentication flow.
 * Shows the user code, opens browser, polls for authorization.
 */

import { open } from '@tauri-apps/plugin-shell'
import { Check, Clipboard, ExternalLink, Loader2, X } from 'lucide-solid'
import { type Component, createSignal, onCleanup, onMount, Show } from 'solid-js'
import type { DeviceCodeResponse } from '../../services/auth/oauth'
import { pollDeviceCodeAuth, storeOAuthCredentials } from '../../services/auth/oauth'
import type { LLMProvider } from '../../types/llm'

interface DeviceCodeDialogProps {
  provider: LLMProvider
  deviceCode: DeviceCodeResponse
  onClose: () => void
  onSuccess: (accessToken: string) => void
}

export const DeviceCodeDialog: Component<DeviceCodeDialogProps> = (props) => {
  const [copied, setCopied] = createSignal(false)
  const [status, setStatus] = createSignal<'waiting' | 'success' | 'error'>('waiting')
  const [errorMsg, setErrorMsg] = createSignal('')
  const abortController = new AbortController()

  const copyCode = async () => {
    await navigator.clipboard.writeText(props.deviceCode.userCode)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  const openBrowser = () => {
    open(props.deviceCode.verificationUri)
  }

  onMount(() => {
    // Start polling for authorization
    pollDeviceCodeAuth(
      props.provider,
      props.deviceCode.deviceCode,
      props.deviceCode.interval,
      abortController.signal
    )
      .then((tokens) => {
        if (tokens) {
          storeOAuthCredentials(props.provider, tokens)
          setStatus('success')
          setTimeout(() => props.onSuccess(tokens.accessToken), 1000)
        } else {
          setStatus('error')
          setErrorMsg('Authorization expired or was denied')
        }
      })
      .catch((err) => {
        if (!abortController.signal.aborted) {
          setStatus('error')
          setErrorMsg(err instanceof Error ? err.message : 'Authorization failed')
        }
      })
  })

  onCleanup(() => {
    abortController.abort()
  })

  return (
    <div class="fixed inset-0 z-[var(--z-modal)] flex items-center justify-center bg-black/50">
      <div
        class="
          w-[380px] bg-[var(--surface-overlay)]
          border border-[var(--border-default)]
          rounded-[var(--radius-xl)]
          shadow-xl p-6
          animate-slide-up
        "
      >
        {/* Header */}
        <div class="flex items-center justify-between mb-4">
          <h3 class="text-[var(--text-base)] font-medium text-[var(--text-primary)]">
            Sign in with GitHub Copilot
          </h3>
          <button
            type="button"
            onClick={props.onClose}
            class="p-1 text-[var(--text-muted)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        {/* Instructions */}
        <p class="text-[var(--text-sm)] text-[var(--text-secondary)] mb-4">
          Go to the URL below and enter the code to authorize Estela.
        </p>

        {/* User Code */}
        <div class="flex items-center gap-2 mb-4">
          <div
            class="
              flex-1 px-4 py-3 text-center
              bg-[var(--surface-sunken)]
              border border-[var(--border-default)]
              rounded-[var(--radius-lg)]
              font-[var(--font-mono)] text-lg tracking-[0.3em]
              text-[var(--text-primary)] font-bold select-all
            "
          >
            {props.deviceCode.userCode}
          </div>
          <button
            type="button"
            onClick={copyCode}
            class="
              p-2.5 rounded-[var(--radius-lg)]
              bg-[var(--surface-sunken)]
              border border-[var(--border-default)]
              text-[var(--text-muted)] hover:text-[var(--text-primary)]
              transition-colors
            "
            title="Copy code"
          >
            <Show when={copied()} fallback={<Clipboard class="w-4 h-4" />}>
              <Check class="w-4 h-4 text-[var(--success)]" />
            </Show>
          </button>
        </div>

        {/* Open Browser Button */}
        <button
          type="button"
          onClick={openBrowser}
          class="
            w-full flex items-center justify-center gap-2
            px-4 py-2.5
            bg-[var(--accent)] text-white
            rounded-[var(--radius-lg)]
            font-medium text-[var(--text-sm)]
            hover:opacity-90 transition-opacity
          "
        >
          <ExternalLink class="w-4 h-4" />
          Open {props.deviceCode.verificationUri}
        </button>

        {/* Status */}
        <div class="mt-4 flex items-center justify-center gap-2">
          <Show when={status() === 'waiting'}>
            <Loader2 class="w-4 h-4 text-[var(--accent)] animate-spin" />
            <span class="text-[var(--text-xs)] text-[var(--text-muted)]">
              Waiting for authorization...
            </span>
          </Show>
          <Show when={status() === 'success'}>
            <Check class="w-4 h-4 text-[var(--success)]" />
            <span class="text-[var(--text-xs)] text-[var(--success)]">Authorized successfully</span>
          </Show>
          <Show when={status() === 'error'}>
            <X class="w-4 h-4 text-[var(--error)]" />
            <span class="text-[var(--text-xs)] text-[var(--error)]">{errorMsg()}</span>
          </Show>
        </div>
      </div>
    </div>
  )
}
