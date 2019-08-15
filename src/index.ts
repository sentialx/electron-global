import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises } from 'fs';
import * as rmrf from 'rimraf';
import * as semver from 'semver';
import * as phin from 'phin';

import { DEFAULT_EXCLUDE } from './constants';

const asar = require('asar');

const rimraf = promisify(rmrf);
const mkdirp = promisify(mkp);
const copy = promisify(ncp);

export const build = async (baseDir: string, dest: string): Promise<void> => {
  try {
    const resources = join(dest, 'resources');
    const operations: Promise<any>[] = [];

    await mkdirp(resources);

    const pkg = JSON.parse(
      await promises.readFile(join(baseDir, 'package.json'), 'utf8'),
    );

    let electronMajor: number;

    if (pkg) {
      if (pkg.devDependencies && pkg.devDependencies.electron) {
        electronMajor = semver.minVersion(pkg.devDependencies.electron).major;
      } else {
        throw new Error('Electron should be installed as a dev dependency.');
      }
    } else {
      throw new Error(
        'package.json file does not exist in the specified directory.',
      );
    }

    console.log('Fetching Electron releases...');

    const res = await phin({
      url: 'https://api.github.com/repos/electron/electron/releases',
      parse: 'json',
      headers: {
        'User-Agent':
          'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_14_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/76.0.3809.100 Safari/537.36',
      },
    });

    const release = res.body.find((x: any) =>
      x.tag_name.startsWith(`v${electronMajor}`),
    );

    const asset = release.assets.find(
      (x: any) => x.name === `electron-${release.tag_name}-darwin-x64.zip`,
    );

    await promises.writeFile(
      join(dest, 'electron_url'),
      asset.browser_download_url,
    );

    const files = await promises.readdir(baseDir);

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
