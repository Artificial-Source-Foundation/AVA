/**
 * GitHub webhook handler — verify signature, parse events, extract @ava mentions.
 */

import { createHmac } from 'node:crypto'
import type { IncomingMessage } from 'node:http'
import type { BotTask, GitHubWebhookPayload } from './types.js'

const BOT_MENTION = /@ava\b/i

/** Verify GitHub webhook HMAC-SHA256 signature. */
export function verifySignature(payload: string, signature: string, secret: string): boolean {
  if (!signature.startsWith('sha256=')) return false
  const expected = createHmac('sha256', secret).update(payload).digest('hex')
  const actual = signature.slice(7)

  // Constant-time comparison
  if (expected.length !== actual.length) return false
  let diff = 0
  for (let i = 0; i < expected.length; i++) {
    diff |= expected.charCodeAt(i) ^ actual.charCodeAt(i)
  }
  return diff === 0
}

/** Read the full body from an HTTP request. */
export function readRequestBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    req.on('data', (chunk: Buffer) => chunks.push(chunk))
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf-8')))
    req.on('error', reject)
  })
}

/** Extract the task description from a comment body containing @ava. */
export function extractTask(commentBody: string): string | null {
  if (!BOT_MENTION.test(commentBody)) return null

  // Get text after @ava mention
  const match = commentBody.match(/@ava\s+(.+)/is)
  if (!match) return null

  return match[1]!.trim()
}

/** Parse a webhook payload into a BotTask (or null if not relevant). */
export function parseWebhookEvent(payload: GitHubWebhookPayload): BotTask | null {
  const comment = payload.comment
  if (!comment) return null

  // Only handle created comments
  if (payload.action !== 'created') return null

  const task = extractTask(comment.body)
  if (!task) return null

  const isPR = !!payload.pull_request
  const issueNumber = payload.pull_request?.number ?? payload.issue?.number
  if (!issueNumber) return null

  return {
    id: crypto.randomUUID(),
    repo: payload.repository.full_name,
    issueNumber,
    isPR,
    task,
    triggerUser: payload.sender.login,
    triggerUrl: comment.html_url,
    createdAt: Date.now(),
  }
}

/** Check if a user is allowed to trigger the bot. */
export function isUserAllowed(user: string, allowedUsers?: string[]): boolean {
  if (!allowedUsers || allowedUsers.length === 0) return true
  return allowedUsers.includes(user)
}

/** Check if a repo is allowed. */
export function isRepoAllowed(repo: string, allowedRepos?: string[]): boolean {
  if (!allowedRepos || allowedRepos.length === 0) return true
  return allowedRepos.includes(repo)
}
