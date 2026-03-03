import { getSettingsManager } from '@ava/core-v2/config'

export type VoiceProvider = 'openai' | 'local'

export interface VoiceSettings {
  provider: VoiceProvider
  model: string
  language: string
}

export const DEFAULT_VOICE_SETTINGS: VoiceSettings = {
  provider: 'openai',
  model: 'whisper-1',
  language: 'en',
}

export function getVoiceSettings(overrides?: Partial<VoiceSettings>): VoiceSettings {
  let current: Partial<VoiceSettings> = {}
  try {
    current = getSettingsManager().get<VoiceSettings>('voice')
  } catch {
    current = {}
  }

  return {
    ...DEFAULT_VOICE_SETTINGS,
    ...current,
    ...overrides,
  }
}
