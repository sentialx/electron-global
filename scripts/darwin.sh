#!/bin/sh
url=`cat electron_url`
major=`cat electron_version`

path = "~/.electron-runtime/$major"

if [ -d $path ]; then

else
  mkdir -p $path
  cd $path

  curl -o "electron.zip" $url -L
  unzip "electron.zip"
fi
