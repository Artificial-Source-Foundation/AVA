/**
 * MCP Server Presets
 * Curated list of popular MCP servers for quick setup.
 */

import type { MCPTransportType } from '../stores/settings/settings-types'

export interface MCPPreset {
  name: string
  description: string
  type: MCPTransportType
  command?: string
  args?: string[]
  url?: string
  category: string
}

export const MCP_PRESETS: MCPPreset[] = [
  {
    name: 'filesystem',
    description: 'Read, write, and manage local files',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-filesystem', '.'],
    category: 'Filesystem',
  },
  {
    name: 'github',
    description: 'GitHub API integration (issues, PRs, repos)',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-github'],
    category: 'Dev Tools',
  },
  {
    name: 'sqlite',
    description: 'Query and manage SQLite databases',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-sqlite', 'database.db'],
    category: 'Database',
  },
  {
    name: 'postgres',
    description: 'Query PostgreSQL databases',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-postgres'],
    category: 'Database',
  },
  {
    name: 'brave-search',
    description: 'Web search via Brave Search API',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-brave-search'],
    category: 'Search',
  },
  {
    name: 'playwright',
    description: 'Browser automation and web app testing via Playwright',
    type: 'stdio',
    command: 'pnpm',
    args: ['exec', 'playwright-mcp', '--headless', '--allowed-origins=*'],
    category: 'Dev Tools',
  },
  {
    name: 'puppeteer',
    description: 'Browser automation and web scraping',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-puppeteer'],
    category: 'Dev Tools',
  },
  {
    name: 'memory',
    description: 'Persistent key-value memory store',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-memory'],
    category: 'AI',
  },
  {
    name: 'sequential-thinking',
    description: 'Step-by-step reasoning and planning',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-sequential-thinking'],
    category: 'AI',
  },
  {
    name: 'slack',
    description: 'Slack workspace integration',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-slack'],
    category: 'Dev Tools',
  },
  {
    name: 'fetch',
    description: 'HTTP fetch and web page content extraction',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-fetch'],
    category: 'Search',
  },
  {
    name: 'git',
    description: 'Git repository operations',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-git'],
    category: 'Dev Tools',
  },
  {
    name: 'everything',
    description: 'Reference MCP server with all capabilities',
    type: 'stdio',
    command: 'npx',
    args: ['-y', '@modelcontextprotocol/server-everything'],
    category: 'Dev Tools',
  },
]

export const MCP_CATEGORIES = [...new Set(MCP_PRESETS.map((p) => p.category))]
