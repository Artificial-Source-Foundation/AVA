import { describe, expect, it } from 'vitest'

import { mapWebSessionMessageRows, mapWebSessionMessages } from './web-session-messages'

describe('web session message mapping', () => {
  it('round-trips images from web session payloads into frontend messages', () => {
    const messages = mapWebSessionMessages(
      [
        {
          id: 'msg-1',
          role: 'user',
          content: 'describe this image',
          timestamp: '2026-04-20T10:00:00Z',
          images: [{ data: 'base64-image', media_type: 'image/png' }],
          metadata: { source: 'backend' },
        },
      ],
      'session-1'
    )

    expect(messages).toEqual([
      expect.objectContaining({
        id: 'msg-1',
        sessionId: 'session-1',
        images: [{ data: 'base64-image', mimeType: 'image/png' }],
        metadata: expect.objectContaining({
          source: 'backend',
          images: [{ data: 'base64-image', mimeType: 'image/png' }],
        }),
      }),
    ])
  })

  it('persists mapped images into web fallback message rows', () => {
    const rows = mapWebSessionMessageRows(
      [
        {
          id: 'msg-2',
          role: 'user',
          content: 'keep my attachment',
          created_at: 1,
          images: [{ data: 'base64-image', media_type: 'image/webp' }],
        },
      ],
      'session-2'
    )

    expect(JSON.parse(String(rows[0]?.metadata))).toEqual({
      images: [{ data: 'base64-image', mimeType: 'image/webp' }],
    })
  })
})
