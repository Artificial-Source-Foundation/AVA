/**
 * Stub: @tauri-apps/plugin-sql
 */
export default class Database {
  static async load() {
    return new Database()
  }
  async execute() {
    return { rowsAffected: 0, lastInsertId: 0 }
  }
  async select() {
    return []
  }
  async close() {}
}
