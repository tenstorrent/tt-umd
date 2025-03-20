#!/bin/bash

# Define the command to run
COMMAND="./build/test/umd/unified/unified_tests"

# Number of times to run the command
RUNS=100

for ((i=1; i<=RUNS; i++)); do
    echo "Running attempt $i..."
    
    # Run the command
    $COMMAND
    
    # Check the exit status
    if [[ $? -ne 0 ]]; then
        echo "Error encountered on attempt $i. Stopping execution."
        exit 1
    fi
done

echo "All attempts completed successfully."