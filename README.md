# electron-global

[![Travis](https://img.shields.io/travis/com/sentialx/electron-global.svg?style=flat-square)](https://travis-ci.com/sentialx/electron-global)
[![Downloads](https://img.shields.io/github/downloads/sentialx/electron-global/total.svg?style=flat-square)](https://github.com/sentialx/electron-global/releases)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-brightgreen?style=flat-square)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=VCPPFUAL4R6M6&source=url)
[![Discord](https://discordapp.com/api/guilds/307605794680209409/widget.png?style=shield)](https://discord.gg/P7Vn4VX)

A tool for building lightweight Electron apps using a global Electron instance. Forget about 100MB for a Hello World app in Electron!

# How it works?

`electron-global` creates a custom Electron distributable with a small app launcher which checks the app's `package.json` and downloads corresponding `major` version and the newest in case of `minor` and `patch`. The Electron versions are being saved to:

- on macOS and Linux: `~/.electron-global/x`
- on Windows: `%HOMEPATH%/.electron-global/x`

Where `x` is the major version of Electron (e.g. 6).

Then the distributable can be used with [`electron-builder`](https://github.com/electron-userland/electron-builder) to build the app installers.

# Installation

The [`electron-builder`](https://github.com/electron-userland/electron-builder) package is also required to successfully build an app.

```
$ npm install --save-dev electron-global electron-builder
```

# Usage

You need to create `electron-builder.json` file in your project directory, configure it and specify `electronDist` directory to where the `electron-global` generates the output (default is `./electron-global`). Example:

```
{
  ...
  "electronDist": "./electron-global"
  ...
}
```

Then you can run the following command (example for macOS):

```
$ electron-global -m && electron-builder -m
```

## CLI Options:
```
Usage: electron-global [options]

Options:
  -m, --mac                Create Electron dist for macOS.
  -l, --linux              Create Electron dist for Linux.
  -w, --windows            Create Electron dist for Windows.
  -o, --output <path>      Output path of the created Electron runtime launcher. Defaults to `./dist/runtime`.
  --projectDir, --project  The path to project directory. Defaults to current working directory.
  -h, --help               output usage information
```
