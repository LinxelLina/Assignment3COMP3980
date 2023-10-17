#!/usr/bin/env bash

# Function to run make clean all in directories with Makefile
run_make()
{
    local current_dir="$1"

    echo "Running 'make clean' in $current_dir..."
    make clean
}

run_make "."
