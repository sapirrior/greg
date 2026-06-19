#!/usr/bin/env bash

# High-precision manual timing runner
PATTERN="1"
DIR="$HOME"

# Check tool argument
if [ -z "$1" ]; then
    echo "Usage: $0 <tool_id>"
    echo "Available tools: greg, rg, ag, ugrep"
    exit 1
fi

TOOL="$1"
case "$TOOL" in
    greg)
        if [ ! -f "./greg" ]; then
            echo "Error: ./greg is not built. Run 'make' first."
            exit 1
        fi
        cmd="./greg \"$PATTERN\" \"$DIR\""
        ;;
    rg)
        cmd="rg \"$PATTERN\" \"$DIR\""
        ;;
    ag)
        cmd="ag \"$PATTERN\" \"$DIR\""
        ;;
    ugrep)
        cmd="ugrep -R \"$PATTERN\" \"$DIR\""
        ;;
    *)
        echo "Unknown tool: $TOOL"
        echo "Available tools: greg, rg, ag, ugrep"
        exit 1
        ;;
esac

# Execute the exact command with shell timing redirected to /dev/null
echo "Executing: time $cmd > /dev/null"
echo "--------------------------------------"
time eval "$cmd" > /dev/null
