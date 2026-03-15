use std::collections::BTreeSet;
use std::fmt;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

use tokio::net::lookup_host;
use url::Url;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResolvedTargetCategory {
    Public,
    Loopback,
    Private,
    LinkLocal,
    Unspecified,
    Metadata,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ValidatedOutboundTarget {
    pub scheme: String,
    pub host: String,
    pub port: u16,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OutboundTargetError {
    message: String,
}

impl OutboundTargetError {
    fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl fmt::Display for OutboundTargetError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for OutboundTargetError {}

pub fn validate_outbound_url_str(
    url: &str,
) -> Result<ValidatedOutboundTarget, OutboundTargetError> {
    let parsed = Url::parse(url)
        .map_err(|e| OutboundTargetError::new(format!("invalid URL '{url}': {e}")))?;
    validate_outbound_url(&parsed)
}

pub fn validate_outbound_url(url: &Url) -> Result<ValidatedOutboundTarget, OutboundTargetError> {
    let scheme = url.scheme().to_ascii_lowercase();
    if scheme != "http" && scheme != "https" {
        return Err(OutboundTargetError::new(format!(
            "only http:// and https:// URLs are allowed, got scheme '{scheme}'"
        )));
    }

    let host = url
        .host_str()
        .ok_or_else(|| OutboundTargetError::new("URL has no host"))?
        .to_ascii_lowercase();

    let normalized_host = host.trim_end_matches('.');
    let canonical_host = normalized_host
        .trim_start_matches('[')
        .trim_end_matches(']');
    if normalized_host == "localhost" || normalized_host.ends_with(".localhost") {
        return Err(OutboundTargetError::new(format!(
            "blocked outbound target '{host}' (localhost)"
        )));
    }

    if is_metadata_hostname(canonical_host) {
        return Err(OutboundTargetError::new(format!(
            "blocked outbound target '{host}' (cloud metadata endpoint)"
        )));
    }

    let port = url.port_or_known_default().ok_or_else(|| {
        OutboundTargetError::new(format!(
            "URL missing known default port for scheme '{scheme}'"
        ))
    })?;

    if let Ok(ip) = canonical_host.parse::<IpAddr>() {
        let category = categorize_ip(ip);
        if category != ResolvedTargetCategory::Public {
            return Err(OutboundTargetError::new(format!(
                "blocked outbound target '{host}' with unsafe IP category ({})",
                category_label(category)
            )));
        }
    }

    Ok(ValidatedOutboundTarget { scheme, host, port })
}

pub async fn resolve_public_socket_addrs(
    host: &str,
    port: u16,
) -> Result<Vec<SocketAddr>, OutboundTargetError> {
    let canonical_host = host.trim_start_matches('[').trim_end_matches(']');

    if let Ok(ip) = canonical_host.parse::<IpAddr>() {
        let category = categorize_ip(ip);
        if category != ResolvedTargetCategory::Public {
            return Err(OutboundTargetError::new(format!(
                "blocked outbound target '{host}' with unsafe IP category ({})",
                category_label(category)
            )));
        }
        return Ok(vec![SocketAddr::new(ip, port)]);
    }

    let resolved = lookup_host((canonical_host, port)).await.map_err(|e| {
        OutboundTargetError::new(format!("failed to resolve host '{host}:{port}': {e}"))
    })?;

    let mut unique = BTreeSet::new();
    for addr in resolved {
        let category = categorize_ip(addr.ip());
        if category != ResolvedTargetCategory::Public {
            return Err(OutboundTargetError::new(format!(
                "blocked outbound target '{host}' resolved to unsafe IP {} ({})",
                addr.ip(),
                category_label(category)
            )));
        }
        unique.insert(addr);
    }

    if unique.is_empty() {
        return Err(OutboundTargetError::new(format!(
            "host '{host}:{port}' resolved to no addresses"
        )));
    }

    Ok(unique.into_iter().collect())
}

fn categorize_ip(ip: IpAddr) -> ResolvedTargetCategory {
    if is_metadata_ip(ip) {
        return ResolvedTargetCategory::Metadata;
    }

    match ip {
        IpAddr::V4(v4) => {
            if v4.is_loopback() {
                ResolvedTargetCategory::Loopback
            } else if v4.is_private() {
                ResolvedTargetCategory::Private
            } else if v4.is_link_local() {
                ResolvedTargetCategory::LinkLocal
            } else if v4.is_unspecified() {
                ResolvedTargetCategory::Unspecified
            } else {
                ResolvedTargetCategory::Public
            }
        }
        IpAddr::V6(v6) => {
            if let Some(mapped_v4) = v6.to_ipv4_mapped() {
                return categorize_ip(IpAddr::V4(mapped_v4));
            }

            if v6.is_loopback() {
                ResolvedTargetCategory::Loopback
            } else if v6.is_unique_local() {
                ResolvedTargetCategory::Private
            } else if v6.is_unicast_link_local() {
                ResolvedTargetCategory::LinkLocal
            } else if v6.is_unspecified() {
                ResolvedTargetCategory::Unspecified
            } else {
                ResolvedTargetCategory::Public
            }
        }
    }
}

fn is_metadata_hostname(host: &str) -> bool {
    matches!(host, "metadata" | "metadata.google.internal")
}

fn is_metadata_ip(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => {
            v4 == Ipv4Addr::new(169, 254, 169, 254)
                || v4 == Ipv4Addr::new(169, 254, 170, 2)
                || v4 == Ipv4Addr::new(100, 100, 100, 200)
                || v4 == Ipv4Addr::new(168, 63, 129, 16)
        }
        IpAddr::V6(v6) => v6 == Ipv6Addr::new(0xfd00, 0xec2, 0, 0, 0, 0, 0, 0x254),
    }
}

fn category_label(category: ResolvedTargetCategory) -> &'static str {
    match category {
        ResolvedTargetCategory::Public => "public",
        ResolvedTargetCategory::Loopback => "loopback",
        ResolvedTargetCategory::Private => "private",
        ResolvedTargetCategory::LinkLocal => "link-local",
        ResolvedTargetCategory::Unspecified => "unspecified",
        ResolvedTargetCategory::Metadata => "metadata",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blocks_localhost_and_loopback() {
        assert!(validate_outbound_url_str("http://localhost/secret").is_err());
        assert!(validate_outbound_url_str("http://sub.localhost/path").is_err());
        assert!(validate_outbound_url_str("http://127.0.0.1/secret").is_err());
        assert!(validate_outbound_url_str("http://[::1]/secret").is_err());
    }

    #[test]
    fn blocks_private_and_link_local_ranges() {
        assert!(validate_outbound_url_str("http://10.0.0.1").is_err());
        assert!(validate_outbound_url_str("http://192.168.1.10").is_err());
        assert!(validate_outbound_url_str("http://172.16.1.5").is_err());
        assert!(validate_outbound_url_str("http://169.254.1.1").is_err());
        assert!(validate_outbound_url_str("http://[fe80::1]").is_err());
        assert!(validate_outbound_url_str("http://[fd00::1]").is_err());
        assert!(validate_outbound_url_str("http://[::ffff:127.0.0.1]").is_err());
    }

    #[test]
    fn blocks_cloud_metadata_endpoints() {
        assert!(validate_outbound_url_str("http://169.254.169.254/latest").is_err());
        assert!(validate_outbound_url_str("http://169.254.170.2/v2/credentials").is_err());
        assert!(validate_outbound_url_str("http://100.100.100.200/latest/meta-data").is_err());
        assert!(validate_outbound_url_str("http://168.63.129.16/metadata").is_err());
        assert!(validate_outbound_url_str("http://metadata.google.internal/").is_err());
    }

    #[test]
    fn allows_public_ip_targets() {
        assert!(validate_outbound_url_str("https://1.1.1.1").is_ok());
        assert!(validate_outbound_url_str("http://8.8.8.8/dns-query").is_ok());
    }

    #[tokio::test]
    async fn resolve_public_ip_target() {
        let addrs = resolve_public_socket_addrs("1.1.1.1", 443)
            .await
            .expect("public IP should resolve");
        assert!(!addrs.is_empty());
    }

    #[tokio::test]
    async fn reject_private_ip_target_during_resolution() {
        let err = resolve_public_socket_addrs("10.0.0.1", 80)
            .await
            .expect_err("private IP should be rejected");
        assert!(err.to_string().contains("unsafe IP category"));
    }

    #[test]
    fn blocks_non_http_schemes() {
        assert!(validate_outbound_url_str("file:///etc/passwd").is_err());
        assert!(validate_outbound_url_str("ftp://example.com/file").is_err());
    }
}
