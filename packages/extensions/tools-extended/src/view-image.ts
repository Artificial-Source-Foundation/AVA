import type { ImageBlock } from '@ava/core-v2/llm'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import { z } from 'zod'

const mediaByExtension: Record<string, ImageBlock['source']['media_type']> = {
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
}

function getMediaType(path: string): ImageBlock['source']['media_type'] | null {
  const lower = path.toLowerCase()
  const entry = Object.entries(mediaByExtension).find(([ext]) => lower.endsWith(ext))
  return entry?.[1] ?? null
}

export const viewImageTool = defineTool({
  name: 'view_image',
  description:
    'Reads an image file and returns an ImageBlock payload for multimodal models to inspect.',
  schema: z.object({
    path: z.string().describe('Path to a local image file (png, jpg, jpeg, gif, webp).'),
  }),
  async execute(input, ctx) {
    const absolutePath = await resolvePathSafe(input.path, ctx.workingDirectory)

    const mediaType = getMediaType(absolutePath)
    if (!mediaType) {
      return {
        success: false,
        output: '',
        error: 'Unsupported image type. Allowed: png, jpg, jpeg, gif, webp',
      }
    }

    try {
      const bytes = await getPlatform().fs.readBinary(absolutePath)
      const base64 = Buffer.from(bytes).toString('base64')

      const image: ImageBlock = {
        type: 'image',
        source: {
          type: 'base64',
          media_type: mediaType,
          data: base64,
        },
      }

      return {
        success: true,
        output: `Loaded image ${absolutePath}`,
        metadata: {
          image,
        },
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      return {
        success: false,
        output: '',
        error: `Failed to read image: ${message}`,
      }
    }
  },
})
