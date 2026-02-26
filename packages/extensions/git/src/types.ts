/**
 * Git extension types.
 */

export interface GitSnapshot {
  hash: string
  message: string
  timestamp: number
  files: string[]
}

export interface GitConfig {
  autoCommit: boolean
  snapshotOnToolCall: boolean
  maxSnapshots: number
}

export const DEFAULT_GIT_CONFIG: GitConfig = {
  autoCommit: false,
  snapshotOnToolCall: true,
  maxSnapshots: 50,
}
