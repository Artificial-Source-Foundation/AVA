/**
 * MCP extension — Model Context Protocol client.
 * Connects to MCP servers and registers their tools.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('MCP extension activated')
  return {
    dispose() {
      api.log.debug('MCP extension deactivated')
    },
  }
}
