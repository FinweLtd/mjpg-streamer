#!/bin/bash

while read line; do
	echo "Power key pressed, stopping stream..."
	bash -c "/home/odroid/stop_stream.sh"
done < /dev/stdin
