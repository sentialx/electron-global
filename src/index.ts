import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises } from 'fs';
import * as rmrf from 'rimraf';
import * as semver from 'semver';

import { DEFAULT_EXCLUDE } from './constants';

const asar = require('asar');

const rimraf = promisify(rmrf);
const mkdirp = promisify(mkp);
const copy = promisify(ncp);

export const build = async (baseDir: string, dest: string): Promise<void> => {
  try {
    const resources = join(dest, 'resources');
    const operations: Promise<any>[] = [];

    const pkg = JSON.parse(
      await promises.readFile(join(baseDir, 'package.json'), 'utf8'),
    );

    let electronVersion: string;

    if (pkg) {
      if (pkg.devDependencies && pkg.devDependencies.electron) {
        electronVersion = semver.clean(pkg.devDependencies.electron);
      } else {
        throw new Error('Electron should be installed as a dev dependency.');
      }
    } else {
      throw new Error(
        'package.json file does not exist in the specified directory.',
      );
    }

    await promises.writeFile(join(dest, 'electron_version'), electronVersion);

    const files = await promises.readdir(baseDir);

    await mkdirp(resources);

    for (const file of files) {
      if (DEFAULT_EXCLUDE.includes(file) || file === dest) continue;

      const promise = copy(join(baseDir, file), join(resources, file));
      operations.push(promise);
    }

    await Promise.all(operations);
    await asar.createPackage(resources, join(dest, 'app.asar'));
    await rimraf(resources);
  } catch (e) {
    console.log(e);
  }
};
