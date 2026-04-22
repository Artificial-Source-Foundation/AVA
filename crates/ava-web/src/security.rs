use axum::extract::{Request, State};
use axum::http::header::{AUTHORIZATION, CONTENT_TYPE, ORIGIN};
use axum::http::{HeaderName, HeaderValue, Method, StatusCode, Uri};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use reqwest::Url;
use serde_json::json;
use tower_http::cors::{AllowOrigin, Any, CorsLayer};

pub const TOKEN_QUERY_PARAM: &str = "token";
pub const TOKEN_QUERY_PARAM_ALIAS: &str = "access_token";
pub const TOKEN_HEADER: &str = "x-ava-token";

#[derive(Clone, Debug)]
pub struct WebSecurityConfig {
    control_token: String,
    allow_any_origin: bool,
}

impl WebSecurityConfig {
    pub fn new(control_token: String, allow_any_origin: bool) -> Self {
        Self {
            control_token,
            allow_any_origin,
        }
    }

    #[cfg(test)]
    pub fn permissive_for_tests() -> Self {
        Self::new("test-control-token".to_string(), true)
    }

    pub fn control_token(&self) -> &str {
        &self.control_token
    }

    pub fn allow_any_origin(&self) -> bool {
        self.allow_any_origin
    }

    pub fn cors_layer(&self) -> CorsLayer {
        let cors = CorsLayer::new()
            .allow_methods([
                Method::GET,
                Method::POST,
                Method::PUT,
                Method::PATCH,
                Method::DELETE,
                Method::OPTIONS,
            ])
            .allow_headers([
                AUTHORIZATION,
                CONTENT_TYPE,
                HeaderName::from_static(TOKEN_HEADER),
            ]);

        if self.allow_any_origin {
            cors.allow_origin(Any)
        } else {
            cors.allow_origin(AllowOrigin::predicate(|origin, _request_parts| {
                is_allowed_browser_origin(origin, false)
            }))
        }
    }
}

pub async fn require_control_plane_http_access(
    State(security): State<WebSecurityConfig>,
    request: Request,
    next: Next,
) -> Response {
    enforce_request_security(&security, request, next, false).await
}

pub async fn require_control_plane_ws_access(
    State(security): State<WebSecurityConfig>,
    request: Request,
    next: Next,
) -> Response {
    enforce_request_security(&security, request, next, true).await
}

fn unauthorized_response() -> Response {
    (
        StatusCode::UNAUTHORIZED,
        axum::Json(json!({
            "error": "privileged ava serve routes require a valid control token"
        })),
    )
        .into_response()
}

fn forbidden_origin_response() -> Response {
    (
        StatusCode::FORBIDDEN,
        axum::Json(json!({
            "error": "browser origin is not allowed for this ava serve instance"
        })),
    )
        .into_response()
}

async fn enforce_request_security(
    security: &WebSecurityConfig,
    request: Request,
    next: Next,
    allow_query_token: bool,
) -> Response {
    if request.method() == Method::OPTIONS {
        return next.run(request).await;
    }

    if let Some(origin) = request.headers().get(ORIGIN) {
        if !is_allowed_browser_origin(origin, security.allow_any_origin()) {
            return forbidden_origin_response();
        }
    }

    let token = extract_request_token(request.headers(), request.uri(), allow_query_token);
    if token.as_deref() != Some(security.control_token()) {
        return unauthorized_response();
    }

    next.run(request).await
}

fn extract_request_token(
    headers: &axum::http::HeaderMap,
    uri: &Uri,
    allow_query_token: bool,
) -> Option<String> {
    let token = authorization_token(headers).or_else(|| custom_header_token(headers));

    if token.is_some() || !allow_query_token {
        return token;
    }

    query_token(uri)
}

fn authorization_token(headers: &axum::http::HeaderMap) -> Option<String> {
    let value = headers.get(AUTHORIZATION)?.to_str().ok()?.trim();

    value
        .strip_prefix("Bearer ")
        .or_else(|| value.strip_prefix("bearer "))
        .map(str::trim)
        .filter(|token| !token.is_empty())
        .map(ToOwned::to_owned)
}

fn custom_header_token(headers: &axum::http::HeaderMap) -> Option<String> {
    headers
        .get(TOKEN_HEADER)
        .and_then(|value| value.to_str().ok())
        .map(str::trim)
        .filter(|token| !token.is_empty())
        .map(ToOwned::to_owned)
}

fn query_token(uri: &Uri) -> Option<String> {
    let query = uri.query()?;
    let url = Url::parse(&format!("http://localhost/?{query}")).ok()?;

    url.query_pairs().find_map(|(key, value)| {
        ((key == TOKEN_QUERY_PARAM) || (key == TOKEN_QUERY_PARAM_ALIAS))
            .then(|| value.trim().to_string())
            .filter(|token| !token.is_empty())
    })
}

pub fn is_allowed_browser_origin(origin: &HeaderValue, allow_any_origin: bool) -> bool {
    if allow_any_origin {
        return true;
    }

    let Ok(origin) = origin.to_str() else {
        return false;
    };

    let Ok(url) = Url::parse(origin) else {
        return false;
    };

    if !matches!(url.scheme(), "http" | "https") {
        return false;
    }

    let Some(host) = url.host_str() else {
        return false;
    };

    host.eq_ignore_ascii_case("localhost")
        || host
            .parse::<std::net::IpAddr>()
            .map(|ip| ip.is_loopback())
            .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn local_origin_predicate_allows_loopback_hosts() {
        assert!(is_allowed_browser_origin(
            &HeaderValue::from_static("http://localhost:1490"),
            false,
        ));
        assert!(is_allowed_browser_origin(
            &HeaderValue::from_static("http://127.0.0.1:8080"),
            false,
        ));
        assert!(is_allowed_browser_origin(
            &HeaderValue::from_static("https://[::1]:11420"),
            false,
        ));
    }

    #[test]
    fn local_origin_predicate_rejects_non_loopback_hosts() {
        assert!(!is_allowed_browser_origin(
            &HeaderValue::from_static("https://example.com"),
            false,
        ));
        assert!(!is_allowed_browser_origin(
            &HeaderValue::from_static("null"),
            false,
        ));
    }

    #[test]
    fn query_token_accepts_access_token_alias() {
        let uri: Uri = "/ws?access_token=alias-secret".parse().expect("uri");
        assert_eq!(query_token(&uri).as_deref(), Some("alias-secret"));
    }
}
