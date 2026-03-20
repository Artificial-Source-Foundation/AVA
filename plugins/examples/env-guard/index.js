Object.defineProperty(exports, '__esModule', { value: true })
const plugin_1 = require('@ava-ai/plugin')
const BLOCKED_PATTERNS = ['.env', 'credentials.json', '.secret', 'id_rsa', '.pem']
;(0, plugin_1.createPlugin)({
  'tool.before': async (ctx, params) => {
    const tool = params.tool
    const args = params.args ?? {}
    if (tool === 'read' || tool === 'edit' || tool === 'write') {
      const filePath = args.file_path || ''
      for (const pattern of BLOCKED_PATTERNS) {
        if (filePath.includes(pattern)) {
          throw new Error(`Blocked: ${tool} on sensitive file '${filePath}'`)
        }
      }
    }
    return { args }
  },
})
