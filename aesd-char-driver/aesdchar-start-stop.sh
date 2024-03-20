#!/bin/sh

case "$1" in
    start)
        echo "Loading aesdchar module"
        start-stop-daemon -S -n init -a /etc/aesdchar_modules_script/aesdchar_load
        ;;
    stop)
        echo "Unloading aesdchar module"
        start-stop-daemon -S -n init -a /etc/aesdchar_modules_script/aesdchar_unload
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac