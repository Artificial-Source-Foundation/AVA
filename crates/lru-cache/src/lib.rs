use std::collections::HashMap;
use std::collections::VecDeque;
use std::hash::Hash;

pub struct LruCache<K, V> {
    capacity: usize,
    map: HashMap<K, V>,
    order: VecDeque<K>,
}

impl<K: Eq + Hash + Clone, V: Clone> LruCache<K, V> {
    pub fn new(capacity: usize) -> Self {
        assert!(capacity > 0, "LruCache capacity must be greater than 0");
        Self {
            capacity,
            map: HashMap::with_capacity(capacity),
            order: VecDeque::with_capacity(capacity),
        }
    }

    pub fn get(&mut self, key: &K) -> Option<V> {
        if self.map.contains_key(key) {
            self.touch(key);
            self.map.get(key).cloned()
        } else {
            None
        }
    }

    pub fn put(&mut self, key: K, value: V) {
        if self.map.contains_key(&key) {
            self.touch(&key);
            self.map.insert(key, value);
        } else {
            if self.map.len() >= self.capacity {
                if let Some(evicted) = self.order.pop_front() {
                    self.map.remove(&evicted);
                }
            }
            self.order.push_back(key.clone());
            self.map.insert(key, value);
        }
    }

    fn touch(&mut self, key: &K) {
        if let Some(pos) = self.order.iter().position(|k| k == key) {
            self.order.remove(pos);
            self.order.push_back(key.clone());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_put_and_get() {
        let mut cache = LruCache::new(2);
        cache.put("a", 1);
        cache.put("b", 2);
        assert_eq!(cache.get(&"a"), Some(1));
        assert_eq!(cache.get(&"b"), Some(2));
    }

    #[test]
    fn test_eviction() {
        let mut cache = LruCache::new(2);
        cache.put("a", 1);
        cache.put("b", 2);
        cache.put("c", 3); // evicts "a"
        assert_eq!(cache.get(&"a"), None);
        assert_eq!(cache.get(&"b"), Some(2));
        assert_eq!(cache.get(&"c"), Some(3));
    }

    #[test]
    fn test_get_promotes_key() {
        let mut cache = LruCache::new(2);
        cache.put("a", 1);
        cache.put("b", 2);
        cache.get(&"a"); // promotes "a", so "b" is now LRU
        cache.put("c", 3); // evicts "b"
        assert_eq!(cache.get(&"a"), Some(1));
        assert_eq!(cache.get(&"b"), None);
        assert_eq!(cache.get(&"c"), Some(3));
    }

    #[test]
    fn test_put_updates_existing() {
        let mut cache = LruCache::new(2);
        cache.put("a", 1);
        cache.put("a", 10);
        assert_eq!(cache.get(&"a"), Some(10));
        // size should still be 1
        cache.put("b", 2);
        cache.put("c", 3); // evicts "a" if not promoted, but "a" was touched by put
        // "a" was promoted by the second put, so "b" is LRU
        assert_eq!(cache.get(&"a"), Some(10));
        assert_eq!(cache.get(&"b"), None);
    }

    #[test]
    fn test_missing_key() {
        let mut cache = LruCache::<&str, i32>::new(2);
        assert_eq!(cache.get(&"x"), None);
    }

    #[test]
    fn test_capacity_one() {
        let mut cache = LruCache::new(1);
        cache.put("a", 1);
        assert_eq!(cache.get(&"a"), Some(1));
        cache.put("b", 2);
        assert_eq!(cache.get(&"a"), None);
        assert_eq!(cache.get(&"b"), Some(2));
    }

    #[test]
    #[should_panic(expected = "capacity must be greater than 0")]
    fn test_zero_capacity_panics() {
        let _cache = LruCache::<String, i32>::new(0);
    }

    #[test]
    fn test_integer_keys() {
        let mut cache = LruCache::new(3);
        for i in 0..5 {
            cache.put(i, i * 100);
        }
        // 0, 1 evicted; 2, 3, 4 remain
        assert_eq!(cache.get(&0), None);
        assert_eq!(cache.get(&1), None);
        assert_eq!(cache.get(&2), Some(200));
        assert_eq!(cache.get(&3), Some(300));
        assert_eq!(cache.get(&4), Some(400));
    }
}
