#!/bin/bash

#NOTE: it's the job of the script that inserts bookmarks to make sure there are no dupes.

file=${XDG_DATA_HOME:-$HOME/.local/share}/reuzbl/bookmarks
[ -r "$file" ] || exit

if [ -z "$DMENUSETTINGS" ]; then
    COLORS=" -nb #303030 -nf khaki -sb #CCFFAA -sf #303030"
else
    COLORS=$DMENUSETTINGS
fi

#if dmenu --help 2>&1 | grep -q '\[-rs\] \[-ni\] \[-nl\] \[-xs\]'
#then
	DMENU="dmenu -i -l 10" # vertical patch
	# show tags as well
	goto=`$DMENU $COLORS < $file | awk '{print $1}'`
#else
#	DMENU="dmenu -i"
#	# because they are all after each other, just show the url, not their tags.
#	goto=`awk '{print $1}' $file | $DMENU $COLORS`
#fi

if [ "$8" = "new" ]
then
    [ -n "$goto" ] && echo "chain 'event SET_KEYCMD @OpenInNewWindowKey' 'event APPEND_KEYCMD $goto' 'event KEYCMD_EXEC_CURRENT' 'event SET_KEYCMD'" | socat - unix-connect:$5
else
    [ -n "$goto" ] && echo "uri $goto" | socat - unix-connect:$5
fi
