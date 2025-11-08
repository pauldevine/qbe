#!/bin/sh
# Build script for QBE with OpenWatcom (for running on Mac/Linux)
# Assumes OpenWatcom is installed and environment is set up

if [ "$1" = "32" ]; then
    echo "Building QBE for 32-bit DOS (protected mode with DOS4GW)..."
    echo "This is the recommended build method."
    wmake -f watcom32.mak
elif [ "$1" = "16" ] || [ -z "$1" ]; then
    echo "Building QBE for 16-bit DOS (real mode)..."
    echo "WARNING: This may fail due to memory constraints!"
    wmake -f watcom.mak
else
    echo "Invalid argument. Use: $0 [16|32]"
    exit 1
fi
