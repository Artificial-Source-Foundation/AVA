import { dirname, join, parse } from 'node:path'
import { getSettingsManager } from '@ava/core-v2/config'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import { z } from 'zod'
import { DEFAULT_VOICE_SETTINGS, getVoiceSettings, type VoiceProvider } from './voice-settings.js'

const schema = z.object({
  audioPath: z.string().describe('Path to local audio file.'),
  provider: z.enum(['openai', 'local']).optional(),
  model: z.string().optional(),
  language: z.string().optional(),
})

async function transcribeWithOpenAI(
  path: string,
  model: string,
  language: string
): Promise<string> {
  const token = await getPlatform().credentials.get('openai')
  if (!token) {
    throw new Error('OpenAI API key is required for voice.provider=openai')
  }

  const bytes = await getPlatform().fs.readBinary(path)
  const form = new FormData()
  form.append('model', model)
  form.append('language', language)
  form.append('file', new Blob([bytes]), parse(path).base)

  const response = await fetch('https://api.openai.com/v1/audio/transcriptions', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
    },
    body: form,
  })

  if (!response.ok) {
    const body = await response.text()
    throw new Error(`OpenAI transcription failed (${response.status}): ${body}`)
  }

  const payload = (await response.json()) as { text?: string }
  if (!payload.text) {
    throw new Error('OpenAI transcription response did not include text')
  }

  return payload.text
}

async function transcribeWithLocal(path: string, model: string, language: string): Promise<string> {
  const outputDir = dirname(path)
  const outputBase = parse(path).name
  const command = `whisper "${path}" --model "${model}" --language "${language}" --output_format txt --output_dir "${outputDir}"`
  const result = await getPlatform().shell.exec(command)
  if (result.exitCode !== 0) {
    throw new Error(result.stderr || 'Local whisper transcription failed')
  }

  const outputPath = join(outputDir, `${outputBase}.txt`)
  const text = await getPlatform().fs.readFile(outputPath)
  return text.trim()
}

export const voiceTranscribeTool = defineTool({
  name: 'voice_transcribe',
  description:
    'Transcribe local audio file to text via OpenAI Whisper API or local whisper binary.',
  schema,
  async execute(input, ctx) {
    try {
      const resolved = await resolvePathSafe(input.audioPath, ctx.workingDirectory)

      try {
        getSettingsManager().registerCategory('voice', DEFAULT_VOICE_SETTINGS)
      } catch {
        // already registered
      }

      const settings = getVoiceSettings({
        provider: input.provider as VoiceProvider | undefined,
        model: input.model,
        language: input.language,
      })

      const text =
        settings.provider === 'openai'
          ? await transcribeWithOpenAI(resolved, settings.model, settings.language)
          : await transcribeWithLocal(resolved, settings.model, settings.language)

      return {
        success: true,
        output: text,
        metadata: {
          provider: settings.provider,
          model: settings.model,
          language: settings.language,
          promoteToUserMessage: true,
          userMessage: text,
        },
      }
    } catch (error) {
      return {
        success: false,
        output: '',
        error: error instanceof Error ? error.message : String(error),
      }
    }
  },
})
