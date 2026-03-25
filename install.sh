#!/bin/sh
# AVA CLI installer
# Usage: curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
#
# Installs the `ava` CLI binary to ~/.ava/bin/ and adds it to PATH.
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
        die "Required command not found: $1. Please install it and try again."
    fi
}

# ── Detect platform ──────────────────────────────────────────────────────────

detect_os() {
    case "$(uname -s)" in
        Linux*)           echo "linux" ;;
        Darwin*)          echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*)
            die "Windows detected. Please build from source:
  git clone https://github.com/${REPO}.git && cd AVA
  cargo build --release --bin ava
  # Binary will be at target/release/ava.exe" ;;
        *)
            die "Unsupported OS: $(uname -s). AVA supports Linux and macOS.
  Build from source: cargo build --release --bin ava" ;;
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

    case "${_os}-${_arch}" in
        linux-x86_64)   echo "x86_64-unknown-linux-gnu" ;;
        linux-aarch64)  echo "aarch64-unknown-linux-gnu" ;;
        macos-x86_64)   echo "x86_64-apple-darwin" ;;
        macos-aarch64)  echo "aarch64-apple-darwin" ;;
        *)              die "Unsupported platform: ${_os}-${_arch}" ;;
    esac
}

# ── Resolve latest release tag ───────────────────────────────────────────────

get_latest_tag() {
    _url="https://github.com/${REPO}/releases/latest"

    if command -v curl >/dev/null 2>&1; then
        _location=$(curl -fsSI -o /dev/null -w '%{url_effective}' "${_url}" 2>/dev/null) || {
            warn "Failed to fetch latest release from GitHub."
            warn "No releases published yet. Build from source instead:"
            printf '\n    \033[1mgit clone https://github.com/%s.git && cd AVA\033[0m\n' "${REPO}"
            printf '    \033[1mcargo build --release --bin ava\033[0m\n\n'
            exit 1
        }
    elif command -v wget >/dev/null 2>&1; then
        _location=$(wget --spider --max-redirect=0 -S "${_url}" 2>&1 | \
            sed -n 's/.*Location: *//p' | tr -d '\r') || {
            warn "Failed to fetch latest release from GitHub."
            warn "Build from source: cargo build --release --bin ava"
            exit 1
        }
    else
        die "Neither curl nor wget found. Please install one of them."
    fi

    # Extract tag from URL: .../releases/tag/v2.1.0 -> v2.1.0
    _tag="${_location##*/}"

    # Validate we actually got a tag, not just "releases" or empty
    case "${_tag}" in
        releases|latest|"")
            warn "No releases published yet. Build from source instead:"
            printf '\n    \033[1mgit clone https://github.com/%s.git && cd AVA\033[0m\n' "${REPO}"
            printf '    \033[1mcargo build --release --bin ava\033[0m\n'
            printf '    \033[1mcp target/release/ava ~/.ava/bin/\033[0m\n\n'
            exit 1
            ;;
    esac

    echo "${_tag}"
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

# ── Add to PATH ──────────────────────────────────────────────────────────────

add_to_path() {
    _path_line='export PATH="$HOME/.ava/bin:$PATH"'
    _marker="# AVA"

    # Add to whichever shell configs exist
    for _rcfile in "${HOME}/.bashrc" "${HOME}/.zshrc" "${HOME}/.bash_profile" "${HOME}/.profile"; do
        if [ -f "${_rcfile}" ]; then
            if ! grep -q "${_marker}" "${_rcfile}" 2>/dev/null; then
                printf '\n%s\n%s\n' "${_marker}" "${_path_line}" >> "${_rcfile}"
                ok "Added to PATH in $(basename "${_rcfile}")"
            fi
        fi
    done

    # Fish shell
    _fish_config="${HOME}/.config/fish/config.fish"
    if [ -f "${_fish_config}" ]; then
        if ! grep -q "ava/bin" "${_fish_config}" 2>/dev/null; then
            printf '\n# AVA\nfish_add_path "$HOME/.ava/bin"\n' >> "${_fish_config}"
            ok "Added to PATH in config.fish"
        fi
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '\n\033[1m  AVA Installer\033[0m\n\n'

    # Check dependencies
    need_cmd tar

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
No binary available for ${_target}.
Build from source: cargo build --release --bin ava"

    # Download and verify checksum (optional)
    if download "${_checksum_url}" "${TMP_DIR}/${_archive}.sha256" 2>/dev/null; then
        verify_checksum "${TMP_DIR}/${_archive}" "${TMP_DIR}/${_archive}.sha256"
    else
        warn "No checksum file available; skipping verification."
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

    # Add to PATH automatically
    add_to_path

    # Export for current session
    export PATH="${INSTALL_DIR}:${PATH}"

    printf '\n'

    # Check if it works
    if "${INSTALL_DIR}/ava" --version >/dev/null 2>&1; then
        _version=$("${INSTALL_DIR}/ava" --version 2>/dev/null || echo "${_tag}")
        ok "AVA ${_version} is ready!"
    else
        ok "AVA ${_tag} installed."
    fi

    # Success message
    printf '\n\033[1;32m  AVA %s installed successfully!\033[0m\n\n' "${_tag}"
    printf '  Get started:\n\n'
    printf '    \033[1mava\033[0m                                    # Interactive TUI\n'
    printf '    \033[1mava "your goal" --headless\033[0m              # Headless mode\n'
    printf '    \033[1mava --help\033[0m                              # All options\n'
    printf '\n'
    printf '  First time? Run \033[1mava\033[0m and use \033[1m/connect\033[0m to add your API key.\n'
    printf '  Docs: https://github.com/%s\n\n' "${REPO}"
}

main "$@"
