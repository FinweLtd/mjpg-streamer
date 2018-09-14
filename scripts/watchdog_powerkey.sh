#!/bin/bash

# Hint: disable Odroid power key from Ubuntu's Power Management
# (when power button is pressed: do nothing) and also from
# login daemon: sudo nano /etc/systemd/logind.conf where you
# need to set HandlePowerKey=ignore

EVENT="$(ls -l /dev/input/by-path | grep gpio | grep -Eo 'event[0-9]+')"
POWERKEY_CMD="evtest /dev/input/${EVENT} | unbuffer -p grep \"value 1\" | /home/odroid/handle_powerkey.sh"

echo "Power key watchdog starting"
bash -c "$POWERKEY_CMD" &
