export interface AgentProfile {
  name: string
  model: string
  tools: string[]
  instructions: string
  skills: string[]
}

export const BUILT_IN_PROFILES: AgentProfile[] = [
  {
    name: 'researcher',
    model: 'claude-3-5-sonnet-latest',
    tools: ['read_file', 'glob', 'grep', 'webfetch', 'websearch'],
    instructions: 'Focus on information gathering, synthesis, and evidence-based answers.',
    skills: ['researcher'],
  },
  {
    name: 'coder',
    model: 'claude-3-5-sonnet-latest',
    tools: ['*'],
    instructions: 'Implement tasks directly with tests and concise rationale.',
    skills: ['refactorer', 'test-writer'],
  },
  {
    name: 'reviewer',
    model: 'claude-3-5-sonnet-latest',
    tools: ['read_file', 'glob', 'grep', 'bash'],
    instructions: 'Review code for correctness, safety, and maintainability.',
    skills: ['code-reviewer'],
  },
]
