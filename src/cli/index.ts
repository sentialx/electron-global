#!/usr/bin/env node

import program from 'commander';

program.option('-m', '--mac', 'Build for macOS');

program.parse(process.argv);

if (program.mac) {
  console.log('Hooray!');
}
