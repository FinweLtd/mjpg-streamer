#!/bin/bash

# Script for starting streaming and optionally (with --watchdog switch) 
# keep it running forever with a watchdog that restarts it when the 
# process dies or is killed.

# For auto-start on boot, type "crontab -e" and add the following lines:
# @reboot /home/odroid/watchdog_powerkey.sh
# @reboot /home/odroid/start_stream.sh --watchdog

MJPG_STREAMER_PATH="/home/odroid/Source/GitHub/mjpg-streamer/mjpg-streamer-experimental"
INPUT_PLUGIN="${MJPG_STREAMER_PATH}/input_pylon.so"
OUTPUT_PLUGIN="${MJPG_STREAMER_PATH}/output_http.so -p 8080 -w ${MJPG_STREAMER_PATH}/www"
MJPG_STREAMER_CMD="${MJPG_STREAMER_PATH}/mjpg_streamer -i \"${INPUT_PLUGIN}\" -o \"${OUTPUT_PLUGIN}\""

if [[ $1 == "--watchdog" ]]
then
	echo "Watchdog starting"
	until bash -c "$MJPG_STREAMER_CMD"; do
		echo "Stream stopped with exit code  $?. Restarting in 5 seconds..." >&2
		sleep 5
	done
else
	echo "Stream starting"
	echo $MJPG_STREAMER_CMD
	bash -c "$MJPG_STREAMER_CMD" &
fi
