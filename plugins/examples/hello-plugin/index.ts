import { createPlugin } from '@ava-ai/plugin'

createPlugin({
  'session.start': async (ctx, params) => {
    const sessionId = params.session_id ?? 'unknown'
    process.stderr.write(`[hello] Session ${sessionId} started in ${ctx.project.directory}\n`)
  },

  'session.end': async (_ctx, params) => {
    const sessionId = params.session_id ?? 'unknown'
    process.stderr.write(`[hello] Session ${sessionId} ended\n`)
  },
})
