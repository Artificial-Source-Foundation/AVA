/**
 * Provider Defaults
 *
 * Canonical types and default configurations for LLM providers.
 * Shared by the settings store and the ProvidersTab UI.
 */

import { Bot, Braces, Cloud, Cpu, Flame, Globe, Monitor, Shield, Sparkles, Zap } from 'lucide-solid'
import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

type IconComponent = Component<{ class?: string }>

export interface ProviderModel {
  id: string
  name: string
  contextWindow: number
  isDefault?: boolean
}

export interface LLMProviderConfig {
  id: string
  name: string
  icon: IconComponent
  description: string
  enabled: boolean
  apiKey?: string
  baseUrl?: string
  defaultModel?: string
  models: ProviderModel[]
  status: 'connected' | 'disconnected' | 'error'
  error?: string
}

// ============================================================================
// Default Provider Configurations
// ============================================================================

export const defaultProviders: LLMProviderConfig[] = [
  {
    id: 'anthropic',
    name: 'Anthropic',
    icon: Sparkles as IconComponent,
    description: 'Claude models with advanced reasoning',
    enabled: true,
    status: 'disconnected',
    models: [
      {
        id: 'claude-sonnet-4-5-20250514',
        name: 'Claude Sonnet 4.5',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'claude-opus-4-5-20251124', name: 'Claude Opus 4.5', contextWindow: 200000 },
      { id: 'claude-haiku-4-5-20251022', name: 'Claude Haiku 4.5', contextWindow: 200000 },
      { id: 'claude-opus-4-1-20250801', name: 'Claude Opus 4.1', contextWindow: 200000 },
      { id: 'claude-opus-4-20250514', name: 'Claude Opus 4', contextWindow: 200000 },
      { id: 'claude-sonnet-4-20250514', name: 'Claude Sonnet 4', contextWindow: 200000 },
      { id: 'claude-3-7-sonnet-20250219', name: 'Claude 3.7 Sonnet', contextWindow: 200000 },
      { id: 'claude-3-5-sonnet-20241022', name: 'Claude 3.5 Sonnet v2', contextWindow: 200000 },
      { id: 'claude-3-5-haiku-20241022', name: 'Claude 3.5 Haiku', contextWindow: 200000 },
    ],
    defaultModel: 'claude-sonnet-4-5-20250514',
  },
  {
    id: 'openai',
    name: 'OpenAI',
    icon: Cpu as IconComponent,
    description: 'GPT and reasoning models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'gpt-5.2', name: 'GPT-5.2', contextWindow: 196000, isDefault: true },
      { id: 'gpt-5.2-mini', name: 'GPT-5.2 Mini', contextWindow: 128000 },
      { id: 'gpt-5.1-codex-max', name: 'GPT-5.1 Codex Max', contextWindow: 196000 },
      { id: 'gpt-4.1', name: 'GPT-4.1', contextWindow: 1000000 },
      { id: 'gpt-4.1-mini', name: 'GPT-4.1 Mini', contextWindow: 1000000 },
      { id: 'gpt-4.1-nano', name: 'GPT-4.1 Nano', contextWindow: 1000000 },
      { id: 'o3', name: 'o3', contextWindow: 200000 },
      { id: 'o3-pro', name: 'o3 Pro', contextWindow: 200000 },
      { id: 'o4-mini', name: 'o4 Mini', contextWindow: 200000 },
      { id: 'gpt-4o', name: 'GPT-4o', contextWindow: 128000 },
      { id: 'gpt-4o-mini', name: 'GPT-4o Mini', contextWindow: 128000 },
    ],
    defaultModel: 'gpt-5.2',
  },
  {
    id: 'google',
    name: 'Google',
    icon: Globe as IconComponent,
    description: 'Gemini models with large context',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000, isDefault: true },
      { id: 'gemini-2.5-flash', name: 'Gemini 2.5 Flash', contextWindow: 1000000 },
      { id: 'gemini-2.0-flash', name: 'Gemini 2.0 Flash', contextWindow: 1000000 },
      { id: 'gemini-1.5-pro', name: 'Gemini 1.5 Pro', contextWindow: 2000000 },
    ],
    defaultModel: 'gemini-2.5-pro',
  },
  {
    id: 'copilot',
    name: 'GitHub Copilot',
    icon: Monitor as IconComponent,
    description: 'Copilot subscription models via device code',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'copilot-gpt-4o', name: 'GPT-4o (Copilot)', contextWindow: 128000, isDefault: true },
      { id: 'copilot-claude-sonnet', name: 'Claude Sonnet (Copilot)', contextWindow: 200000 },
    ],
    defaultModel: 'copilot-gpt-4o',
  },
  {
    id: 'openrouter',
    name: 'OpenRouter',
    icon: Zap as IconComponent,
    description: 'Access 300+ models via single API',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'anthropic/claude-sonnet-4.5',
        name: 'Claude Sonnet 4.5',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'anthropic/claude-opus-4.5', name: 'Claude Opus 4.5', contextWindow: 200000 },
      { id: 'openai/gpt-5.2', name: 'GPT-5.2', contextWindow: 196000 },
      { id: 'openai/o3', name: 'o3', contextWindow: 200000 },
      { id: 'google/gemini-3-pro-preview', name: 'Gemini 3 Pro', contextWindow: 2000000 },
      { id: 'google/gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000 },
      { id: 'deepseek/deepseek-r1', name: 'DeepSeek R1', contextWindow: 64000 },
      { id: 'meta-llama/llama-4-405b', name: 'Llama 4 405B', contextWindow: 256000 },
      { id: 'mistralai/codestral-2501', name: 'Codestral', contextWindow: 256000 },
      { id: 'x-ai/grok-3', name: 'Grok 3', contextWindow: 131072 },
    ],
    defaultModel: 'anthropic/claude-sonnet-4.5',
  },
  {
    id: 'xai',
    name: 'xAI',
    icon: Flame as IconComponent,
    description: 'Grok models for reasoning and code',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'grok-3', name: 'Grok 3', contextWindow: 131072, isDefault: true },
      { id: 'grok-3-mini', name: 'Grok 3 Mini', contextWindow: 131072 },
    ],
    defaultModel: 'grok-3',
  },
  {
    id: 'mistral',
    name: 'Mistral',
    icon: Cloud as IconComponent,
    description: 'European AI with code-specialized models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'mistral-large-latest', name: 'Mistral Large', contextWindow: 128000, isDefault: true },
      { id: 'codestral-latest', name: 'Codestral', contextWindow: 256000 },
      { id: 'mistral-small-latest', name: 'Mistral Small', contextWindow: 128000 },
    ],
    defaultModel: 'mistral-large-latest',
  },
  {
    id: 'groq',
    name: 'Groq',
    icon: Zap as IconComponent,
    description: 'Ultra-fast inference on open models',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'llama-3.3-70b-versatile',
        name: 'Llama 3.3 70B',
        contextWindow: 128000,
        isDefault: true,
      },
      { id: 'mixtral-8x7b-32768', name: 'Mixtral 8x7B', contextWindow: 32768 },
      { id: 'gemma2-9b-it', name: 'Gemma 2 9B', contextWindow: 8192 },
    ],
    defaultModel: 'llama-3.3-70b-versatile',
  },
  {
    id: 'deepseek',
    name: 'DeepSeek',
    icon: Braces as IconComponent,
    description: 'Open-weight reasoning and coding models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'deepseek-chat', name: 'DeepSeek V3', contextWindow: 64000, isDefault: true },
      { id: 'deepseek-reasoner', name: 'DeepSeek R1', contextWindow: 64000 },
    ],
    defaultModel: 'deepseek-chat',
  },
  {
    id: 'cohere',
    name: 'Cohere',
    icon: Shield as IconComponent,
    description: 'Enterprise RAG and command models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'command-r-plus', name: 'Command R+', contextWindow: 128000, isDefault: true },
      { id: 'command-r', name: 'Command R', contextWindow: 128000 },
    ],
    defaultModel: 'command-r-plus',
  },
  {
    id: 'together',
    name: 'Together',
    icon: Cloud as IconComponent,
    description: 'Open-source models with fast inference',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
        name: 'Llama 3.3 70B',
        contextWindow: 128000,
        isDefault: true,
      },
      { id: 'mistralai/Mixtral-8x7B-Instruct-v0.1', name: 'Mixtral 8x7B', contextWindow: 32768 },
      { id: 'Qwen/Qwen2.5-Coder-32B-Instruct', name: 'Qwen 2.5 Coder 32B', contextWindow: 32768 },
    ],
    defaultModel: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
  },
  {
    id: 'kimi',
    name: 'Kimi',
    icon: Bot as IconComponent,
    description: 'Moonshot AI models with long context',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'moonshot-v1-128k', name: 'Kimi v1 128K', contextWindow: 128000, isDefault: true },
    ],
    defaultModel: 'moonshot-v1-128k',
  },
  {
    id: 'glm',
    name: 'Zhipu (GLM)',
    icon: Cpu as IconComponent,
    description: 'Chinese AI with bilingual capabilities',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'glm-4-plus', name: 'GLM-4 Plus', contextWindow: 128000, isDefault: true },
      { id: 'glm-4-flash', name: 'GLM-4 Flash', contextWindow: 128000 },
    ],
    defaultModel: 'glm-4-plus',
  },
  {
    id: 'ollama',
    name: 'Ollama',
    icon: Bot as IconComponent,
    description: 'Run models locally',
    enabled: false,
    status: 'disconnected',
    baseUrl: 'http://localhost:11434',
    models: [
      { id: 'llama3.3:latest', name: 'Llama 3.3', contextWindow: 128000 },
      { id: 'deepseek-r1:latest', name: 'DeepSeek R1', contextWindow: 64000 },
      { id: 'qwen2.5-coder:latest', name: 'Qwen 2.5 Coder', contextWindow: 32000 },
      { id: 'codestral:latest', name: 'Codestral', contextWindow: 32000 },
      { id: 'mistral:latest', name: 'Mistral', contextWindow: 32000 },
      { id: 'phi4:latest', name: 'Phi-4', contextWindow: 16000 },
    ],
  },
]
