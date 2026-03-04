import type {
  INativeCompute,
  NativeFuzzyReplaceInput,
  NativeFuzzyReplaceOutput,
  NativeGrepInput,
  NativeGrepOutput,
} from '@ava/core-v2'
import { invoke } from '@tauri-apps/api/core'

export class TauriNativeCompute implements INativeCompute {
  async grep(input: NativeGrepInput): Promise<NativeGrepOutput> {
    return invoke<NativeGrepOutput>('compute_grep', { input })
  }

  async fuzzyReplace(input: NativeFuzzyReplaceInput): Promise<NativeFuzzyReplaceOutput> {
    return invoke<NativeFuzzyReplaceOutput>('compute_fuzzy_replace', { input })
  }
}
