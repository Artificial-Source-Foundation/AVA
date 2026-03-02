/**
 * GitHub context collector — gather PR/issue context for the agent.
 */

import type { BotTask, GitHubWebhookPayload } from './types.js'

/** Build agent context from a PR-triggered task. */
export function buildPRContext(task: BotTask, payload: GitHubWebhookPayload): string {
  const pr = payload.pull_request
  if (!pr) return buildIssueContext(task, payload)

  const parts = [
    `## PR #${pr.number}: ${pr.title}`,
    `Branch: ${pr.head.ref} → ${pr.base.ref}`,
    `Repository: ${task.repo}`,
    '',
    `### PR Description`,
    pr.body || '(no description)',
    '',
    `### Task from @${task.triggerUser}`,
    task.task,
  ]

  return parts.join('\n')
}

/** Build agent context from an issue-triggered task. */
export function buildIssueContext(task: BotTask, payload: GitHubWebhookPayload): string {
  const issue = payload.issue

  const parts = [
    `## Issue #${task.issueNumber}${issue ? `: ${issue.title}` : ''}`,
    `Repository: ${task.repo}`,
  ]

  if (issue?.labels.length) {
    parts.push(`Labels: ${issue.labels.map((l) => l.name).join(', ')}`)
  }

  parts.push(
    '',
    `### Issue Description`,
    issue?.body || '(no description)',
    '',
    `### Task from @${task.triggerUser}`,
    task.task
  )

  return parts.join('\n')
}

/** Build a `gh` CLI command to fetch PR diff. */
export function buildDiffCommand(repo: string, prNumber: number): string {
  return `gh pr diff ${prNumber} --repo ${repo}`
}

/** Build a `gh` CLI command to list PR files. */
export function buildFilesCommand(repo: string, prNumber: number): string {
  return `gh pr diff ${prNumber} --repo ${repo} --name-only`
}

/** Build a `gh` CLI command to get related issues. */
export function buildRelatedIssuesCommand(repo: string, prNumber: number): string {
  return `gh pr view ${prNumber} --repo ${repo} --json closingIssuesReferences --jq '.closingIssuesReferences[].number'`
}
