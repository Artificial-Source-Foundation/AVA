/**
 * Voice Dictation Service
 *
 * Web Speech API wrapper for continuous speech-to-text dictation.
 * Returns null if the browser doesn't support SpeechRecognition.
 */

// TypeScript declarations for Web Speech API
interface SpeechRecognitionEvent extends Event {
  resultIndex: number
  results: SpeechRecognitionResultList
}

interface SpeechRecognitionResultList {
  length: number
  item(index: number): SpeechRecognitionResult
  [index: number]: SpeechRecognitionResult
}

interface SpeechRecognitionResult {
  isFinal: boolean
  length: number
  item(index: number): SpeechRecognitionAlternative
  [index: number]: SpeechRecognitionAlternative
}

interface SpeechRecognitionAlternative {
  transcript: string
  confidence: number
}

interface SpeechRecognitionErrorEvent extends Event {
  error: string
  message: string
}

interface SpeechRecognition extends EventTarget {
  continuous: boolean
  interimResults: boolean
  lang: string
  start(): void
  stop(): void
  abort(): void
  onresult: ((event: SpeechRecognitionEvent) => void) | null
  onerror: ((event: SpeechRecognitionErrorEvent) => void) | null
  onend: (() => void) | null
  onstart: (() => void) | null
}

declare global {
  interface Window {
    SpeechRecognition: new () => SpeechRecognition
    webkitSpeechRecognition: new () => SpeechRecognition
  }
}

export interface DictationCallbacks {
  onTranscript: (text: string) => void
  onStateChange: (recording: boolean) => void
  onError: (error: string) => void
}

export interface DictationHandle {
  start: () => void
  stop: () => void
  toggle: () => void
  isRecording: () => boolean
}

/** Check if the Web Speech API is available */
export function isDictationSupported(): boolean {
  return !!(window.SpeechRecognition || window.webkitSpeechRecognition)
}

// ---------------------------------------------------------------------------
// Audio Analyser (Feature 1.4 — waveform data for visualizer)
// ---------------------------------------------------------------------------

export interface AudioAnalyserHandle {
  getFrequencyData(): Uint8Array
  stop(): void
}

/** Create an AudioContext + AnalyserNode for real-time frequency data. */
export async function createAudioAnalyser(deviceId?: string): Promise<AudioAnalyserHandle> {
  const constraints: MediaStreamConstraints = {
    audio: deviceId ? { deviceId: { exact: deviceId } } : true,
  }
  const stream = await navigator.mediaDevices.getUserMedia(constraints)
  const ctx = new AudioContext()
  const source = ctx.createMediaStreamSource(stream)
  const analyser = ctx.createAnalyser()
  analyser.fftSize = 64
  source.connect(analyser)
  const data = new Uint8Array(analyser.frequencyBinCount)

  return {
    getFrequencyData() {
      analyser.getByteFrequencyData(data)
      return data
    },
    stop() {
      source.disconnect()
      for (const track of stream.getTracks()) track.stop()
      void ctx.close()
    },
  }
}

// ---------------------------------------------------------------------------
// Audio Device Enumeration (Feature 1.5 — device picker)
// ---------------------------------------------------------------------------

/** List available audio input devices. */
export async function getAudioDevices(): Promise<MediaDeviceInfo[]> {
  const devices = await navigator.mediaDevices.enumerateDevices()
  return devices.filter((d) => d.kind === 'audioinput')
}

/** Create a dictation handle. Returns null if not supported. */
export function createDictation(callbacks: DictationCallbacks): DictationHandle | null {
  const Ctor = window.SpeechRecognition || window.webkitSpeechRecognition
  if (!Ctor) return null

  const recognition = new Ctor()
  recognition.continuous = true
  recognition.interimResults = true
  recognition.lang = 'en-US'

  let recording = false
  let shouldBeRecording = false

  recognition.onresult = (event: SpeechRecognitionEvent) => {
    for (let i = event.resultIndex; i < event.results.length; i++) {
      const result = event.results[i]
      if (result.isFinal) {
        callbacks.onTranscript(`${result[0].transcript} `)
      }
    }
  }

  recognition.onerror = (event: SpeechRecognitionErrorEvent) => {
    const errorMap: Record<string, string> = {
      'no-speech': 'No speech detected',
      'audio-capture': 'Microphone not available',
      'not-allowed': 'Microphone permission denied',
      aborted: 'Recognition aborted',
      network: 'Network error',
    }
    const message = errorMap[event.error] || `Speech error: ${event.error}`
    if (event.error !== 'aborted') {
      callbacks.onError(message)
    }
    if (event.error === 'not-allowed' || event.error === 'audio-capture') {
      shouldBeRecording = false
      recording = false
      callbacks.onStateChange(false)
    }
  }

  recognition.onend = () => {
    recording = false
    // Auto-restart if it stopped unexpectedly (browser timeout)
    if (shouldBeRecording) {
      try {
        recognition.start()
        recording = true
      } catch {
        shouldBeRecording = false
        callbacks.onStateChange(false)
      }
    } else {
      callbacks.onStateChange(false)
    }
  }

  recognition.onstart = () => {
    recording = true
    callbacks.onStateChange(true)
  }

  return {
    start() {
      if (recording) return
      shouldBeRecording = true
      try {
        recognition.start()
      } catch {
        callbacks.onError('Failed to start recognition')
      }
    },
    stop() {
      shouldBeRecording = false
      if (recording) {
        recognition.stop()
      }
    },
    toggle() {
      if (recording) {
        this.stop()
      } else {
        this.start()
      }
    },
    isRecording: () => recording,
  }
}
