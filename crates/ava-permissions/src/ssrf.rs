//! SSRF (Server-Side Request Forgery) guard.
//!
//! Validates URLs by resolving DNS and blocking requests to private/internal IP ranges,
//! while allowing loopback addresses for local development.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, ToSocketAddrs};

use thiserror::Error;

/// Errors returned by the SSRF guard.
#[derive(Debug, Error, PartialEq, Eq)]
pub enum SsrfError {
    #[error("invalid URL: {0}")]
    InvalidUrl(String),

    #[error("URL has no host")]
    NoHost,

    #[error("blocked private IP {ip} for host '{host}'")]
    PrivateIp { ip: String, host: String },

    #[error("blocked unspecified address for host '{host}'")]
    UnspecifiedAddress { host: String },

    #[error("DNS resolution failed for '{host}': {reason}")]
    DnsResolutionFailed { host: String, reason: String },
}

/// Check a URL string for SSRF vulnerabilities.
///
/// Parses the URL, extracts the host, resolves DNS, and verifies that none of the
/// resolved IP addresses fall into blocked private ranges.
///
/// # Allowed
/// - Public IP addresses (any IP not in the blocked ranges below)
/// - Loopback: `127.0.0.0/8` and `::1` (for local development)
///
/// # Blocked
/// - `0.0.0.0/8` (unspecified)
/// - `10.0.0.0/8` (private)
/// - `100.64.0.0/10` (carrier-grade NAT)
/// - `169.254.0.0/16` (link-local)
/// - `172.16.0.0/12` (private)
/// - `192.168.0.0/16` (private)
/// - `::` (IPv6 unspecified)
/// - `fc00::/7` (IPv6 unique local)
/// - `fe80::/10` (IPv6 link-local)
/// - `::ffff:<blocked-v4>` (IPv4-mapped IPv6 with blocked v4 address)
pub fn check_ssrf(url: &str) -> Result<(), SsrfError> {
    // Parse URL to extract host and port
    let parsed = url::Url::parse(url).map_err(|e| SsrfError::InvalidUrl(e.to_string()))?;

    let host = parsed.host_str().ok_or(SsrfError::NoHost)?.to_string();

    let port = parsed.port_or_known_default().unwrap_or(80);

    // If the host is already an IP literal, check it directly
    let canonical = host.trim_start_matches('[').trim_end_matches(']');
    if let Ok(ip) = canonical.parse::<IpAddr>() {
        return check_ip(ip, &host);
    }

    // Resolve DNS and check all resulting addresses
    let addr_str = format!("{canonical}:{port}");
    let addrs: Vec<_> = addr_str
        .to_socket_addrs()
        .map_err(|e| SsrfError::DnsResolutionFailed {
            host: host.clone(),
            reason: e.to_string(),
        })?
        .collect();

    if addrs.is_empty() {
        return Err(SsrfError::DnsResolutionFailed {
            host,
            reason: "resolved to no addresses".to_string(),
        });
    }

    for addr in addrs {
        check_ip(addr.ip(), &host)?;
    }

    Ok(())
}

/// Check a single IP address against the SSRF blocklist.
fn check_ip(ip: IpAddr, host: &str) -> Result<(), SsrfError> {
    match ip {
        IpAddr::V4(v4) => check_ipv4(v4, host),
        IpAddr::V6(v6) => check_ipv6(v6, host),
    }
}

/// Check an IPv4 address against blocked ranges.
fn check_ipv4(ip: Ipv4Addr, host: &str) -> Result<(), SsrfError> {
    let octets = ip.octets();

    // Allow loopback (127.0.0.0/8)
    if octets[0] == 127 {
        return Ok(());
    }

    // Block 0.0.0.0/8 (unspecified / "this" network)
    if octets[0] == 0 {
        return Err(SsrfError::UnspecifiedAddress {
            host: host.to_string(),
        });
    }

    // Block 10.0.0.0/8
    if octets[0] == 10 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    // Block 100.64.0.0/10 (carrier-grade NAT)
    if octets[0] == 100 && (octets[1] & 0xC0) == 64 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    // Block 169.254.0.0/16 (link-local)
    if octets[0] == 169 && octets[1] == 254 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    // Block 172.16.0.0/12
    if octets[0] == 172 && (octets[1] & 0xF0) == 16 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    // Block 192.168.0.0/16
    if octets[0] == 192 && octets[1] == 168 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    Ok(())
}

