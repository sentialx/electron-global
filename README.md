# electron-runtime

A complete solution to build a ready for distribution, lightweight Electron app for Windows, macOS and Linux with auto updater support. Instead of bundling Electron with each app, electron-runtime will use a global installation of the corresponding Electron major version.

# Electron version

`electron-runtime` checks the app's `package.json` and downloads corresponding major version, minor and patch versions are ignored. The Electron versions are being saved to:

The `x` is major version of Electron, for example 6
- on macOS and Linux: `~/.electron-runtime/x`

Everytime an Electron app packaged with `electron-runtime` starts, it checks in the background process if there's any newer Electron version regarding `minor` and `patch` (WIP).
