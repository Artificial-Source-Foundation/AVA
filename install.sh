#!/bin/sh
# AVA CLI installer
# Usage: curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
#
# Installs the `ava` CLI binary to ~/.ava/bin/ and prints PATH instructions.
# Set AVA_INSTALL_DIR to override the installation directory:
#   AVA_INSTALL_DIR=/usr/local/bin curl -fsSL ... | sh

set -eu

# ── Constants ─────────────────────────────────────────────────────────────────

REPO="ASF-GROUP/AVA"
INSTALL_DIR="${AVA_INSTALL_DIR:-${HOME}/.ava/bin}"
TMP_DIR=""

# ── Helpers ───────────────────────────────────────────────────────────────────

info()  { printf '  \033[1;34m>\033[0m %s\n' "$1"; }
ok()    { printf '  \033[1;32m✓\033[0m %s\n' "$1"; }
warn()  { printf '  \033[1;33m!\033[0m %s\n' "$1"; }
err()   { printf '  \033[1;31m✗\033[0m %s\n' "$1" >&2; }
die()   { err "$1"; exit 1; }

cleanup() {
    if [ -n "${TMP_DIR}" ] && [ -d "${TMP_DIR}" ]; then
        rm -rf "${TMP_DIR}"
    fi
}
trap cleanup EXIT INT TERM

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        die "Required command not found: $1"
    fi
}

# ── Detect platform ──────────────────────────────────────────────────────────

detect_os() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "macos" ;;
        *)       die "Unsupported OS: $(uname -s). AVA supports Linux and macOS." ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)   echo "x86_64" ;;
        aarch64|arm64)  echo "aarch64" ;;
        *)              die "Unsupported architecture: $(uname -m). AVA supports x86_64 and aarch64/arm64." ;;
    esac
}

# Map OS + arch to Rust target triple
get_target() {
    _os="$1"
    _arch="$2"

    case "${_os}" in
        linux)
            case "${_arch}" in
                x86_64)  echo "x86_64-unknown-linux-gnu" ;;
                aarch64) echo "aarch64-unknown-linux-gnu" ;;
            esac
            ;;
        macos)
            case "${_arch}" in
                x86_64)  echo "x86_64-apple-darwin" ;;
                aarch64) echo "aarch64-apple-darwin" ;;
            esac
            ;;
    esac
}

# ── Resolve latest release tag ───────────────────────────────────────────────

get_latest_tag() {
    # Follow the redirect from /releases/latest to get the tag
    _url="https://github.com/${REPO}/releases/latest"

    if command -v curl >/dev/null 2>&1; then
        _location=$(curl -fsSI -o /dev/null -w '%{url_effective}' "${_url}" 2>/dev/null) || \
            die "Failed to fetch latest release. Check your network connection."
    elif command -v wget >/dev/null 2>&1; then
        _location=$(wget --spider --max-redirect=0 -S "${_url}" 2>&1 | \
            sed -n 's/.*Location: *//p' | tr -d '\r') || \
            die "Failed to fetch latest release. Check your network connection."
    else
        die "Neither curl nor wget found. Please install one of them."
    fi

    # Extract tag from URL: .../releases/tag/v1.2.3
    echo "${_location##*/}"
}

# ── Download helper ──────────────────────────────────────────────────────────

