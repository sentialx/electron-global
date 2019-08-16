# electron-runtime

A tool for building lighweight Electron apps using a global Electron instance.

![image](https://user-images.githubusercontent.com/11065386/63189647-b526eb00-c064-11e9-9280-c2148d8ae99e.png)

(`electron-quick-start` repo built with `electron-runtime` and `electron-builder`)

# How it works?

`electron-runtime` creates a custom Electron distributable with a small app launcher which checks the app's `package.json` and downloads corresponding `major` version and the newest in case of `minor` and `patch`. The Electron versions are being saved to:

- on macOS and Linux: `~/.electron-runtime/x`

Where `x` is the major version of Electron (e.g. 6).

Then the distributable can be used with [`electron-builder`](https://github.com/electron-userland/electron-builder) to build the app installers.

# Installation

The [`electron-builder`](https://github.com/electron-userland/electron-builder) package is also required to successfully build an app.

```
$ npm install --save-dev electron-runtime electron-builder
```

# Usage

You need to create `electron-builder.json` file in your project directory, configure it and specify `electronDist` directory to where the `electron-runtime` generates the output (default is `./electron-runtime`). Example:

```
{
  ...
  "electronDist": "./electron-runtime"
  ...
}
```

Then you can run the following command (example for macOS):

```
$ electron-runtime -m && electron-builder -m
```

## CLI Options:
```
Usage: electron-runtime [options]

Options:
  -m, --mac                Create Electron dist for macOS.
  -l, --linux              Create Electron dist for Linux.
  -w, --windows            Create Electron dist for Windows.
  -o, --output <path>      Output path of the created Electron runtime launcher. Defaults to `./dist/runtime`.
  --projectDir, --project  The path to project directory. Defaults to current working directory.
  -h, --help               output usage information
```
