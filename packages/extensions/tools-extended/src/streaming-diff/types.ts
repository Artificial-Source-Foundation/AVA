import type { ParsedPatch } from '../apply-patch/parser.js'

export interface StreamingChunkResult {
  appliedCount: number
  pendingCount: number
  hadError: boolean
  errors: string[]
}

export interface StreamingApplyResult {
  success: boolean
  appliedCount: number
  pendingCount: number
  errors: string[]
}

export interface StreamingParsedPatch {
  patch: ParsedPatch
  raw: string
}
