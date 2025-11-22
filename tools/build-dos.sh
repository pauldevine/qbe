#!/bin/bash
# QBE DOS Build Script
# Compiles C source to DOS executable
# Phase 0 - C11 8086 Architecture Implementation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QBE_DIR="$(dirname "$SCRIPT_DIR")"

# Paths to tools
MINIC="$QBE_DIR/minic/minic"
QBE="$QBE_DIR/qbe"

# Check if tools exist
if [ ! -f "$MINIC" ]; then
    echo -e "${RED}Error: MiniC compiler not found at $MINIC${NC}"
    echo -e "${YELLOW}Please build it first: cd minic && make${NC}"
    exit 1
fi

if [ ! -f "$QBE" ]; then
    echo -e "${RED}Error: QBE not found at $QBE${NC}"
    echo -e "${YELLOW}Please build it first: make${NC}"
    exit 1
fi

# Usage message
usage() {
    echo -e "${BLUE}Usage: $0 <input.c> [output_name]${NC}"
    echo ""
    echo "Compiles C source code to i8086 assembly for DOS"
    echo ""
    echo "Arguments:"
    echo "  input.c      - C source file to compile"
    echo "  output_name  - Output filename without extension (default: input filename)"
    echo ""
    echo "Outputs:"
    echo "  <name>.ssa - QBE IL intermediate representation"
    echo "  <name>.asm - i8086 assembly code"
    echo ""
    echo "Example:"
    echo "  $0 hello.c"
    echo "  $0 hello.c my_program"
    exit 1
}

# Parse arguments
if [ $# -lt 1 ]; then
    usage
fi

INPUT_FILE="$1"
if [ ! -f "$INPUT_FILE" ]; then
    echo -e "${RED}Error: Input file not found: $INPUT_FILE${NC}"
    exit 1
fi

# Determine output name
if [ $# -ge 2 ]; then
    OUTPUT_NAME="$2"
else
    OUTPUT_NAME="$(basename "$INPUT_FILE" .c)"
fi

SSA_FILE="${OUTPUT_NAME}.ssa"
ASM_FILE="${OUTPUT_NAME}.asm"

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}QBE DOS Build Pipeline${NC}"
echo -e "${BLUE}================================${NC}"
echo -e "Input:  ${GREEN}$INPUT_FILE${NC}"
echo -e "Output: ${GREEN}$OUTPUT_NAME${NC}"
echo ""

# Step 1: C to QBE IL
echo -e "${BLUE}[1/2] Compiling C to QBE IL...${NC}"
if ! "$MINIC" < "$INPUT_FILE" > "$SSA_FILE" 2>&1; then
    echo -e "${RED}✗ MiniC compilation failed${NC}"
    rm -f "$SSA_FILE"
    exit 1
fi
echo -e "${GREEN}✓ Generated: $SSA_FILE${NC}"
echo -e "${YELLOW}Size: $(wc -l < "$SSA_FILE") lines${NC}"

# Step 2: QBE IL to i8086 assembly
echo -e "${BLUE}[2/2] Generating i8086 assembly...${NC}"
if ! "$QBE" -t i8086 "$SSA_FILE" > "$ASM_FILE" 2>&1; then
    echo -e "${RED}✗ QBE code generation failed${NC}"
    rm -f "$ASM_FILE"
    exit 1
fi
echo -e "${GREEN}✓ Generated: $ASM_FILE${NC}"
echo -e "${YELLOW}Size: $(wc -l < "$ASM_FILE") lines${NC}"

echo ""
echo -e "${BLUE}================================${NC}"
echo -e "${GREEN}Build successful!${NC}"
echo -e "${BLUE}================================${NC}"
echo ""
echo -e "${YELLOW}Output files:${NC}"
echo "  QBE IL:   $SSA_FILE"
echo "  Assembly: $ASM_FILE"
echo ""
echo -e "${YELLOW}Next steps (manual for now):${NC}"
echo "  1. Assemble with NASM:  nasm -f obj $ASM_FILE -o ${OUTPUT_NAME}.obj"
echo "  2. Link (requires DOS linker and runtime library)"
echo ""

exit 0
