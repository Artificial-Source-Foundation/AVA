import { createPlugin } from '@ava-ai/plugin'

createPlugin(
  {
    'session.start': async (ctx, params) => {
      const sessionId = params.session_id ?? 'unknown'
      process.stderr.write(`[hello] Session ${sessionId} started in ${ctx.project.directory}\n`)
      return undefined
    },

    'session.end': async (_ctx, params) => {
      const sessionId = params.session_id ?? 'unknown'
      process.stderr.write(`[hello] Session ${sessionId} ended\n`)
      return undefined
    },
  },
  {
    capabilities: {
      commands: [{ name: 'demo.ping', description: 'Ping the hello plugin' }],
      routes: [{ path: '/status', method: 'GET', description: 'Get plugin status' }],
      events: [{ name: 'demo.updated', description: 'Emitted after demo.ping' }],
      mounts: [
        {
          id: 'hello-plugin.settings',
          location: 'settings.section',
          label: 'Hello Plugin',
          description: 'Example plugin settings mount',
        },
      ],
    },
    commands: {
      'demo.ping': async (ctx, payload) => ({
        result: {
          ok: true,
          project: ctx.project.name,
          payload,
        },
        emittedEvents: [
          {
            event: 'demo.updated',
            payload: { source: 'hello-plugin', project: ctx.project.name || 'unknown' },
          },
        ],
      }),
    },
    routes: {
      'GET /status': async (ctx) => ({
        result: {
          plugin: 'hello-plugin',
          status: 'ok',
          project: ctx.project.name,
        },
      }),
    },
  }
)
