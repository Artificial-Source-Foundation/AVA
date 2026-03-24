/**
 * Voice Button
 *
 * Self-contained voice dictation section for the toolbar strip.
 * Manages recording state, audio analyser waveform, and device picker.
 */

import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import {
  type AudioAnalyserHandle,
  createAudioAnalyser,
  createDictation,
  getAudioDevices,
  isDictationSupported,
} from '../../../services/voice-dictation'
import { useSettings } from '../../../stores/settings'
import { MicButton } from './mic-button'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface VoiceButtonProps {
  /** Callback when transcribed text arrives */
  onTranscript: (text: string) => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const VoiceButton: Component<VoiceButtonProps> = (props) => {
  const { settings, updateSettings } = useSettings()

  // Voice dictation
  const [isRecording, setIsRecording] = createSignal(false)
  const dictationSupported = createMemo(() => isDictationSupported())
  const dictation = createDictation({
    onTranscript: (text) => props.onTranscript(text),
    onStateChange: setIsRecording,
    onError: (err) => console.warn('Voice dictation:', err),
  })

  // Audio analyser for waveform visualization
  const [waveformBars, setWaveformBars] = createSignal<number[]>([0, 0, 0, 0, 0, 0, 0, 0])
  let analyserHandle: AudioAnalyserHandle | undefined
  let waveformRaf: number | undefined

  // Audio device list
  const [audioDevices, setAudioDevices] = createSignal<MediaDeviceInfo[]>([])

  // Load audio devices on mount when dictation is supported
  if (dictationSupported()) {
    getAudioDevices()
      .then(setAudioDevices)
      .catch(() => {})
  }

  // Start/stop analyser when recording state changes
  createEffect(
    on(isRecording, (rec) => {
      if (rec) {
        const deviceId = settings().behavior.voiceDeviceId || undefined
        createAudioAnalyser(deviceId)
          .then((handle) => {
            analyserHandle = handle
            const tick = () => {
              const data = handle.getFrequencyData()
              const bars: number[] = []
              const step = Math.floor(data.length / 8)
              for (let i = 0; i < 8; i++) {
                bars.push(Math.round((data[i * step] / 255) * 16))
              }
              setWaveformBars(bars)
              waveformRaf = requestAnimationFrame(tick)
            }
            waveformRaf = requestAnimationFrame(tick)
          })
          .catch(() => {})
      } else {
        if (waveformRaf !== undefined) cancelAnimationFrame(waveformRaf)
        analyserHandle?.stop()
        analyserHandle = undefined
        setWaveformBars([0, 0, 0, 0, 0, 0, 0, 0])
      }
    })
  )

  // Listen for global voice toggle event (Ctrl+R shortcut)
  const handleVoiceToggle = () => {
    if (dictation) dictation.toggle()
  }
  window.addEventListener('ava:voice-toggle', handleVoiceToggle)

  onCleanup(() => {
    dictation?.stop()
    if (waveformRaf !== undefined) cancelAnimationFrame(waveformRaf)
    analyserHandle?.stop()
    window.removeEventListener('ava:voice-toggle', handleVoiceToggle)
  })

  return (
    <Show when={dictation}>
      <MicButton
        isRecording={isRecording}
        onToggle={() => dictation!.toggle()}
        supported={dictationSupported}
      />

      {/* Waveform visualizer */}
      <Show when={isRecording()}>
        <div class="flex items-center gap-[2px] w-[20px] h-[16px]">
          <For each={waveformBars()}>
            {(h) => (
              <div
                class="w-[2px] rounded-full bg-[var(--accent)] transition-[height] duration-75"
                style={{ height: `${Math.max(2, h)}px` }}
              />
            )}
          </For>
        </div>
      </Show>

      {/* Device picker */}
      <Show when={audioDevices().length > 1}>
        <select
          class="h-[18px] text-[var(--text-2xs)] max-w-[80px] truncate bg-transparent border-none outline-none text-[var(--text-tertiary)] cursor-pointer"
          style={{ 'font-family': 'var(--font-ui-mono)' }}
          value={settings().behavior.voiceDeviceId}
          onChange={(e) => {
            updateSettings({
              behavior: { ...settings().behavior, voiceDeviceId: e.currentTarget.value },
            })
          }}
        >
          <option value="">Default mic</option>
          <For each={audioDevices()}>
            {(dev) => (
              <option value={dev.deviceId}>{dev.label || `Mic ${dev.deviceId.slice(0, 6)}`}</option>
            )}
          </For>
        </select>
      </Show>
    </Show>
  )
}

/** Expose recording state accessor for parent to check */
export { isDictationSupported } from '../../../services/voice-dictation'
