import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises } from 'fs';

import { DEFAULT_EXCLUDE } from './constants';

const mkdirp = promisify(mkp);
const copy = promisify(ncp);

export const build = async (baseDir: string, dest: string): Promise<void> => {
  try {
    const files = await promises.readdir(baseDir);
    mkdirp(dest);

    const operations: Promise<any>[] = [];

    for (const file of files) {
      if (DEFAULT_EXCLUDE.includes(file) || file === dest) continue;

      const promise = copy(join(baseDir, file), join(dest, file));
      operations.push(promise);
    }

    await Promise.all(operations);
  } catch (e) {
    console.error(e);
  }
};
