#!/bin/bash
# QBE DOS Toolchain Setup Script
# Installs required tools for building DOS executables from C code
# Phase 0 - C11 8086 Architecture Implementation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}QBE DOS Toolchain Setup${NC}"
echo -e "${BLUE}================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Warning: This script may need root privileges to install packages.${NC}"
    echo -e "${YELLOW}You may be prompted for your password.${NC}"
    echo ""
    SUDO="sudo"
else
    SUDO=""
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VER=$VERSION_ID
else
    echo -e "${RED}Cannot detect OS. /etc/os-release not found.${NC}"
    exit 1
fi

echo -e "${BLUE}Detected OS: $OS $VER${NC}"
echo ""

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Install NASM
echo -e "${BLUE}[1/3] Checking NASM...${NC}"
if command_exists nasm; then
    echo -e "${GREEN}✓ NASM already installed: $(nasm -version | head -1)${NC}"
else
    echo -e "${YELLOW}Installing NASM assembler...${NC}"
    case "$OS" in
        ubuntu|debian)
            $SUDO apt-get install -y nasm
            ;;
        fedora|rhel|centos)
            $SUDO dnf install -y nasm || $SUDO yum install -y nasm
            ;;
        arch)
            $SUDO pacman -S --noconfirm nasm
            ;;
        *)
            echo -e "${RED}Unsupported OS for automatic installation.${NC}"
            echo -e "${YELLOW}Please install NASM manually from: https://www.nasm.us/${NC}"
            exit 1
            ;;
    esac
    echo -e "${GREEN}✓ NASM installed successfully${NC}"
fi
echo ""

# Install DOSBox
echo -e "${BLUE}[2/3] Checking DOSBox...${NC}"
if command_exists dosbox; then
    echo -e "${GREEN}✓ DOSBox already installed: $(dosbox -version 2>&1 | head -1)${NC}"
else
    echo -e "${YELLOW}Installing DOSBox emulator...${NC}"
    case "$OS" in
        ubuntu|debian)
            $SUDO apt-get install -y dosbox
            ;;
        fedora|rhel|centos)
            $SUDO dnf install -y dosbox || $SUDO yum install -y dosbox
            ;;
        arch)
            $SUDO pacman -S --noconfirm dosbox
            ;;
        *)
            echo -e "${RED}Unsupported OS for automatic installation.${NC}"
            echo -e "${YELLOW}Please install DOSBox manually from: https://www.dosbox.com/${NC}"
            exit 1
            ;;
    esac
    echo -e "${GREEN}✓ DOSBox installed successfully${NC}"
fi
echo ""

# Check for yacc/bison (needed for MiniC)
echo -e "${BLUE}[3/3] Checking yacc/bison...${NC}"
if command_exists yacc || command_exists bison; then
    if command_exists yacc; then
        echo -e "${GREEN}✓ yacc already installed${NC}"
    else
        echo -e "${GREEN}✓ bison already installed${NC}"
    fi
else
    echo -e "${YELLOW}Installing yacc/bison (needed for MiniC)...${NC}"
    case "$OS" in
        ubuntu|debian)
            $SUDO apt-get install -y bison
            ;;
        fedora|rhel|centos)
            $SUDO dnf install -y bison || $SUDO yum install -y bison
            ;;
        arch)
            $SUDO pacman -S --noconfirm bison
            ;;
        *)
            echo -e "${RED}Unsupported OS for automatic installation.${NC}"
            echo -e "${YELLOW}Please install bison/yacc manually.${NC}"
            exit 1
            ;;
    esac
    echo -e "${GREEN}✓ bison installed successfully${NC}"
fi
echo ""

# Summary
echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}Installation Summary${NC}"
echo -e "${BLUE}================================${NC}"
echo -e "${GREEN}✓ NASM:   $(nasm -version | head -1)${NC}"
echo -e "${GREEN}✓ DOSBox: $(dosbox -version 2>&1 | head -1)${NC}"
if command_exists yacc; then
    echo -e "${GREEN}✓ yacc:   Installed${NC}"
else
    echo -e "${GREEN}✓ bison:  $(bison --version | head -1)${NC}"
fi
echo ""

echo -e "${BLUE}================================${NC}"
echo -e "${GREEN}Toolchain setup complete!${NC}"
echo -e "${BLUE}================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Build QBE:   make"
echo "  2. Build MiniC: cd minic && make"
echo "  3. Test build:  ./tools/build-dos.sh <program.c>"
echo ""

exit 0
