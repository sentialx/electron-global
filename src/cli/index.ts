#!/usr/bin/env node

import * as program from 'commander';
import { build } from '..';
import { DEFAULT_DEST } from '../constants';

program.option('-m, --mac', 'Build for macOS');

program.parse(process.argv);

const basePath = process.cwd();

(async function(): Promise<void> {
  if (program.mac) {
    build(basePath, DEFAULT_DEST);
  }
})();
