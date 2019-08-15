#!/usr/bin/env node

import * as program from 'commander';
import { join } from 'path';
import { promises, existsSync } from 'fs';
import { promisify } from 'util';
import { ncp } from 'ncp';

const copy = promisify(ncp);

program.option('-m, --mac', 'Build for macOS');

program.parse(process.argv);

const basePath = process.cwd();

const DEST = 'dist';

const EXCLUDE = ['.git', 'node_modules', DEST];

(async function(): Promise<void> {
  if (program.mac) {
    try {
      const files = await promises.readdir(basePath);
      if (!existsSync(DEST)) {
        await promises.mkdir(DEST);
      }

      const operations: Promise<any>[] = [];

      for (const file of files) {
        if (EXCLUDE.includes(file)) continue;

        const promise = copy(join(basePath, file), join(DEST, file));
        operations.push(promise);
      }

      await Promise.all(operations);
    } catch (e) {
      console.error(e);
    }
  }
})();
