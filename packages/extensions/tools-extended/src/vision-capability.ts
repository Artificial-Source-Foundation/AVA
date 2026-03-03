import { isVisionCapable as isProviderVisionCapable } from '../../providers/_shared/src/openai-compat.js'

export function isVisionCapable(model: string): boolean {
  return isProviderVisionCapable(model)
}
