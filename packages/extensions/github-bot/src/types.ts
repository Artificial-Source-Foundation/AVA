/**
 * GitHub bot types — webhook payloads, bot events.
 */

export interface GitHubWebhookPayload {
  action: string
  comment?: {
    id: number
    body: string
    user: { login: string }
    html_url: string
    created_at: string
  }
  issue?: {
    number: number
    title: string
    body: string
    html_url: string
    labels: Array<{ name: string }>
  }
  pull_request?: {
    number: number
    title: string
    body: string
    html_url: string
    head: { ref: string; sha: string }
    base: { ref: string }
    diff_url: string
  }
  repository: {
    full_name: string
    clone_url: string
    default_branch: string
  }
  sender: {
    login: string
  }
}

export interface BotTask {
  id: string
  repo: string
  issueNumber: number
  isPR: boolean
  task: string
  triggerUser: string
  triggerUrl: string
  createdAt: number
}

export interface BotResult {
  taskId: string
  success: boolean
  summary: string
  filesChanged?: string[]
  error?: string
  duration: number
}

export interface GitHubBotConfig {
  webhookSecret: string
  allowedRepos?: string[]
  allowedUsers?: string[]
  maxConcurrent?: number
}