/// Check an IPv6 address against blocked ranges.
fn check_ipv6(ip: Ipv6Addr, host: &str) -> Result<(), SsrfError> {
    // Allow ::1 (loopback)
    if ip == Ipv6Addr::LOCALHOST {
        return Ok(());
    }

    // Block :: (unspecified)
    if ip == Ipv6Addr::UNSPECIFIED {
        return Err(SsrfError::UnspecifiedAddress {
            host: host.to_string(),
        });
    }

    // Check IPv4-mapped IPv6 addresses (::ffff:x.x.x.x)
    if let Some(mapped_v4) = ip.to_ipv4_mapped() {
        return check_ipv4(mapped_v4, host);
    }

    let segments = ip.segments();

    // Block fc00::/7 (unique local addresses)
    // fc00::/7 means first byte is 0xFC or 0xFD → first segment high byte is 0xFC or 0xFD
    if (segments[0] & 0xFE00) == 0xFC00 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    // Block fe80::/10 (link-local)
    if (segments[0] & 0xFFC0) == 0xFE80 {
        return Err(SsrfError::PrivateIp {
            ip: ip.to_string(),
            host: host.to_string(),
        });
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blocks_private_10_network() {
        let err = check_ssrf("http://10.0.0.1/admin").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_private_172_16_network() {
        let err = check_ssrf("http://172.16.0.1/secret").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_private_192_168_network() {
        let err = check_ssrf("http://192.168.1.1/router").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_link_local_169_254() {
        let err = check_ssrf("http://169.254.169.254/latest/meta-data").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_carrier_grade_nat() {
        let err = check_ssrf("http://100.64.0.1/internal").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_unspecified_0_0_0_0() {
        let err = check_ssrf("http://0.0.0.0/").unwrap_err();
        assert!(matches!(err, SsrfError::UnspecifiedAddress { .. }));
    }

    #[test]
    fn blocks_ipv6_unspecified() {
        let err = check_ssrf("http://[::]/").unwrap_err();
        assert!(matches!(err, SsrfError::UnspecifiedAddress { .. }));
    }

    #[test]
    fn blocks_ipv6_unique_local() {
        let err = check_ssrf("http://[fc00::1]/internal").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_ipv6_link_local() {
        let err = check_ssrf("http://[fe80::1]/").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_ipv4_mapped_ipv6_private() {
        let err = check_ssrf("http://[::ffff:192.168.1.1]/").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn blocks_ipv4_mapped_ipv6_10_network() {
        let err = check_ssrf("http://[::ffff:10.0.0.1]/").unwrap_err();
        assert!(matches!(err, SsrfError::PrivateIp { .. }));
    }

    #[test]
    fn allows_loopback_ipv4() {
        assert!(check_ssrf("http://127.0.0.1:8080/api").is_ok());
    }

    #[test]
    fn allows_loopback_127_x() {
        assert!(check_ssrf("http://127.0.0.2:3000/").is_ok());
    }

    #[test]
    fn allows_loopback_ipv6() {
        assert!(check_ssrf("http://[::1]:8080/api").is_ok());
    }

    #[test]
    fn allows_public_ip() {
        assert!(check_ssrf("https://1.1.1.1/dns-query").is_ok());
    }

    #[test]
    fn allows_public_ip_8_8_8_8() {
        assert!(check_ssrf("https://8.8.8.8/").is_ok());
    }

    #[test]
    fn rejects_invalid_url() {
        let err = check_ssrf("not a url").unwrap_err();
        assert!(matches!(err, SsrfError::InvalidUrl(_)));
    }

    #[test]
    fn rejects_url_without_host() {
        let err = check_ssrf("file:///etc/passwd").unwrap_err();
        // file URLs have a host (empty string) or not depending on parser,
        // but either way should fail
        assert!(
            matches!(
                err,
                SsrfError::NoHost | SsrfError::DnsResolutionFailed { .. }
            ) || matches!(err, SsrfError::UnspecifiedAddress { .. })
        );
    }

    #[test]
    fn check_ip_directly_blocks_10_network() {
        let ip: IpAddr = "10.0.0.1".parse().unwrap();
        assert!(check_ip(ip, "test").is_err());
    }

    #[test]
    fn check_ip_directly_allows_public() {
        let ip: IpAddr = "93.184.216.34".parse().unwrap();
        assert!(check_ip(ip, "test").is_ok());
    }
}
