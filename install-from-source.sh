#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# AVA — Build from Source Installer
# ============================================================================
# Interactive installer that lets you choose what to build and install.
# The pre-built binary installer is at install.sh — use that for quick setup.
#
# Components:
#   CLI/TUI  — Terminal interface (Rust only, no Node deps needed)
#   Desktop  — Tauri desktop app (Rust + SolidJS, needs pnpm)
#
# Files installed (all in $HOME, no sudo for the app itself):
#   ~/.local/bin/ava                                (CLI binary)
#   ~/.local/share/applications/ava.desktop         (desktop entry, desktop only)
#   ~/.local/share/icons/hicolor/128x128/apps/ava.png (icon, desktop only)
#
# To uninstall: ./uninstall.sh
# ============================================================================

VERSION="1.0.0"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

print_step() { echo -e "${BLUE}==>${NC} $1"; }
print_success() { echo -e "${GREEN}✓${NC} $1"; }
print_warning() { echo -e "${YELLOW}!${NC} $1"; }
print_error() { echo -e "${RED}✗${NC} $1"; }
print_dry() { echo -e "${DIM}[dry-run]${NC} $1"; }
print_info() { echo -e "  ${DIM}$1${NC}"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default options
DRY_RUN=false
INSTALL_CLI=false
INSTALL_DESKTOP=false
NON_INTERACTIVE=false

# ============================================================================
# Help
# ============================================================================
show_help() {
    cat << EOF
${BOLD}AVA — Build from Source${NC} v${VERSION}

${BOLD}USAGE:${NC}
    ./install-from-source.sh [OPTIONS]

${BOLD}OPTIONS:${NC}
    -h, --help          Show this help message
    -n, --dry-run       Show what would be done without making changes
    --cli               Install CLI/TUI only (non-interactive)
    --desktop           Install desktop app only (non-interactive)
    --all               Install everything (non-interactive)

${BOLD}EXAMPLES:${NC}
    ./install-from-source.sh              # Interactive — pick what to install
    ./install-from-source.sh --cli        # Just the terminal app
    ./install-from-source.sh --desktop    # Just the desktop app
    ./install-from-source.sh --all        # Everything
    ./install-from-source.sh --dry-run    # Preview what would happen

${BOLD}COMPONENTS:${NC}
    CLI/TUI    Terminal interface — pure Rust, no Node.js needed
    Desktop    Tauri app (SolidJS + Rust) — needs pnpm + Node.js

${BOLD}WHAT IS SUDO NEEDED FOR?${NC}
    Only if system build libraries are missing (libwebkit2gtk, etc).
    This is a one-time package install (apt/dnf). The app itself
    installs entirely in your home directory — no sudo required.

${BOLD}SUPPORTED DISTROS:${NC}
    Ubuntu, Debian, Pop!_OS, Linux Mint (apt)
    Fedora, RHEL, CentOS, Nobara (dnf)
    Arch, Manjaro (pacman)
    openSUSE (zypper)

${BOLD}TO UNINSTALL:${NC}
    ./uninstall.sh

EOF
}

# ============================================================================
# Parse arguments
# ============================================================================
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -n|--dry-run)
            DRY_RUN=true
            shift
            ;;
        --cli)
            INSTALL_CLI=true
            NON_INTERACTIVE=true
            shift
            ;;
        --desktop)
            INSTALL_DESKTOP=true
            NON_INTERACTIVE=true
            shift
            ;;
        --all)
            INSTALL_CLI=true
            INSTALL_DESKTOP=true
            NON_INTERACTIVE=true
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Run './install-from-source.sh --help' for usage."
            exit 1
            ;;
    esac
done

# ============================================================================
# Helpers
# ============================================================================
detect_distro() {
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        echo "$ID"
    else
        echo "unknown"
    fi
}

confirm() {
    local prompt="$1"
    local reply
    echo -ne "${YELLOW}?${NC} ${prompt} ${DIM}[Y/n]${NC} "
    read -r reply
    [[ -z "$reply" || "$reply" =~ ^[Yy]$ ]]
}

DISTRO=$(detect_distro)

