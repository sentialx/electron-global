#!/usr/bin/env node

import * as program from 'commander';
import {
  createDistMac,
  createDistWindows,
  downloadBinaries,
  createDistLinux,
} from '..';
import { DEFAULT_DEST } from '../constants';

program.option('-m, --mac', 'Create Electron dist for macOS.');
program.option('-l, --linux', 'Create Electron dist for Linux.');
program.option('-w, --windows', 'Create Electron dist for Windows.');
program.option(
  '-o, --output <path>',
  'Output path of the created Electron runtime launcher. Defaults to `./electron-runtime`.',
);
program.option(
  '--projectDir, --project <path>',
  'The path to project directory. Defaults to current working directory.',
);

program.parse(process.argv);

(async function(): Promise<void> {
  if (program.mac || program.windows || program.linux) {
    if (program.mac) {
      await downloadBinaries('darwin');
      await createDistMac(
        program.projectDir ? program.projectDir : process.cwd(),
        program.output ? program.output : DEFAULT_DEST,
      );
    }

    if (program.windows) {
      await downloadBinaries('win32');
      await createDistWindows(
        program.projectDir ? program.projectDir : process.cwd(),
        program.output ? program.output : DEFAULT_DEST,
      );
    }

    if (program.linux) {
      await downloadBinaries('linux');
      await createDistLinux(
        program.projectDir ? program.projectDir : process.cwd(),
        program.output ? program.output : DEFAULT_DEST,
      );
    }

    console.log('Done!');
  }
})();
