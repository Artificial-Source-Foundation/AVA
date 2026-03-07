use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use tokio::sync::RwLock;

/// Session-scoped connection pool that reuses `reqwest::Client` instances
/// across providers sharing the same base URL.
pub struct ConnectionPool {
    clients: RwLock<HashMap<String, Arc<reqwest::Client>>>,
    connect_timeout: Duration,
    request_timeout: Duration,
    pool_max_idle_per_host: usize,
    keep_alive: Duration,
}

#[derive(Debug, Clone)]
pub struct PoolStats {
    pub active_clients: usize,
    pub base_urls: Vec<String>,
}

impl Default for ConnectionPool {
    fn default() -> Self {
        Self::new()
    }
}

impl ConnectionPool {
    pub fn new() -> Self {
        Self {
            clients: RwLock::new(HashMap::new()),
            connect_timeout: Duration::from_secs(10),
            request_timeout: Duration::from_secs(120),
            pool_max_idle_per_host: 10,
            keep_alive: Duration::from_secs(90),
        }
    }

    pub fn with_timeouts(
        connect_timeout: Duration,
        request_timeout: Duration,
    ) -> Self {
        Self {
            connect_timeout,
            request_timeout,
            ..Self::new()
        }
    }

    /// Get or create a client for the given base URL.
    /// Clients are reused across providers with the same base URL.
    pub async fn get_client(&self, base_url: &str) -> Arc<reqwest::Client> {
        // Fast path: read lock
        if let Some(client) = self.clients.read().await.get(base_url) {
            return client.clone();
        }

        // Slow path: write lock + create
        let mut clients = self.clients.write().await;
        // Double-check after acquiring write lock
        if let Some(client) = clients.get(base_url) {
            return client.clone();
        }

        let client = Arc::new(
            reqwest::Client::builder()
                .connect_timeout(self.connect_timeout)
                .timeout(self.request_timeout)
                .pool_max_idle_per_host(self.pool_max_idle_per_host)
                .pool_idle_timeout(self.keep_alive)
                .build()
                .expect("failed to build reqwest client"),
        );

        clients.insert(base_url.to_string(), client.clone());
        client
    }

    pub async fn stats(&self) -> PoolStats {
        let clients = self.clients.read().await;
        PoolStats {
            active_clients: clients.len(),
            base_urls: clients.keys().cloned().collect(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn same_base_url_returns_same_client() {
        let pool = ConnectionPool::new();

        let c1 = pool.get_client("https://api.openai.com").await;
        let c2 = pool.get_client("https://api.openai.com").await;

        assert!(Arc::ptr_eq(&c1, &c2));
    }

    #[tokio::test]
    async fn different_base_urls_return_different_clients() {
        let pool = ConnectionPool::new();

        let c1 = pool.get_client("https://api.openai.com").await;
        let c2 = pool.get_client("https://api.anthropic.com").await;

        assert!(!Arc::ptr_eq(&c1, &c2));
    }

    #[tokio::test]
    async fn stats_tracks_active_clients() {
        let pool = ConnectionPool::new();

        let stats = pool.stats().await;
        assert_eq!(stats.active_clients, 0);

        pool.get_client("https://api.openai.com").await;
        pool.get_client("https://openrouter.ai/api").await;

        let stats = pool.stats().await;
        assert_eq!(stats.active_clients, 2);
        assert!(stats.base_urls.contains(&"https://api.openai.com".to_string()));
    }

    #[tokio::test]
    async fn custom_timeouts() {
        let pool = ConnectionPool::with_timeouts(
            Duration::from_secs(5),
            Duration::from_secs(60),
        );

        let client = pool.get_client("https://example.com").await;
        assert!(Arc::strong_count(&client) >= 2); // pool + local
    }

    #[tokio::test]
    async fn pool_is_send_and_sync() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<ConnectionPool>();
    }
}
