#!/bin/sh

set -eux

CMD="./mid_lts"
PAGES=16

if [ ! -f "$CMD" ]; then
	echo "You need to compile $CMD first, buddy!"
	exit 1
fi

while true; do
	NSESS=$((RANDOM % 10 + 1))
	$CMD -p $PAGES -x $NSESS >> mid_lts.log
done
