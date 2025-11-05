#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

error_exit() {
    echo -e "${RED}Error: $1${NC}" >&2
    exit 1
}

log_message() {
    echo -e "${GREEN}$1${NC}"
}

trap 'error_exit "An unexpected error occurred at line $LINENO"' ERR

check_root() {
    if [[ $EUID -ne 0 ]]; then
        error_exit "This script must be run as root or with sudo privileges"
    fi
}

install_git() {
    packages_to_install=""
    
    if ! command -v git &> /dev/null; then
        log_message "Git not found. Installing..."
        packages_to_install="git"
    fi
    
    if [ -n "$packages_to_install" ]; then
        if [ -f /etc/debian_version ]; then
            apt-get update -q >/dev/null 2>&1 || error_exit "Failed to update package lists"
            apt-get install -y -q $packages_to_install >/dev/null 2>&1 || error_exit "Failed to install packages"
        elif [ -f /etc/redhat-release ]; then
            if command -v dnf &> /dev/null; then
                dnf install -y -q $packages_to_install >/dev/null 2>&1 || error_exit "Failed to install packages with dnf"
            elif command -v yum &> /dev/null; then
                yum install -y -q $packages_to_install >/dev/null 2>&1 || error_exit "Failed to install packages with yum"
            else
                error_exit "Could not install packages: no package manager found"
            fi
        elif [ -f /etc/alpine-release ]; then
            apk add --no-cache -q $packages_to_install >/dev/null 2>&1 || error_exit "Failed to install packages with apk"
        else
            error_exit "Unsupported OS for automatic package installation"
        fi
    else
        log_message "Git is already installed"
    fi
}

check_root
install_git

clone_repository() {
    PHOBOS_BASE_DIR="/opt/Phobos"
    REPO_DIR="$PHOBOS_BASE_DIR/repo"
    if [ -d "$PHOBOS_BASE_DIR" ]; then
        log_message "Phobos base directory already exists at $PHOBOS_BASE_DIR"
        if [ -d "$REPO_DIR" ]; then
            log_message "Phobos repository directory already exists at $REPO_DIR"
            rm -rf "$REPO_DIR" >/dev/null 2>&1 || error_exit "Failed to remove existing Phobos repository directory"
            log_message "Removed existing Phobos repository directory"
        fi
    else
        mkdir -p "$PHOBOS_BASE_DIR" >/dev/null 2>&1 || error_exit "Failed to create Phobos base directory"
    fi
    
    log_message "Cloning Phobos repository to $REPO_DIR..."
    
    mkdir -p "$REPO_DIR" >/dev/null 2>&1
    cd "$REPO_DIR" || error_exit "Failed to change to $REPO_DIR directory"
    git init >/dev/null 2>&1 || error_exit "Failed to initialize git repository"
    git remote add origin https://github.com/Ground-Zerro/Phobos.git >/dev/null 2>&1 || error_exit "Failed to add git remote"
    
    git config core.sparseCheckout true >/dev/null 2>&1 || error_exit "Failed to enable sparse checkout"
    
    echo "server" > .git/info/sparse-checkout
    echo "client" >> .git/info/sparse-checkout
    
    git pull origin main >/dev/null 2>&1 || error_exit "Failed to pull repository with sparse checkout"
    
    rm -rf .git >/dev/null 2>&1 || error_exit "Failed to remove .git directory"
    
    if [ -d "server" ]; then
        find server -name "*.sh" -exec chmod +x {} \; 2>/dev/null || true
    else
        error_exit "Server directory not found after cloning"
    fi
    if [ -d "client" ]; then
        find client -name "*.sh" -exec chmod +x {} \; 2>/dev/null || true
    fi
    
    log_message "Repository cloned successfully with only server and client directories"
}

clone_repository

prompt_username() {
    echo
    if [ -t 1 ] ; then
        exec < /dev/tty
    fi
    read -p "Введите имя для первого пользователя: " username
    if [ -z "$username" ]; then
        error_exit "Username cannot be empty"
    fi
    if [[ ! "$username" =~ ^[a-zA-Z0-9_-]+$ ]]; then
        error_exit "Invalid username. Use only letters, numbers, underscores, and dashes."
    fi
    log_message "Username '$username' is valid"
    export FIRST_USERNAME="$username"
}

prompt_username

run_init_script() {
    REPO_DIR="/opt/Phobos/repo"
    INIT_SCRIPT="$REPO_DIR/server/scripts/vps-init-all.sh"
    if [ ! -f "$INIT_SCRIPT" ]; then
        error_exit "Initialization script not found at $INIT_SCRIPT"
    fi
    chmod +x "$INIT_SCRIPT" >/dev/null 2>&1 || error_exit "Failed to make $INIT_SCRIPT executable"
    log_message "Running vps-init-all.sh..."
    cd "$REPO_DIR/server/scripts" || error_exit "Failed to change to $REPO_DIR/server/scripts directory"
    "$INIT_SCRIPT" || error_exit "Initialization script failed"
    log_message "Initialization completed successfully"
}

run_init_script

run_client_add_script() {
    REPO_DIR="/opt/Phobos/repo"
    CLIENT_ADD_SCRIPT="$REPO_DIR/server/scripts/vps-client-add.sh"
    if [ ! -f "$CLIENT_ADD_SCRIPT" ]; then
        error_exit "Client add script not found at $CLIENT_ADD_SCRIPT"
    fi
    chmod +x "$CLIENT_ADD_SCRIPT" >/dev/null 2>&1 || error_exit "Failed to make $CLIENT_ADD_SCRIPT executable"
    log_message "Running vps-client-add.sh for user $FIRST_USERNAME..."
    cd "$REPO_DIR/server/scripts" || error_exit "Failed to change to $REPO_DIR/server/scripts directory"
    "$CLIENT_ADD_SCRIPT" "$FIRST_USERNAME" || error_exit "Client add script failed"
    log_message "Client added successfully"
}

run_client_add_script

if [ -d "/opt/Phobos/repo/server" ]; then
    find /opt/Phobos/repo/server -name "*.sh" -exec chmod +x {} \; 2>/dev/null || true
fi

log_message "Phobos VPS deployment completed successfully!"
log_message "First user '$FIRST_USERNAME' has been created."