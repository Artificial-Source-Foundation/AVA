/**
 * GitHub bot extension — respond to @ava mentions in issues and PRs.
 *
 * Registers a webhook route with the server extension.
 * Parses GitHub webhook events, extracts @ava mentions,
 * and runs agent tasks with context from the issue/PR.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { buildIssueContext, buildPRContext } from './context.js'
import { buildCommentCommand, formatAckComment, formatResultComment } from './poster.js'
import type { BotResult, GitHubBotConfig, GitHubWebhookPayload } from './types.js'
import {
  isRepoAllowed,
  isUserAllowed,
  parseWebhookEvent,
  readRequestBody,
  verifySignature,
} from './webhook.js'

export function activate(api: ExtensionAPI): Disposable {
  const config: GitHubBotConfig = {
    webhookSecret: process.env.AVA_GITHUB_WEBHOOK_SECRET ?? '',
    allowedRepos: process.env.AVA_GITHUB_ALLOWED_REPOS?.split(',').map((s) => s.trim()),
    allowedUsers: process.env.AVA_GITHUB_ALLOWED_USERS?.split(',').map((s) => s.trim()),
    maxConcurrent: 3,
  }

  let activeTasks = 0

  // Register webhook route with server extension
  api.emit('server:register-route', {
    path: '/api/v1/github/webhook',
    handler: async (req: unknown, res: unknown) => {
      const httpReq = req as import('node:http').IncomingMessage
      const httpRes = res as import('node:http').ServerResponse

      try {
        const body = await readRequestBody(httpReq)

        // Verify signature if secret is configured
        if (config.webhookSecret) {
          const sig = (httpReq.headers['x-hub-signature-256'] as string) ?? ''
          if (!verifySignature(body, sig, config.webhookSecret)) {
            httpRes.writeHead(401)
            httpRes.end('Invalid signature')
            return
          }
        }

        const payload = JSON.parse(body) as GitHubWebhookPayload
        const task = parseWebhookEvent(payload)

        if (!task) {
          httpRes.writeHead(200)
          httpRes.end('No @ava mention found')
          return
        }

        // Access control
        if (!isUserAllowed(task.triggerUser, config.allowedUsers)) {
          httpRes.writeHead(403)
          httpRes.end('User not allowed')
          return
        }

        if (!isRepoAllowed(task.repo, config.allowedRepos)) {
          httpRes.writeHead(403)
          httpRes.end('Repository not allowed')
          return
        }

        if (activeTasks >= (config.maxConcurrent ?? 3)) {
          httpRes.writeHead(429)
          httpRes.end('Too many concurrent tasks')
          return
        }

        // Accept webhook
        httpRes.writeHead(202)
        httpRes.end(JSON.stringify({ taskId: task.id }))

        // Process task asynchronously
        activeTasks++
        const context = task.isPR ? buildPRContext(task, payload) : buildIssueContext(task, payload)

        // Post acknowledgment
        const ackBody = formatAckComment(task)
        const ackCmd = buildCommentCommand(task.repo, task.issueNumber, ackBody, task.isPR)
        api.emit('github-bot:ack', { task, command: ackCmd })

        // Run the agent
        const startTime = Date.now()
        api.emit('server:run', {
          runId: task.id,
          goal: `${context}\n\n---\n\nPlease complete the task described above.`,
        })

        // Listen for completion
        const unsub = api.on('server:run-complete', (data: unknown) => {
          const ev = data as Record<string, unknown>
          if (ev.runId !== task.id) return
          unsub.dispose()
          activeTasks--

          const result: BotResult = {
            taskId: task.id,
            success: !ev.error,
            summary: (ev.result as string) ?? 'Task completed',
            error: ev.error as string | undefined,
            duration: Date.now() - startTime,
          }

          // Post result comment
          const resultBody = formatResultComment(task, result)
          const resultCmd = buildCommentCommand(task.repo, task.issueNumber, resultBody, task.isPR)
          api.emit('github-bot:result', { task, result, command: resultCmd })
        })
      } catch (err) {
        httpRes.writeHead(500)
        httpRes.end(String(err))
      }
    },
  })

  api.log.debug('GitHub bot extension activated')

  return {
    dispose() {
      activeTasks = 0
    },
  }
}
