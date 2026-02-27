/**
 * Ollama Provider — Local model runner
 * https://ollama.com
 */

import { OllamaLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const ollama: LLMProviderConfig = {
  id: 'ollama',
  name: 'Ollama',
  icon: OllamaLogo,
  description: 'Run models locally',
  enabled: false,
  status: 'disconnected',
  baseUrl: 'http://localhost:11434',
  models: [
    {
      id: 'llama3.3:latest',
      name: 'Llama 3.3',
      contextWindow: 128000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'deepseek-r1:latest',
      name: 'DeepSeek R1',
      contextWindow: 64000,
      capabilities: ['tools', 'reasoning', 'free'],
    },
    {
      id: 'qwen2.5-coder:latest',
      name: 'Qwen 2.5 Coder',
      contextWindow: 32000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'codestral:latest',
      name: 'Codestral',
      contextWindow: 32000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'mistral:latest',
      name: 'Mistral',
      contextWindow: 32000,
      capabilities: ['tools', 'free'],
    },
    {
      id: 'phi4:latest',
      name: 'Phi-4',
      contextWindow: 16000,
      capabilities: ['tools', 'free'],
    },
  ],
}
