export default {
  extends: ['@commitlint/config-conventional'],
  rules: {
    // Type must be one of these
    'type-enum': [
      2,
      'always',
      [
        'feat', // New feature
        'fix', // Bug fix
        'docs', // Documentation
        'style', // Formatting (no code change)
        'refactor', // Code change (no feat/fix)
        'perf', // Performance improvement
        'test', // Adding tests
        'build', // Build system or dependencies
        'ci', // CI configuration
        'chore', // Other changes
        'revert', // Revert previous commit
      ],
    ],
    // Subject must not be empty
    'subject-empty': [2, 'never'],
    // Type must not be empty
    'type-empty': [2, 'never'],
    // Subject max length
    'subject-max-length': [2, 'always', 72],
  },
}
