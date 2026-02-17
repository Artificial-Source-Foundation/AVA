import type { PluginCatalogItem } from '../../types/plugin'

export interface PluginManifest extends PluginCatalogItem {
  author: string
  featured: boolean
  hasSettings: boolean
  tags: string[]
}

const MOCK_PLUGIN_CATALOG: PluginManifest[] = [
  {
    id: 'prettier-formatter',
    name: 'Prettier Formatter',
    description: 'Formats JS/TS files with consistent style rules.',
    category: 'quality',
    version: '1.2.0',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added Biome compat mode',
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
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Fixed flat config support',
    author: 'AVA Labs',
    featured: true,
    hasSettings: true,
    tags: ['lint', 'quality', 'eslint'],
  },
  {
    id: 'vitest-accelerator',
    name: 'Vitest Accelerator',
    description: 'Runs targeted tests from changed files quickly.',
    category: 'workflow',
    version: '0.5.1',
    source: 'community',
    trust: 'reviewed',
    changelogSummary: 'Initial release',
    author: 'Community',
    featured: false,
    hasSettings: false,
    tags: ['test', 'vitest'],
  },
  {
    id: 'git-smart-commits',
    name: 'Git Smart Commits',
    description: 'Generates conventional commit messages from staged changes.',
    category: 'workflow',
    version: '1.0.3',
    source: 'community',
    trust: 'reviewed',
    changelogSummary: 'Added scope detection',
    author: 'Community',
    featured: false,
    hasSettings: true,
    tags: ['git', 'commit', 'workflow'],
  },
  {
    id: 'ci-workflow-helper',
    name: 'CI Workflow Helper',
    description: 'Creates and validates CI templates for GitHub Actions.',
    category: 'integration',
    version: '0.3.8',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added Node 22 matrix',
    author: 'AVA Labs',
    featured: true,
    hasSettings: false,
    tags: ['ci', 'automation', 'github-actions'],
  },
  {
    id: 'cloudflare-toolkit',
    name: 'Cloudflare Toolkit',
    description: 'Utilities for Workers, Wrangler, and deployment checks.',
    category: 'integration',
    version: '2.1.0',
    source: 'community',
    trust: 'reviewed',
    changelogSummary: 'Added Pages deploy support',
    author: 'Community',
    featured: false,
    hasSettings: true,
    tags: ['cloudflare', 'workers', 'wrangler'],
  },
]

export async function listPluginCatalog(): Promise<PluginManifest[]> {
  return Promise.resolve(MOCK_PLUGIN_CATALOG)
}
