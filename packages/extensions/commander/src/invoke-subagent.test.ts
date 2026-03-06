import { describe, expect, it } from 'vitest'
import { createInvokeSubagentTool } from './invoke-subagent.js'

describe('invoke_subagent', () => {
  it('defines invoke_subagent tool', () => {
    const tool = createInvokeSubagentTool()
    expect(tool.definition.name).toBe('invoke_subagent')
  })

  it('supports reviewer validation inputs', () => {
    const tool = createInvokeSubagentTool()
    const schema = tool.definition.input_schema as unknown as {
      properties: {
        type: { enum: string[] }
        run_validation: { type: string }
      }
    }
    expect(schema.properties.type.enum).toContain('reviewer')
    expect(schema.properties.run_validation.type).toBe('boolean')
  })
})
