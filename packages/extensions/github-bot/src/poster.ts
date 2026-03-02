/**
 * GitHub result poster — format and post results as comments.
 */

import type { BotResult, BotTask } from './types.js'

/** Format a bot result as a GitHub markdown comment. */
export function formatResultComment(task: BotTask, result: BotResult): string {
  const statusEmoji = result.success ? ':white_check_mark:' : ':x:'
  const duration = (result.duration / 1000).toFixed(1)

  const parts = [
    `${statusEmoji} **AVA completed task** (${duration}s)`,
    '',
    `> ${task.task.slice(0, 200)}${task.task.length > 200 ? '...' : ''}`,
    '',
  ]

  if (result.summary) {
    parts.push('### Summary', result.summary, '')
  }

  if (result.error) {
    parts.push(
      '<details><summary>Error details</summary>',
      '',
      '```',
      result.error,
      '```',
      '',
      '</details>',
      ''
    )
  }

  if (result.filesChanged && result.filesChanged.length > 0) {
    parts.push(
      `<details><summary>Files changed (${result.filesChanged.length})</summary>`,
      '',
      result.filesChanged.map((f) => `- \`${f}\``).join('\n'),
      '',
      '</details>',
      ''
    )
  }

  parts.push(`---`, `*Triggered by @${task.triggerUser}*`)

  return parts.join('\n')
}

/** Build `gh` CLI command to post a comment on an issue/PR. */
export function buildCommentCommand(
  repo: string,
  issueNumber: number,
  body: string,
  isPR: boolean
): string {
  const safeBody = body.replace(/'/g, "'\\''")
  if (isPR) {
    return `gh pr comment ${issueNumber} --repo ${repo} --body '${safeBody}'`
  }
  return `gh issue comment ${issueNumber} --repo ${repo} --body '${safeBody}'`
}

/** Format a "working on it" acknowledgment comment. */
export function formatAckComment(task: BotTask): string {
  return [
    ':robot: **AVA is working on this...**',
    '',
    `> ${task.task.slice(0, 200)}${task.task.length > 200 ? '...' : ''}`,
    '',
    `Triggered by @${task.triggerUser}`,
  ].join('\n')
}
