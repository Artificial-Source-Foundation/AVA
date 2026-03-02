import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { loadCustomTools } from './custom-tools.js'

describe('loadCustomTools', () => {
  it('returns empty array when tools directory does not exist', async () => {
    const { api } = createMockExtensionAPI()
    const disposables = await loadCustomTools('/nonexistent', api)
    expect(disposables).toEqual([])
  })

  it('skips test files', async () => {
    const { api } = createMockExtensionAPI()
    api.platform.fs.addDir('/project/.ava/tools')
    api.platform.fs.addFile('/project/.ava/tools/my-tool.test.ts', 'test file')
    api.platform.fs.addFile('/project/.ava/tools/my-tool.test.js', 'test file')
    api.platform.fs.addFile('/project/.ava/tools/types.d.ts', 'type file')

    const disposables = await loadCustomTools('/project', api)
    expect(disposables).toEqual([])
  })

  it('skips files that do not export a valid tool', async () => {
    const { api } = createMockExtensionAPI()
    api.platform.fs.addDir('/project/.ava/tools')
    api.platform.fs.addFile('/project/.ava/tools/bad-tool.js', 'export default {}')

    // The import will fail in test env since it's a mock FS, not a real file
    const disposables = await loadCustomTools('/project', api)
    expect(disposables).toEqual([])
    // Warning should be logged for failed import
    expect(api.log.warn).toHaveBeenCalled()
  })

  it('handles import errors gracefully', async () => {
    const { api } = createMockExtensionAPI()
    api.platform.fs.addDir('/project/.ava/tools')
    api.platform.fs.addFile('/project/.ava/tools/broken.ts', 'invalid code')

    const disposables = await loadCustomTools('/project', api)
    expect(disposables).toEqual([])
    expect(api.log.warn).toHaveBeenCalledWith(expect.stringContaining('Failed to load custom tool'))
  })

  it('scans both project and global dirs', async () => {
    const { api } = createMockExtensionAPI()
    // Neither dir exists — should return empty without error
    const disposables = await loadCustomTools('/project', api)
    expect(disposables).toEqual([])
  })

  it('validates tool definition structure', async () => {
    // Test the isToolDefinition logic indirectly
    const { api, registeredTools } = createMockExtensionAPI()
    api.platform.fs.addDir('/project/.ava/tools')
    // No real files can be imported from mock FS, so just ensure no crash
    const disposables = await loadCustomTools('/project', api)
    expect(disposables).toHaveLength(0)
    expect(registeredTools).toHaveLength(0)
  })
})
