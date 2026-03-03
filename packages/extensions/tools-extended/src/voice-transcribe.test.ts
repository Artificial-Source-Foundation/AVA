import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { resetSettingsManager } from '../../../core-v2/src/config/index.js'
import { voiceTranscribeTool } from './voice-transcribe.js'

const ctx = {
  sessionId: 'session',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('voiceTranscribeTool', () => {
  beforeEach(() => {
    resetSettingsManager()
    installMockPlatform()
  })

  afterEach(() => {
    vi.restoreAllMocks()
    resetSettingsManager()
  })

  it('transcribes with OpenAI provider', async () => {
    const platform = installMockPlatform()
    platform.fs.addBinary('/tmp/audio.wav', Uint8Array.from([1, 2, 3]))
    await platform.credentials.set('openai', 'test-key')

    vi.stubGlobal(
      'fetch',
      vi.fn(
        async () =>
          new Response(JSON.stringify({ text: 'hello world' }), {
            status: 200,
            headers: { 'content-type': 'application/json' },
          })
      )
    )

    const result = await voiceTranscribeTool.execute(
      { audioPath: '/tmp/audio.wav', provider: 'openai' },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toBe('hello world')
    expect(result.metadata?.promoteToUserMessage).toBe(true)
  })

  it('transcribes with local whisper provider', async () => {
    const platform = installMockPlatform()
    platform.fs.addBinary('/tmp/audio.wav', Uint8Array.from([1, 2, 3]))
    platform.fs.addFile('/tmp/audio.txt', 'local transcript\n')
    platform.shell.defaultResult = { stdout: '', stderr: '', exitCode: 0 }

    const result = await voiceTranscribeTool.execute(
      { audioPath: '/tmp/audio.wav', provider: 'local', model: 'base', language: 'en' },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toBe('local transcript')
    expect(result.metadata?.provider).toBe('local')
  })
})
