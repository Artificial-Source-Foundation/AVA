import type { DeclarativePolicyRule, PolicySource } from '../types.js'

export interface PolicyLoadItem {
  path: string
  source: PolicySource
  content: string
}

export interface PolicyLoadResult {
  files: PolicyLoadItem[]
  warnings: string[]
}

export interface PolicyParseResult {
  rules: DeclarativePolicyRule[]
  warnings: string[]
}

export interface PolicyRuleInput {
  name: string
  tool: string
  decision: 'allow' | 'deny' | 'ask'
  priority?: number
  reason?: string
  argsPattern?: string
  paths?: string[]
  modes?: string[]
}
