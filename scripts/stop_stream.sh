#!/bin/bash

# Kill all mjpg_streamer processes
# -s 2 -> send "CTRL-C" signal to gracefully shutdown mjpg-streamer
# -w   -> wait until process is killed
killall -w -s 2 mjpg_streamer
echo "Stream stopped."