# ============================================================================
# Banner
# ============================================================================
echo ""
echo -e "${CYAN}${BOLD}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}${BOLD}║                                                           ║${NC}"
echo -e "${CYAN}${BOLD}║       ${GREEN}    _    __     __    _                ${CYAN}             ║${NC}"
echo -e "${CYAN}${BOLD}║       ${GREEN}   / \  \ \   / /   / \               ${CYAN}             ║${NC}"
echo -e "${CYAN}${BOLD}║       ${GREEN}  / _ \  \ \ / /   / _ \              ${CYAN}             ║${NC}"
echo -e "${CYAN}${BOLD}║       ${GREEN} / ___ \  \ V /   / ___ \             ${CYAN}             ║${NC}"
echo -e "${CYAN}${BOLD}║       ${GREEN}/_/   \_\  \_/   /_/   \_\            ${CYAN}             ║${NC}"
echo -e "${CYAN}${BOLD}║                                                           ║${NC}"
echo -e "${CYAN}${BOLD}║           Your AI dev team in one app                     ║${NC}"
echo -e "${CYAN}${BOLD}║                                                           ║${NC}"
echo -e "${CYAN}${BOLD}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

if [[ "$DRY_RUN" == "true" ]]; then
    echo -e "${YELLOW}${BOLD}DRY RUN MODE${NC} - No changes will be made"
    echo ""
fi

# ============================================================================
# Interactive component selection
# ============================================================================
if [[ "$NON_INTERACTIVE" == "false" ]]; then
    echo -e "${DIM}  Note: Building from source takes 5-10 minutes on first run.${NC}"
    echo -e "${DIM}  For a quick install, use: curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh${NC}"
    echo ""
    echo -e "${BOLD}What would you like to install?${NC}"
    echo ""
    echo -e "  ${BOLD}1)${NC} CLI/TUI only      ${DIM}— Terminal interface (fast build, Rust only)${NC}"
    echo -e "  ${BOLD}2)${NC} Desktop app only   ${DIM}— Tauri desktop app (needs pnpm + Node.js)${NC}"
    echo -e "  ${BOLD}3)${NC} Everything         ${DIM}— CLI + Desktop${NC}"
    echo ""
    echo -ne "${YELLOW}?${NC} Choose an option ${DIM}[1/2/3]${NC} "
    read -r CHOICE
    echo ""

    case "$CHOICE" in
        1) INSTALL_CLI=true ;;
        2) INSTALL_DESKTOP=true ;;
        3) INSTALL_CLI=true; INSTALL_DESKTOP=true ;;
        *)
            print_error "Invalid choice: $CHOICE"
            exit 1
            ;;
    esac
fi

# ============================================================================
# Check prerequisites
# ============================================================================
print_step "Checking prerequisites..."

# Source cargo env if rustup is installed but cargo isn't in PATH yet
if ! command -v cargo &> /dev/null && [[ -f "$HOME/.cargo/env" ]]; then
    source "$HOME/.cargo/env"
fi

MISSING_DEPS=()

if ! command -v cargo &> /dev/null; then
    MISSING_DEPS+=("rust/cargo")
fi

if [[ "$INSTALL_DESKTOP" == "true" ]]; then
    if ! command -v pnpm &> /dev/null; then
        MISSING_DEPS+=("pnpm")
    fi
    if ! command -v node &> /dev/null; then
        MISSING_DEPS+=("node")
    fi
fi

