#!/bin/bash

# Script to extract mangled names from dc.b strings
# Usage: ./test.sh <input_file> [output_file]

set -euo pipefail

# Check if input file is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <input_file> [output_file]"
    echo "Extracts mangled names from dc.b strings in the input file"
    exit 1
fi


INPUT_FILE="$1"
SUCCESS_FILE="test_success.txt"
FAILURE_FILE="test_failures.txt"

# Clear output files
> "$SUCCESS_FILE"
> "$FAILURE_FILE"

# Use grep and sed to extract strings from dc.b lines
# Pattern explanation:
# - grep finds lines containing 'dc.b'
# - sed extracts content between quotes after dc.b
# - The regex looks for dc.b followed by any characters, then a quote, captures everything until the closing quote
mangled_names=$(grep 'dc\.b' "$INPUT_FILE" | sed -n 's/.*dc\.b[^"]*"\([^"]*\)".*/\1/p')

if [ -z "$mangled_names" ]; then
    echo "ERROR: No mangled names found" >&2
    exit 1
fi

total_count=0
success_count=0
failure_count=0

while IFS= read -r mangled_name; do
    if [ -n "$mangled_name" ]; then
        total_count=$((total_count + 1))
        echo "Processing: $mangled_name"

        # Run MacsBugDemangler and capture output and exit code
        if demangled_output=$(./MacsBugDemangler "$mangled_name" 2>&1); then
            # Success case
            echo "$mangled_name: $demangled_output" | tee -a "$SUCCESS_FILE"
            success_count=$((success_count + 1))
        else
            # Failure case
            echo "$mangled_name: $demangled_output" | tee -a "$FAILURE_FILE"
            failure_count=$((failure_count + 1))
        fi
    fi
done <<< "$mangled_names"

echo ""
echo "Total processed: $total_count"
echo "Successful demanglings: $success_count (saved to $SUCCESS_FILE)"
echo "Failed demanglings: $failure_count (saved to $FAILURE_FILE)"
