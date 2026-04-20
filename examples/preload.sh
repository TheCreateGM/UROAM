#!/bin/bash

# Example script to run applications with memopt library preloaded

# Check if library exists
if [ ! -f /usr/local/lib/libmemopt.so ]; then
    echo "libmemopt.so not found. Run 'make install' first."
    exit 1
fi

# Enable core optimizations
memopt enable

# Set workload type based on argument
case "$1" in
    inference)
        MEMOPT_WORKLOAD=inference
        ;;
    training)
        MEMOPT_WORKLOAD=training
        ;;
    gaming)
        MEMOPT_WORKLOAD=gaming
        ;;
    *)
        MEMOPT_WORKLOAD=general
        ;;
esac

export MEMOPT_WORKLOAD
export LD_PRELOAD=/usr/local/lib/libmemopt.so:$LD_PRELOAD

echo "Running with memopt enabled (workload: $MEMOPT_WORKLOAD)"
echo "Command: $2"

# Run the command
shift
exec "$@"