download() {
    _url="$1"
    _dest="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "${_dest}" "${_url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "${_dest}" "${_url}"
    else
        die "Neither curl nor wget found."
    fi
}

# ── Checksum verification ────────────────────────────────────────────────────

verify_checksum() {
    _file="$1"
    _checksum_file="$2"

    if [ ! -f "${_checksum_file}" ]; then
        warn "No checksum file found; skipping verification."
        return 0
    fi

    _expected=$(awk '{print $1}' "${_checksum_file}")

    if command -v sha256sum >/dev/null 2>&1; then
        _actual=$(sha256sum "${_file}" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        _actual=$(shasum -a 256 "${_file}" | awk '{print $1}')
    else
        warn "No sha256sum or shasum found; skipping checksum verification."
        return 0
    fi

    if [ "${_expected}" != "${_actual}" ]; then
        die "Checksum mismatch!
  Expected: ${_expected}
  Got:      ${_actual}
The download may be corrupted. Please try again."
    fi

    ok "Checksum verified."
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '\n\033[1m  AVA Installer\033[0m\n\n'

    # Detect platform
    _os=$(detect_os)
    _arch=$(detect_arch)
    _target=$(get_target "${_os}" "${_arch}")
    info "Detected platform: ${_os} ${_arch} (${_target})"

    # Resolve latest version
    _tag=$(get_latest_tag)
    if [ -z "${_tag}" ]; then
        die "Could not determine latest release tag."
    fi
    info "Latest release: ${_tag}"

    # Prepare asset URLs
    _archive="ava-${_target}.tar.gz"
    _base_url="https://github.com/${REPO}/releases/download/${_tag}"
    _archive_url="${_base_url}/${_archive}"
    _checksum_url="${_base_url}/${_archive}.sha256"

    # Create temp directory
    TMP_DIR=$(mktemp -d 2>/dev/null || mktemp -d -t 'ava-install')

    # Download archive
    info "Downloading ${_archive}..."
    download "${_archive_url}" "${TMP_DIR}/${_archive}" || \
        die "Failed to download ${_archive_url}
Please check that a release exists for your platform at:
  https://github.com/${REPO}/releases/tag/${_tag}"

    # Download and verify checksum (optional)
    if download "${_checksum_url}" "${TMP_DIR}/${_archive}.sha256" 2>/dev/null; then
        verify_checksum "${TMP_DIR}/${_archive}" "${TMP_DIR}/${_archive}.sha256"
    else
        warn "No .sha256 checksum file available; skipping verification."
    fi

    # Extract
    info "Extracting..."
    tar -xzf "${TMP_DIR}/${_archive}" -C "${TMP_DIR}"

    # Find the binary (may be at top level or in a subdirectory)
    _binary=""
    if [ -f "${TMP_DIR}/ava" ]; then
        _binary="${TMP_DIR}/ava"
    elif [ -f "${TMP_DIR}/ava-${_target}/ava" ]; then
        _binary="${TMP_DIR}/ava-${_target}/ava"
    else
        # Search for it -- try GNU find first, then BSD find
        _binary=$(find "${TMP_DIR}" -name "ava" -type f 2>/dev/null | head -1) || true
    fi

    if [ -z "${_binary}" ] || [ ! -f "${_binary}" ]; then
        die "Could not find 'ava' binary in the downloaded archive."
    fi

    # Install
    mkdir -p "${INSTALL_DIR}"
    cp "${_binary}" "${INSTALL_DIR}/ava"
    chmod +x "${INSTALL_DIR}/ava"
    ok "Installed ava to ${INSTALL_DIR}/ava"

    # Check PATH
    _path_configured=false
    case ":${PATH}:" in
        *":${INSTALL_DIR}:"*) _path_configured=true ;;
    esac

    printf '\n'

    if [ "${_path_configured}" = true ]; then
        ok "~/.ava/bin is already in your PATH."
    else
        warn "~/.ava/bin is not in your PATH."
        printf '\n  Add it by appending this line to your shell config:\n\n'

        # Detect shell rc file
        _shell_name=$(basename "${SHELL:-/bin/sh}")
        case "${_shell_name}" in
            zsh)  _rc_file="~/.zshrc" ;;
            bash) _rc_file="~/.bashrc" ;;
            fish) _rc_file="~/.config/fish/config.fish" ;;
            *)    _rc_file="~/.profile" ;;
        esac

        if [ "${_shell_name}" = "fish" ]; then
            printf '    \033[1mset -gx PATH \$HOME/.ava/bin \$PATH\033[0m\n\n'
        else
            printf '    \033[1mexport PATH="$HOME/.ava/bin:$PATH"\033[0m\n\n'
        fi
        printf '  Then restart your shell or run:\n\n'
        printf '    \033[1msource %s\033[0m\n' "${_rc_file}"
    fi

    # Success message
    printf '\n\033[1;32m  AVA %s installed successfully!\033[0m\n\n' "${_tag}"
    printf '  Get started:\n\n'
    printf '    1. Add your API key:\n'
    printf '       \033[1mmkdir -p ~/.ava && cat > ~/.ava/credentials.json << '\''EOF'\''\n'
    printf '       {\n'
    printf '         "providers": {\n'
    printf '           "openrouter": { "api_key": "YOUR_KEY" }\n'
    printf '         }\n'
    printf '       }\n'
    printf '       EOF\033[0m\n\n'
    printf '    2. Launch the TUI:\n'
    printf '       \033[1mava\033[0m\n\n'
    printf '    3. Or run headless:\n'
    printf '       \033[1mava "your prompt" --headless --provider openrouter --model anthropic/claude-sonnet-4\033[0m\n\n'
    printf '  Docs: https://github.com/%s\n\n' "${REPO}"
}

main "$@"
