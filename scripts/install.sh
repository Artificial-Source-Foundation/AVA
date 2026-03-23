#!/bin/sh
# AVA Installer
# Usage: curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/scripts/install.sh | sh
set -e

REPO="ASF-GROUP/AVA"
INSTALL_DIR="$HOME/.ava/bin"
BINARY_NAME="ava"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

info() { printf "${BLUE}info${NC}: %s\n" "$1"; }
success() { printf "${GREEN}✓${NC} %s\n" "$1"; }
warn() { printf "${YELLOW}warning${NC}: %s\n" "$1"; }
error() { printf "${RED}error${NC}: %s\n" "$1" >&2; exit 1; }

# Cleanup on failure
cleanup() {
    if [ -n "${TMPDIR_CREATED:-}" ] && [ -d "${TMPDIR_CREATED}" ]; then
        rm -rf "$TMPDIR_CREATED"
    fi
}
trap cleanup EXIT

# Detect OS
detect_os() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "darwin" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *)       error "Unsupported OS: $(uname -s)" ;;
    esac
}

# Detect architecture
detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)  echo "x86_64" ;;
        aarch64|arm64) echo "aarch64" ;;
        *)             error "Unsupported architecture: $(uname -m)" ;;
    esac
}

# Map to Rust target triple
get_target() {
    local os="$1"
    local arch="$2"
    case "${os}-${arch}" in
        linux-x86_64)   echo "x86_64-unknown-linux-gnu" ;;
        linux-aarch64)  echo "aarch64-unknown-linux-gnu" ;;
        darwin-aarch64) echo "aarch64-apple-darwin" ;;
        darwin-x86_64)  echo "x86_64-apple-darwin" ;;
        windows-x86_64) echo "x86_64-pc-windows-msvc" ;;
        *)              error "Unsupported platform: ${os}-${arch}" ;;
    esac
}

# Check for required commands
check_deps() {
    for cmd in curl tar; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            error "Required command not found: $cmd"
        fi
    done
}

# Get latest release tag from GitHub
get_latest_version() {
    local url="https://api.github.com/repos/${REPO}/releases/latest"
    local response
    response=$(curl -fsSL "$url" 2>/dev/null) || {
        warn "Could not fetch latest release from GitHub"
        warn "You may need to build from source: cargo install --git https://github.com/${REPO}.git --bin ava"
        exit 1
    }
    echo "$response" | grep '"tag_name"' | head -1 | sed 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'
}

# Add ~/.ava/bin to PATH in shell profiles
add_to_path() {
    local path_line='export PATH="$HOME/.ava/bin:$PATH"'
    local marker="# AVA"

    for rcfile in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.bash_profile" "$HOME/.profile"; do
        if [ -f "$rcfile" ]; then
            if ! grep -q "$marker" "$rcfile" 2>/dev/null; then
                printf '\n%s\n%s\n' "$marker" "$path_line" >> "$rcfile"
                info "Added to PATH in $(basename "$rcfile")"
            fi
        fi
    done

    # Fish shell
    local fish_config="$HOME/.config/fish/config.fish"
    if [ -f "$fish_config" ]; then
        if ! grep -q "ava/bin" "$fish_config" 2>/dev/null; then
            printf '\n# AVA\nfish_add_path "$HOME/.ava/bin"\n' >> "$fish_config"
            info "Added to PATH in config.fish"
        fi
    fi

    # Export for current session
    export PATH="$HOME/.ava/bin:$PATH"
}

# Download and install
install() {
    check_deps

    local os
    local arch
    local target
    os=$(detect_os)
    arch=$(detect_arch)
    target=$(get_target "$os" "$arch")

    printf "\n"
    printf "  ${BOLD}AVA Installer${NC}\n"
    printf "  AI dev team — lean by default, infinitely extensible\n"
    printf "\n"

    info "Detected platform: ${os}/${arch} (${target})"

    # Get latest version
    info "Fetching latest release..."
    local version
    version=$(get_latest_version)
    if [ -z "$version" ]; then
        error "Could not determine latest version"
    fi
    info "Latest version: ${version}"

    # Construct download URL
    local filename="ava-${target}.tar.gz"
    local url="https://github.com/${REPO}/releases/download/${version}/${filename}"

    # Create install directory
    mkdir -p "$INSTALL_DIR"

    # Download
    info "Downloading ${filename}..."
    local tmpdir
    tmpdir=$(mktemp -d)
    TMPDIR_CREATED="$tmpdir"
    local tmpfile="${tmpdir}/${filename}"

    if ! curl -fsSL "$url" -o "$tmpfile" 2>/dev/null; then
        error "Download failed. The release may not have a binary for ${target}."
    fi

    # Extract
    info "Extracting..."
    tar -xzf "$tmpfile" -C "$tmpdir" 2>/dev/null || {
        error "Failed to extract archive"
    }

    # Find and install the binary
    local binary
    binary=$(find "$tmpdir" -name "$BINARY_NAME" -type f | head -1)
    if [ -z "$binary" ]; then
        binary=$(find "$tmpdir" -name "ava" -type f | head -1)
    fi

    if [ -z "$binary" ]; then
        error "Binary not found in archive"
    fi

    chmod +x "$binary"
    mv "$binary" "${INSTALL_DIR}/${BINARY_NAME}"

    success "Installed AVA ${version} to ${INSTALL_DIR}/${BINARY_NAME}"

    # Add to PATH
    add_to_path

    # Verify
    if command -v ava >/dev/null 2>&1; then
        local installed_version
        installed_version=$(ava --version 2>/dev/null || echo "unknown")
        success "AVA is ready! (${installed_version})"
    else
        printf "\n"
        warn "AVA was installed but is not in your PATH yet."
        printf "  Add this to your shell profile:\n"
        printf "  ${BOLD}export PATH=\"\$HOME/.ava/bin:\$PATH\"${NC}\n"
        printf "\n"
        printf "  Then restart your terminal or run:\n"
        printf "  ${BOLD}source ~/.bashrc${NC}  (or ~/.zshrc)\n"
    fi

    printf "\n"
    printf "  ${BOLD}Quick start:${NC}\n"
    printf "    ava                    # Interactive TUI\n"
    printf "    ava \"your goal\" --headless  # Headless mode\n"
    printf "    ava update             # Check for updates\n"
    printf "    ava --help             # All options\n"
    printf "\n"
}

# Run
install
