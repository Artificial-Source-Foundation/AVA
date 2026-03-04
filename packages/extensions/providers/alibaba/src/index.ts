import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { AlibabaClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('alibaba', () => new AlibabaClient())
}