if [[ ${#MISSING_DEPS[@]} -gt 0 ]]; then
    print_error "Missing dependencies: ${MISSING_DEPS[*]}"
    echo ""
    echo "Install them with:"
    echo "  rust:  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
    echo "  pnpm:  npm install -g pnpm"
    echo "  node:  https://nodejs.org/ or your package manager"
    exit 1
fi

print_success "Prerequisites OK"

# ============================================================================
# System dependencies (desktop app only)
# ============================================================================
if [[ "$INSTALL_DESKTOP" == "true" ]]; then
    NEEDS_SUDO=false

    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" || "$DISTRO" == "pop" || "$DISTRO" == "linuxmint" ]]; then
        if dpkg -s libayatana-appindicator3-dev &>/dev/null; then
            APPINDICATOR_PKG=""
        elif dpkg -s libappindicator3-dev &>/dev/null; then
            APPINDICATOR_PKG=""
        else
            APPINDICATOR_PKG="libayatana-appindicator3-dev"
        fi
        TAURI_DEPS="libwebkit2gtk-4.1-dev librsvg2-dev patchelf"
        [[ -n "$APPINDICATOR_PKG" ]] && TAURI_DEPS="$TAURI_DEPS $APPINDICATOR_PKG"
        MISSING_TAURI_DEPS=()
        for dep in $TAURI_DEPS; do
            if ! dpkg -s "$dep" &>/dev/null; then
                MISSING_TAURI_DEPS+=("$dep")
            fi
        done
        if [[ ${#MISSING_TAURI_DEPS[@]} -gt 0 ]]; then
            echo ""
            print_warning "Missing Tauri build libraries: ${MISSING_TAURI_DEPS[*]}"
            print_info "These are system libraries needed to compile the desktop app."
            print_info "This is the ONLY step that needs sudo (one-time install)."
            echo ""
            if [[ "$DRY_RUN" == "true" ]]; then
                print_dry "Would run: sudo apt update && sudo apt install -y ${MISSING_TAURI_DEPS[*]}"
            else
                if confirm "Install them now? (requires sudo)"; then
                    sudo apt update && sudo apt install -y "${MISSING_TAURI_DEPS[@]}"
                    print_success "Build libraries installed"
                else
                    echo ""
                    print_info "You can install them manually and re-run this script:"
                    print_info "  sudo apt update && sudo apt install -y ${MISSING_TAURI_DEPS[*]}"
                    exit 1
                fi
            fi
            echo ""
        fi

    elif [[ "$DISTRO" == "fedora" || "$DISTRO" == "rhel" || "$DISTRO" == "centos" || "$DISTRO" == "nobara" ]]; then
        TAURI_DEPS="webkit2gtk4.1-devel librsvg2-devel patchelf libappindicator-gtk3-devel gtk3-devel openssl-devel"
        MISSING_TAURI_DEPS=()
        for dep in $TAURI_DEPS; do
            if ! rpm -q "$dep" &>/dev/null; then
                MISSING_TAURI_DEPS+=("$dep")
            fi
        done
        if [[ ${#MISSING_TAURI_DEPS[@]} -gt 0 ]]; then
            echo ""
            print_warning "Missing Tauri build libraries: ${MISSING_TAURI_DEPS[*]}"
            print_info "These are system libraries needed to compile the desktop app."
            print_info "This is the ONLY step that needs sudo (one-time install)."
            echo ""
            if [[ "$DRY_RUN" == "true" ]]; then
                print_dry "Would run: sudo dnf install -y ${MISSING_TAURI_DEPS[*]}"
            else
                if confirm "Install them now? (requires sudo)"; then
                    sudo dnf install -y "${MISSING_TAURI_DEPS[@]}"
                    print_success "Build libraries installed"
                else
                    echo ""
                    print_info "You can install them manually and re-run this script:"
                    print_info "  sudo dnf install -y ${MISSING_TAURI_DEPS[*]}"
                    exit 1
                fi
            fi
            echo ""
        fi

    elif [[ "$DISTRO" == "arch" || "$DISTRO" == "manjaro" || "$DISTRO" == "endeavouros" ]]; then
        TAURI_DEPS="webkit2gtk-4.1 librsvg patchelf libappindicator-gtk3 openssl"
        MISSING_TAURI_DEPS=()
        for dep in $TAURI_DEPS; do
            if ! pacman -Qi "$dep" &>/dev/null; then
                MISSING_TAURI_DEPS+=("$dep")
            fi
        done
        if [[ ${#MISSING_TAURI_DEPS[@]} -gt 0 ]]; then
            echo ""
            print_warning "Missing Tauri build libraries: ${MISSING_TAURI_DEPS[*]}"
            print_info "These are system libraries needed to compile the desktop app."
            print_info "This is the ONLY step that needs sudo (one-time install)."
            echo ""
            if [[ "$DRY_RUN" == "true" ]]; then
                print_dry "Would run: sudo pacman -S --needed ${MISSING_TAURI_DEPS[*]}"
            else
                if confirm "Install them now? (requires sudo)"; then
                    sudo pacman -S --needed --noconfirm "${MISSING_TAURI_DEPS[@]}"
                    print_success "Build libraries installed"
                else
                    echo ""
                    print_info "You can install them manually and re-run this script:"
                    print_info "  sudo pacman -S --needed ${MISSING_TAURI_DEPS[*]}"
                    exit 1
                fi
            fi
            echo ""
        fi

    elif [[ "$DISTRO" == "opensuse-tumbleweed" || "$DISTRO" == "opensuse-leap" ]]; then
        TAURI_DEPS="webkit2gtk3-devel librsvg-devel patchelf libappindicator3-devel libopenssl-devel"
        MISSING_TAURI_DEPS=()
        for dep in $TAURI_DEPS; do
            if ! rpm -q "$dep" &>/dev/null; then
                MISSING_TAURI_DEPS+=("$dep")
            fi
        done
        if [[ ${#MISSING_TAURI_DEPS[@]} -gt 0 ]]; then
            echo ""
            print_warning "Missing Tauri build libraries: ${MISSING_TAURI_DEPS[*]}"
            print_info "These are system libraries needed to compile the desktop app."
            print_info "This is the ONLY step that needs sudo (one-time install)."
            echo ""
            if [[ "$DRY_RUN" == "true" ]]; then
                print_dry "Would run: sudo zypper install -y ${MISSING_TAURI_DEPS[*]}"
            else
                if confirm "Install them now? (requires sudo)"; then
                    sudo zypper install -y "${MISSING_TAURI_DEPS[@]}"
                    print_success "Build libraries installed"
                else
                    echo ""
                    print_info "You can install them manually and re-run this script:"
                    print_info "  sudo zypper install -y ${MISSING_TAURI_DEPS[*]}"
                    exit 1
                fi
            fi
            echo ""
        fi
    fi
fi

# ============================================================================
# Build and install CLI
# ============================================================================
if [[ "$INSTALL_CLI" == "true" ]]; then
    echo ""
    print_step "Building AVA CLI/TUI (this may take a few minutes on first build)..."

    if [[ "$DRY_RUN" == "true" ]]; then
        print_dry "Would run: cargo build --release --bin ava"
    else
        cd "$SCRIPT_DIR"
        # Show cargo output filtered to progress lines
        cargo build --release --bin ava 2>&1 | while IFS= read -r line; do
            if [[ "$line" =~ Compiling[[:space:]]+([^ ]+)[[:space:]]+(.+) ]]; then
                printf "  ${DIM}Compiling %s %s${NC}\n" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
            elif [[ "$line" =~ Finished ]]; then
                printf "  ${DIM}%s${NC}\n" "$line"
            elif [[ "$line" =~ error ]]; then
                printf "  ${RED}%s${NC}\n" "$line"
            fi
        done

        if [[ ! -f "$SCRIPT_DIR/target/release/ava" ]]; then
            echo ""
            print_error "Build failed — no binary found"
            exit 1
        fi
    fi
    print_success "CLI build complete"

    echo ""
    print_step "Installing CLI to ~/.local/bin..."
    print_info "Binary: ~/.local/bin/ava"

    if [[ "$DRY_RUN" == "true" ]]; then
        print_dry "Would copy binary to ~/.local/bin/ava"
    else
        mkdir -p "$HOME/.local/bin"
        cp "$SCRIPT_DIR/target/release/ava" "$HOME/.local/bin/ava"
        chmod +x "$HOME/.local/bin/ava"
        print_success "CLI installed"
    fi
fi

# ============================================================================
# Build and install Desktop app
# ============================================================================
if [[ "$INSTALL_DESKTOP" == "true" ]]; then
    echo ""
    print_step "Installing Node.js dependencies..."
    if [[ "$DRY_RUN" == "true" ]]; then
        print_dry "Would run: pnpm install"
    else
        cd "$SCRIPT_DIR"
        pnpm install
    fi
    print_success "Dependencies ready"

    echo ""
    print_step "Building AVA desktop app (this may take a few minutes)..."

    if [[ "$DRY_RUN" == "true" ]]; then
        print_dry "Would run: pnpm tauri build"
    else
        cd "$SCRIPT_DIR"
        pnpm tauri build 2>&1 | while IFS= read -r line; do
            if [[ "$line" =~ Compiling[[:space:]]+([^ ]+)[[:space:]]+(.+) ]]; then
                printf "  ${DIM}Compiling %s %s${NC}\n" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
            elif [[ "$line" =~ Bundling[[:space:]]+(.+) ]]; then
                printf "  ${DIM}Bundling %s${NC}\n" "${BASH_REMATCH[1]}"
            elif [[ "$line" =~ Finished ]]; then
                printf "  ${DIM}%s${NC}\n" "$line"
            elif [[ "$line" =~ error ]]; then
                printf "  ${RED}%s${NC}\n" "$line"
            fi
        done || true

        RELEASE_DIR="$SCRIPT_DIR/target/release"
        BINARY_NAME="ava"
        # Tauri may name the binary differently
        if [[ ! -f "$RELEASE_DIR/$BINARY_NAME" ]]; then
            for name in AVA ava; do
                if [[ -f "$RELEASE_DIR/$name" ]]; then
                    BINARY_NAME="$name"
                    break
                fi
            done
        fi

        if [[ ! -f "$RELEASE_DIR/$BINARY_NAME" ]]; then
            echo ""
            print_error "Desktop build failed — no binary found"
            exit 1
        fi
    fi
    print_success "Desktop build complete"

    RELEASE_DIR="$SCRIPT_DIR/target/release"
    ICON_SRC="$SCRIPT_DIR/src-tauri/icons/128x128.png"

    echo ""
    print_step "Installing desktop app to ~/.local (no sudo needed)..."
    print_info "Binary:  ~/.local/bin/ava-desktop"
    print_info "Desktop: ~/.local/share/applications/ava.desktop"
    print_info "Icon:    ~/.local/share/icons/hicolor/128x128/apps/ava.png"

    if [[ "$DRY_RUN" == "true" ]]; then
        print_dry "Would copy binary to ~/.local/bin/ava-desktop"
        print_dry "Would create desktop entry"
        print_dry "Would install icon"
    else
        # Binary (named ava-desktop to avoid conflict with CLI)
        mkdir -p "$HOME/.local/bin"
        cp "$RELEASE_DIR/$BINARY_NAME" "$HOME/.local/bin/ava-desktop"
        chmod +x "$HOME/.local/bin/ava-desktop"
        print_success "Binary installed"

        # Icon
        if [[ -f "$ICON_SRC" ]]; then
            ICON_DIR="$HOME/.local/share/icons/hicolor/128x128/apps"
            mkdir -p "$ICON_DIR"
            cp "$ICON_SRC" "$ICON_DIR/ava.png"
            print_success "Icon installed"
        else
            print_warning "Icon not found at $ICON_SRC — skipping"
        fi

        # Desktop entry
        mkdir -p "$HOME/.local/share/applications"
        cat > "$HOME/.local/share/applications/ava.desktop" << EOF
[Desktop Entry]
Name=AVA
Comment=Your AI dev team in one app
Exec=$HOME/.local/bin/ava-desktop
Icon=ava
Terminal=false
Type=Application
Categories=Development;IDE;
EOF
        print_success "Desktop entry created"

        # Update icon cache if available
        if command -v gtk-update-icon-cache &>/dev/null; then
            gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
        fi
    fi
fi

# ============================================================================
# PATH check
# ============================================================================
if [[ "$DRY_RUN" != "true" && ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo ""
    print_warning "~/.local/bin is not in your PATH"
    print_info "Add this to your shell config (~/.bashrc or ~/.zshrc):"
    print_info "  export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

# ============================================================================
# Done!
# ============================================================================
echo ""
echo -e "${GREEN}${BOLD}╔═══════════════════════════════════════════════════════════╗${NC}"
if [[ "$DRY_RUN" == "true" ]]; then
    echo -e "${GREEN}${BOLD}║              Dry Run Complete!                            ║${NC}"
else
    echo -e "${GREEN}${BOLD}║              Installation Complete!                       ║${NC}"
fi
echo -e "${GREEN}${BOLD}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

if [[ "$INSTALL_CLI" == "true" ]]; then
    echo -e "  ${GREEN}✓${NC} AVA CLI/TUI"
    if [[ "$DRY_RUN" == "false" ]]; then
        echo -e "    ${DIM}Run: ava${NC}"
    fi
fi
if [[ "$INSTALL_DESKTOP" == "true" ]]; then
    echo -e "  ${GREEN}✓${NC} AVA Desktop App"
    if [[ "$DRY_RUN" == "false" ]]; then
        echo -e "    ${DIM}Launch from your app menu or run: ava-desktop${NC}"
    fi
fi

echo ""
if [[ "$DRY_RUN" == "false" ]]; then
    echo -e "  ${DIM}Get started:${NC}"
    echo -e "    ${DIM}1. Connect a provider: ava auth add${NC}"
    echo -e "    ${DIM}2. Launch: ava${NC}"
fi

echo ""
if [[ "$DRY_RUN" == "true" ]]; then
    echo "Run without --dry-run to actually install."
else
    echo "To uninstall: ./uninstall.sh"
fi
echo ""
