import { describe, expect, it } from 'vitest'
import { extractSymbols, getSupportedLanguages } from './symbol-extractor.js'

describe('extractSymbols', () => {
  describe('TypeScript', () => {
    it('extracts functions', () => {
      const code = `
export function greet(name: string): string {
  return 'hello ' + name
}

async function fetchData() {
  return await fetch('/api')
}
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      const fns = symbols.filter((s) => s.kind === 'function')
      expect(fns).toHaveLength(2)
      expect(fns.map((s) => s.name)).toContain('greet')
      expect(fns.map((s) => s.name)).toContain('fetchData')
    })

    it('extracts classes and interfaces', () => {
      const code = `
export class UserService {
  private db: Database

  getUser(id: string) {
    return this.db.find(id)
  }
}

export interface IUserRepository {
  find(id: string): User
}
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      expect(symbols.find((s) => s.name === 'UserService')?.kind).toBe('class')
      expect(symbols.find((s) => s.name === 'IUserRepository')?.kind).toBe('interface')
      expect(symbols.find((s) => s.name === 'getUser')?.kind).toBe('method')
    })

    it('extracts types and enums', () => {
      const code = `
export type UserId = string
export enum Role {
  Admin,
  User,
}
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      expect(symbols.find((s) => s.name === 'UserId')?.kind).toBe('type')
      expect(symbols.find((s) => s.name === 'Role')?.kind).toBe('enum')
    })

    it('extracts exported variables', () => {
      const code = `
export const MAX_SIZE = 1000
const logger = createLogger('test')
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      const vars = symbols.filter((s) => s.kind === 'variable')
      expect(vars.map((s) => s.name)).toContain('MAX_SIZE')
      expect(vars.map((s) => s.name)).toContain('logger')
    })

    it('includes line numbers', () => {
      const code = `// comment
export function hello() {}
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      const fn = symbols.find((s) => s.name === 'hello')
      expect(fn?.line).toBe(2)
    })
  })

  describe('Python', () => {
    it('extracts functions and classes', () => {
      const code = `
def greet(name):
    return f"hello {name}"

class UserService:
    def get_user(self, id):
        pass

async def fetch_data():
    pass
`
      const symbols = extractSymbols(code, 'python', 'test.py')
      expect(symbols.find((s) => s.name === 'greet')?.kind).toBe('function')
      expect(symbols.find((s) => s.name === 'UserService')?.kind).toBe('class')
      expect(symbols.find((s) => s.name === 'get_user')?.kind).toBe('method')
      expect(symbols.find((s) => s.name === 'fetch_data')?.kind).toBe('function')
    })
  })

  describe('Rust', () => {
    it('extracts functions, structs, traits', () => {
      const code = `
pub fn greet(name: &str) -> String {
    format!("hello {}", name)
}

pub struct User {
    name: String,
}

pub trait Repository {
    fn find(&self, id: &str) -> Option<User>;
}

pub enum Color {
    Red,
    Blue,
}

impl User {
    pub fn new(name: String) -> Self {
        User { name }
    }
}
`
      const symbols = extractSymbols(code, 'rust', 'test.rs')
      expect(symbols.find((s) => s.name === 'greet')?.kind).toBe('function')
      expect(symbols.find((s) => s.name === 'User')?.kind).toBe('class')
      expect(symbols.find((s) => s.name === 'Repository')?.kind).toBe('interface')
      expect(symbols.find((s) => s.name === 'Color')?.kind).toBe('enum')
    })
  })

  describe('Go', () => {
    it('extracts functions, structs, interfaces', () => {
      const code = `
func Greet(name string) string {
    return "hello " + name
}

type User struct {
    Name string
}

type Repository interface {
    Find(id string) *User
}

func (u *User) GetName() string {
    return u.Name
}
`
      const symbols = extractSymbols(code, 'go', 'test.go')
      expect(symbols.find((s) => s.name === 'Greet')?.kind).toBe('function')
      expect(symbols.find((s) => s.name === 'User')?.kind).toBe('class')
      expect(symbols.find((s) => s.name === 'Repository')?.kind).toBe('interface')
      expect(symbols.find((s) => s.name === 'GetName')?.kind).toBe('method')
    })
  })

  describe('edge cases', () => {
    it('returns empty for unsupported language', () => {
      const symbols = extractSymbols('code', 'html', 'test.html')
      expect(symbols).toHaveLength(0)
    })

    it('returns empty for very large files', () => {
      const code = 'x'.repeat(600_000)
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      expect(symbols).toHaveLength(0)
    })

    it('skips keyword names', () => {
      const code = `
if (condition) {
  return value
}
`
      const symbols = extractSymbols(code, 'typescript', 'test.ts')
      const names = symbols.map((s) => s.name)
      expect(names).not.toContain('if')
      expect(names).not.toContain('return')
    })
  })
})

describe('getSupportedLanguages', () => {
  it('returns supported languages', () => {
    const langs = getSupportedLanguages()
    expect(langs).toContain('typescript')
    expect(langs).toContain('python')
    expect(langs).toContain('rust')
    expect(langs).toContain('go')
  })
})
