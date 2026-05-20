#!/bin/bash

# Function to generate random characters
generate_random_string() {
    cat /dev/urandom | tr -dc 'a-zA-Z0-9' | head -c 10
}


for ((j=1; j<=10; j++)); do
    randomness=$(shuf -i 1-3 -n 1)
    random_line=$(shuf -i 1-100 -n $randomness)
    random_dir=$(shuf -i 1-2 -n 1)
    if [ ! -d "test" ]; then
        mkdir test
    fi
    if [ "$random_dir" -eq 1 ]; then
        output_dir="."
    else
        output_dir="./test"
    fi
    for ((i=1; i<=100; i++)); do
        if echo "$random_line" | grep -q "\b$i\b"; then
            echo -n $(whoami) >> "$output_dir/output-$j.txt"
        else
            generate_random_string >> "$output_dir/output-$j.txt"
        fi
	echo >> "$output_dir/output-$j.txt"
    done
done
