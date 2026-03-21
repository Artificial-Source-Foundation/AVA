import { createPlugin } from '@ava-ai/plugin'

const BLOCKED_PATTERNS = ['.env', 'credentials.json', '.secret', 'id_rsa', '.pem']

createPlugin({
  'tool.before': async (_ctx, params) => {
    const tool = params.tool as string
    const args = (params.args ?? {}) as Record<string, unknown>

    if (tool === 'read' || tool === 'edit' || tool === 'write') {
      const filePath = (args.file_path as string) || ''
      for (const pattern of BLOCKED_PATTERNS) {
        if (filePath.includes(pattern)) {
          throw new Error(`Blocked: ${tool} on sensitive file '${filePath}'`)
        }
      }
    }

    return { args }
  },
})
