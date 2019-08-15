#!/usr/bin/env node

import * as program from 'commander';

program.option('-m, --mac', 'Build for macOS');

program.parse(process.argv);

if (program.mac) {
  console.log('Hooray!');
}
