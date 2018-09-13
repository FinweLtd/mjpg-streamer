#!/bin/bash

# Hint: disable Odroid power key from Ubuntu's Power Management
# (when power button is pressed: do nothing) and also from
# login daemon: sudo nano /etc/systemd/logind.conf where you
# need to set HandlePowerKey=ignore

POWERKEY_CMD="evtest /dev/input/event0 | unbuffer -p grep \"value 1\" | /home/odroid/handle_powerkey.sh"

echo "Power key watchdog starting"
bash -c "$POWERKEY_CMD" &
