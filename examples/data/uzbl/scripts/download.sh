#!/bin/sh
# extended download script
# uses mimetype and mime list to determine what to do with downloads

do=`echo -e "download\nabbrechen\nopen"|dmenu`
if test "$do" != "abbrechen";
then

# Some sites block the default wget --user-agent..
GET='wget --content-disposition --user-agent=Firefox -c -nv'
# Get the Filename (with WGET, extract from stderr)
GET_RETURN_FN=' 2>&1 | sed "s/^.* -> \"//g; s/\".*//g"'
# 4 torrents
TGET='ctorrent -D 100 -U 10 -e 1 -S '`hostname`':2780'

dest="$HOME"
url="$8"
http_proxy="$9"
export http_proxy

test "x$url" = "x" && { notify-send "uzbl-dl" "you must supply a url! ($url)"; exit 1; }

shift
if test "$do" = "open";
then
  (
    cd "$dest"
    file=`eval "$GET" "\"$url\"" "$GET_RETURN_FN"`
    if test "x$file" != "x";
    then
      app=`grep \`file -izb "$file" | sed 's/;.*//g'\` $HOME/.mimeapps | cut -f 2`
      if test "x$app" != "x";
      then
        eval "$app" "\"$dest/$file\"" &
      else
	notify-send "Download finished:" "$url"
      fi
    fi
  )
else
  if echo "$url" | grep -E '.*\.torrent' >/dev/null;
  then
      ( cd "$dest"; eval "$TGET" "\"$url\"")
  else
      ( cd "$dest"; eval "$GET" "\"$url\""; )
  fi
  notify-send "Download finished:" "$url"
fi

fi
