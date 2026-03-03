import { applyPatch } from '../apply-patch/applier.js'
import { parsePatch, validatePatch } from '../apply-patch/parser.js'
import { type ChunkParseState, createChunkParseState, parseStreamingChunk } from './chunk-parser.js'
import type { StreamingApplyResult, StreamingChunkResult } from './types.js'

export class StreamingDiffApplier {
  private readonly state: ChunkParseState = createChunkParseState()
  private partialLine = ''
  private appliedCount = 0
  private errors: string[] = []

  constructor(
    private readonly workingDirectory: string,
    private readonly dryRun = false
  ) {}

  async pushChunk(chunk: string): Promise<StreamingChunkResult> {
    const lines = this.consumeChunk(chunk)
    const parsed = parseStreamingChunk(lines, this.state)
    this.state.sawBegin = parsed.state.sawBegin
    this.state.sawEnd = parsed.state.sawEnd
    this.state.currentOperation = parsed.state.currentOperation

    const before = this.appliedCount
    for (const operationLines of parsed.operations) {
      await this.applyOperation(operationLines)
    }

    return {
      appliedCount: this.appliedCount - before,
      pendingCount: this.state.currentOperation.length,
      hadError: this.errors.length > 0,
      errors: [...this.errors],
    }
  }

  async finalize(): Promise<StreamingApplyResult> {
    if (this.partialLine.length > 0) {
      await this.pushChunk('\n')
    }

    if (this.state.currentOperation.length > 0) {
      await this.applyOperation(this.state.currentOperation)
      this.state.currentOperation = []
    }

    return {
      success: this.errors.length === 0,
      appliedCount: this.appliedCount,
      pendingCount: this.state.currentOperation.length,
      errors: [...this.errors],
    }
  }

  private consumeChunk(chunk: string): string[] {
    const merged = this.partialLine + chunk
    const raw = merged.split('\n')
    this.partialLine = raw.pop() ?? ''
    return raw
  }

  private async applyOperation(lines: string[]): Promise<void> {
    const raw = `*** Begin Patch\n${lines.join('\n')}\n*** End Patch\n`
    const parsed = parsePatch(raw)
    const errors = validatePatch(parsed)
    if (errors.length > 0) {
      this.errors.push(...errors)
      return
    }

    const result = await applyPatch(parsed, this.workingDirectory, this.dryRun)
    if (result.successCount > 0) {
      this.appliedCount += result.successCount
    }
    if (!result.success) {
      if (result.error) this.errors.push(result.error)
      for (const file of result.files) {
        if (!file.success && file.error) this.errors.push(file.error)
      }
    }
  }
}
