import type { PluginManifest } from '../../types'

const MOCK_PLUGIN_CATALOG: PluginManifest[] = [
  {
    id: 'prettier-formatter',
    name: 'Prettier Formatter',
    description: 'Formats JS/TS files with consistent style rules.',
    category: 'productivity',
    version: '1.2.0',
    author: 'AVA Labs',
    featured: true,
    hasSettings: true,
    tags: ['format', 'code-style', 'prettier'],
  },
  {
    id: 'eslint-guardian',
    name: 'ESLint Guardian',
    description: 'Runs lint checks and suggests fixes before commit.',
    category: 'quality',
    version: '0.9.4',
    author: 'AVA Labs',
    featured: true,
    hasSettings: true,
    tags: ['lint', 'quality', 'eslint'],
  },
  {
    id: 'vitest-accelerator',
    name: 'Vitest Accelerator',
    description: 'Runs targeted tests from changed files quickly.',
    category: 'testing',
    version: '0.5.1',
    author: 'Community',
    featured: false,
    hasSettings: false,
    tags: ['test', 'vitest'],
  },
  {
    id: 'git-smart-commits',
    name: 'Git Smart Commits',
    description: 'Generates conventional commit messages from staged changes.',
    category: 'git',
    version: '1.0.3',
    author: 'Community',
    featured: false,
    hasSettings: true,
    tags: ['git', 'commit', 'workflow'],
  },
  {
    id: 'ci-workflow-helper',
    name: 'CI Workflow Helper',
    description: 'Creates and validates CI templates for GitHub Actions.',
    category: 'automation',
    version: '0.3.8',
    author: 'AVA Labs',
    featured: true,
    hasSettings: false,
    tags: ['ci', 'automation', 'github-actions'],
  },
  {
    id: 'cloudflare-toolkit',
    name: 'Cloudflare Toolkit',
    description: 'Utilities for Workers, Wrangler, and deployment checks.',
    category: 'integrations',
    version: '2.1.0',
    author: 'Community',
    featured: false,
    hasSettings: true,
    tags: ['cloudflare', 'workers', 'wrangler'],
  },
]

export async function listPluginCatalog(): Promise<PluginManifest[]> {
  return Promise.resolve(MOCK_PLUGIN_CATALOG)
}
