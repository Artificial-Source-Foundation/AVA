/**
 * Stub: @tauri-apps/plugin-shell
 */
export const Command = class {
  static create() {
    return new Command()
  }
  async execute() {
    return { code: 0, stdout: '', stderr: '' }
  }
}
