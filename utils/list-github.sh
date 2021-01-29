#!/bin/bash

# $@: args
my_ghi() {
	ghi list --sort created --no-pulls "${@}" | \
		sed "s/ [0-9]\+$//g;s/ [0-9]\+ @/ @/g;s/ $//g"
}

LAST_WEEK="$(date -dlast-week +%Y-%m-%d)"

echo "    Recently opened (latest from last week: *TODO*)"
echo
my_ghi --state open --since "${LAST_WEEK}"

echo
echo "    Bugs (opened, flagged as \"bug\" and assigned)"
echo
my_ghi --state open -L bug | grep " @"

echo
echo "    Bugs (opened and flagged as \"bug\" and not assigned)"
echo
my_ghi --state open -L bug | grep -v " @"

echo
echo "    In Progress (opened and assigned)"
echo
my_ghi --state open -N bug | grep " @"

echo
echo "    Recently closed (since last week)"
echo
my_ghi --state closed --since "${LAST_WEEK}"
