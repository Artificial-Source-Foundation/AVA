import { describe, expect, it } from 'vitest'
import type { ContentBlock, ImageBlock } from './types.js'

describe('ImageBlock', () => {
  it('can be used in ContentBlock arrays with base64 source', () => {
    const blocks: ContentBlock[] = [
      { type: 'text', text: 'Here is a screenshot' },
      {
        type: 'image',
        source: {
          type: 'base64',
          media_type: 'image/png',
          data: 'iVBORw0KGgoAAAANSUhEUg==',
        },
      },
    ]

    expect(blocks).toHaveLength(2)
    expect(blocks[0]!.type).toBe('text')
    expect(blocks[1]!.type).toBe('image')

    const imageBlock = blocks[1] as ImageBlock
    expect(imageBlock.source.type).toBe('base64')
    expect(imageBlock.source.media_type).toBe('image/png')
    expect(imageBlock.source.data).toBe('iVBORw0KGgoAAAANSUhEUg==')
  })

  it('can be used in ContentBlock arrays with url source', () => {
    const blocks: ContentBlock[] = [
      {
        type: 'image',
        source: {
          type: 'url',
          media_type: 'image/jpeg',
          data: 'https://example.com/image.jpg',
        },
      },
      { type: 'text', text: 'What is in this image?' },
    ]

    expect(blocks).toHaveLength(2)

    const imageBlock = blocks[0] as ImageBlock
    expect(imageBlock.source.type).toBe('url')
    expect(imageBlock.source.media_type).toBe('image/jpeg')
    expect(imageBlock.source.data).toBe('https://example.com/image.jpg')
  })

  it('supports all media types', () => {
    const mediaTypes = ['image/jpeg', 'image/png', 'image/gif', 'image/webp'] as const

    for (const mediaType of mediaTypes) {
      const block: ImageBlock = {
        type: 'image',
        source: {
          type: 'base64',
          media_type: mediaType,
          data: 'dGVzdA==',
        },
      }
      expect(block.source.media_type).toBe(mediaType)
    }
  })

  it('coexists with tool_use and tool_result blocks', () => {
    const blocks: ContentBlock[] = [
      { type: 'text', text: 'Analyzing image...' },
      {
        type: 'image',
        source: {
          type: 'base64',
          media_type: 'image/webp',
          data: 'UklGR...',
        },
      },
      {
        type: 'tool_use',
        id: 'call_1',
        name: 'read_file',
        input: { path: '/test.txt' },
      },
      {
        type: 'tool_result',
        tool_use_id: 'call_1',
        content: 'file contents',
      },
    ]

    expect(blocks).toHaveLength(4)
    expect(blocks.map((b) => b.type)).toEqual(['text', 'image', 'tool_use', 'tool_result'])
  })
})
