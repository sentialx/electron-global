#!/bin/bash

DIR=$(cd "$(dirname "$0")"; pwd)

url=`cat $DIR/electron_url`
major=`cat $DIR/electron_version`

dir="$HOME/.electron-runtime/$major"

electron="$dir/Electron.app/Contents/MacOS/Electron $DIR/../Resources/app.asar"

if [ -d $dir ]; then
  $electron &
else
  mkdir -p $dir
  cd $dir

  curl -o "electron.zip" $url -L
  unzip "electron.zip"

  $electron &
fi
