#!/bin/bash

insmod mp3.ko
mknod node c 423 0

# Parameters for the work process
MEMORY_SIZE=200    # memory size in MB
LOCALITY="R"       # locality, R for Random
ACCESS_COUNT=10000 # number of memory accesses per iteration

# Number of instances to run
NUM_INSTANCES=22

echo "Starting $NUM_INSTANCES instances of the work process..."

for ((i = 1; i <= NUM_INSTANCES; i++)); do
    ./work $MEMORY_SIZE $LOCALITY $ACCESS_COUNT &
    echo "Started instance $i"
done

echo "All instances started."

