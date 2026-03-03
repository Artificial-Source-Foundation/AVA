import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  installMockPlatform,
  type MockPlatform,
} from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { viewImageTool } from './view-image.js'

let platform: MockPlatform

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/tmp',
    signal: AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  // no-op for parity with other tests
})

describe('viewImageTool', () => {
  it('has correct name', () => {
    expect(viewImageTool.definition.name).toBe('view_image')
  })

  it('reads image bytes and returns an ImageBlock payload', async () => {
    const bytes = Uint8Array.from([0x89, 0x50, 0x4e, 0x47])
    platform.fs.addBinary('/tmp/pixel.png', bytes)

    const result = await viewImageTool.execute({ path: '/tmp/pixel.png' }, makeCtx())

    expect(result.success).toBe(true)
    const block = (
      result.metadata as { image: { type: string; source: { media_type: string; data: string } } }
    ).image
    expect(block.type).toBe('image')
    expect(block.source.media_type).toBe('image/png')
    expect(block.source.data).toBe(Buffer.from(bytes).toString('base64'))
  })

  it('fails for unsupported image extension', async () => {
    platform.fs.addBinary('/tmp/blob.bmp', Uint8Array.from([0x42, 0x4d]))

    const result = await viewImageTool.execute({ path: '/tmp/blob.bmp' }, makeCtx())

    expect(result.success).toBe(false)
    expect(result.error).toContain('Unsupported image type')
  })
})
