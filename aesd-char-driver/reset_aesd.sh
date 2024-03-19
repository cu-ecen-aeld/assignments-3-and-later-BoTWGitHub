#!/bin/sh

sudo ./aesdchar_unload
sudo truncate -s 0 /var/log/syslog
make clean
make
sudo ./aesdchar_load
sudo chmod 666 /dev/aesdchar
