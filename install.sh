#!/bin/sh
# AVA CLI installer
# Usage: curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
#
# Installs the `ava` CLI binary to ~/.ava/bin/ and adds it to PATH.
#
# For private repos, have `gh` CLI installed and authenticated.
#
# Set AVA_INSTALL_DIR to override the installation directory:
#   AVA_INSTALL_DIR=/usr/local/bin ... | sh

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
  cargo build --release --bin ava" ;;
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

# ── GitHub helpers ────────────────────────────────────────────────────────────

get_latest_tag() {
    # Try gh CLI first (handles auth automatically)
    if command -v gh >/dev/null 2>&1; then
        _tag=$(gh release view --repo "${REPO}" --json tagName -q '.tagName' 2>/dev/null) || true
        if [ -n "${_tag:-}" ]; then
            echo "${_tag}"
            return 0
        fi
    fi

    # Fallback: GitHub API for public repos
    _api_url="https://api.github.com/repos/${REPO}/releases/latest"
    _response=$(curl -fsSL "${_api_url}" 2>/dev/null) || true

    if [ -n "${_response:-}" ]; then
        _tag=$(echo "${_response}" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
        if [ -n "${_tag}" ]; then
            echo "${_tag}"
            return 0
        fi
    fi

    warn "Could not fetch latest release from GitHub."
    warn "If the repo is private, install the gh CLI and run: gh auth login"
    warn "Or build from source:"
    printf '\n    \033[1mgit clone https://github.com/%s.git && cd AVA\033[0m\n' "${REPO}"
    printf '    \033[1mcargo build --release --bin ava\033[0m\n\n'
    exit 1
}

# Download a release asset (handles private repos via gh CLI)
download_asset() {
    _asset_name="$1"
    _dest="$2"
    _tag="$3"

    # Try gh CLI first (best for private repos)
    if command -v gh >/dev/null 2>&1; then
        if gh release download "${_tag}" --repo "${REPO}" -p "${_asset_name}" -D "$(dirname "${_dest}")" --clobber 2>/dev/null; then
            # gh downloads to dirname with original name
            _dl_path="$(dirname "${_dest}")/${_asset_name}"
            if [ "${_dl_path}" != "${_dest}" ] && [ -f "${_dl_path}" ]; then
                mv "${_dl_path}" "${_dest}"
            fi
            return 0
        fi
    fi

    # Fallback: direct curl download (public repos only)
    _url="https://github.com/${REPO}/releases/download/${_tag}/${_asset_name}"
    curl -fsSL -o "${_dest}" "${_url}" 2>/dev/null && return 0

    return 1
}

# ── Checksum verification ────────────────────────────────────────────────────

verify_checksum() {
    _file="$1"
    _checksum_file="$2"

    if [ ! -f "${_checksum_file}" ]; then
        die "No checksum file found for this release asset. Refusing to install without integrity metadata."
    fi

    _expected=$(awk '{print $1}' "${_checksum_file}")

    if command -v sha256sum >/dev/null 2>&1; then
        _actual=$(sha256sum "${_file}" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        _actual=$(shasum -a 256 "${_file}" | awk '{print $1}')
    else
        die "No sha256 verifier found (need sha256sum or shasum). Refusing to install without integrity verification."
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
    _path_line="export PATH=\"${INSTALL_DIR}:\$PATH\""
    _marker="# AVA"

    for _rcfile in "${HOME}/.bashrc" "${HOME}/.zshrc" "${HOME}/.bash_profile" "${HOME}/.profile"; do
        if [ -f "${_rcfile}" ]; then
            if ! grep -q "${_marker}" "${_rcfile}" 2>/dev/null; then
                printf '\n%s\n%s\n' "${_marker}" "${_path_line}" >> "${_rcfile}"
                ok "Added to PATH in $(basename "${_rcfile}")"
            fi
        fi
    done

    _fish_config="${HOME}/.config/fish/config.fish"
    if [ -f "${_fish_config}" ]; then
        if ! grep -q "ava/bin" "${_fish_config}" 2>/dev/null; then
            printf '\n# AVA\nfish_add_path "%s"\n' "${INSTALL_DIR}" >> "${_fish_config}"
            ok "Added to PATH in config.fish"
        fi
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    printf '\n\033[1m  AVA Installer\033[0m\n\n'

    need_cmd tar

    # Detect platform
    _os=$(detect_os)
    _arch=$(detect_arch)
    _target=$(get_target "${_os}" "${_arch}")
    info "Detected platform: ${_os} ${_arch} (${_target})"

    # Resolve latest version
    _tag=$(get_latest_tag)
    info "Latest release: ${_tag}"

    # Asset name
    _archive="ava-${_target}.tar.gz"

    # Create temp directory
    TMP_DIR=$(mktemp -d 2>/dev/null || mktemp -d -t 'ava-install')

    # Download archive
    info "Downloading ${_archive}..."
    if ! download_asset "${_archive}" "${TMP_DIR}/${_archive}" "${_tag}"; then
        die "Failed to download ${_archive} from release ${_tag}.
No binary available for ${_target}.
Build from source: cargo build --release --bin ava"
    fi

    # Download and verify checksum (optional)
    if download_asset "${_archive}.sha256" "${TMP_DIR}/${_archive}.sha256" "${_tag}" 2>/dev/null; then
        verify_checksum "${TMP_DIR}/${_archive}" "${TMP_DIR}/${_archive}.sha256"
    else
        die "Failed to download ${_archive}.sha256 for release ${_tag}. Refusing to install without integrity metadata."
    fi

    # Extract
    info "Extracting..."
    tar -xzf "${TMP_DIR}/${_archive}" -C "${TMP_DIR}"

    # Find the binary
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

    # Add to PATH
    add_to_path
    export PATH="${INSTALL_DIR}:${PATH}"

    printf '\n'

    # Check if it works
    if "${INSTALL_DIR}/ava" --version >/dev/null 2>&1; then
        _version=$("${INSTALL_DIR}/ava" --version 2>/dev/null || echo "${_tag}")
        ok "AVA ${_version} is ready!"
    else
        ok "AVA ${_tag} installed."
    fi

    printf '\n\033[1;32m  AVA %s installed successfully!\033[0m\n\n' "${_tag}"
    printf '  Get started:\n\n'
    printf '    \033[1mava\033[0m                                    # Interactive TUI\n'
    printf '    \033[1mava "your goal" --headless\033[0m              # Headless mode\n'
    printf '    \033[1mava --help\033[0m                              # All options\n'
    printf '\n'
    printf '  First time? Run \033[1mava --connect openrouter\033[0m or open \033[1mava\033[0m and use \033[1m/connect\033[0m.\n'
    printf '  Docs: https://github.com/%s\n\n' "${REPO}"
}

main "$@"
