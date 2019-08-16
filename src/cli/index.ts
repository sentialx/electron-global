#!/usr/bin/env node

import * as program from 'commander';
import { build, buildMac, testBuild } from '..';
import { DEFAULT_DEST } from '../constants';

program.option('-m, --mac', 'Build for macOS.');
program.option(
  '--projectDir, --project',
  'The path to project directory. Defaults to current working directory.',
);

program.parse(process.argv);

(async function(): Promise<void> {
  await testBuild(
    program.projectDir ? program.projectDir : process.cwd(),
    DEFAULT_DEST,
  );

  /*if (program.mac) {
    buildMac(DEFAULT_DEST, version);
  }*/
})();
