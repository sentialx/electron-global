#!/usr/bin/env node

import * as program from 'commander';
import { createElectronDistMac } from '..';
import { DEFAULT_DEST } from '../constants';

program.option('-m, --mac', 'Create Electron dist for macOS.');
program.option('-l, --linux', 'Create Electron dist for Linux.');
program.option('-w, --windows', 'Create Electron dist for Windows.');
program.option(
  '--projectDir, --project',
  'The path to project directory. Defaults to current working directory.',
);

program.parse(process.argv);

(async function(): Promise<void> {
  if (program.mac) {
    await createElectronDistMac(
      program.projectDir ? program.projectDir : process.cwd(),
      DEFAULT_DEST,
    );
  }
})();
