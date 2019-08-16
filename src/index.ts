import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join, resolve } from 'path';
import { promises } from 'fs';
import * as rmrf from 'rimraf';
import * as semver from 'semver';
import * as phin from 'phin';
import { platform } from 'os';
import { spawnSync } from 'child_process';
import rebuild from 'electron-rebuild';

import { DEFAULT_EXCLUDE } from './constants';

const asar = require('asar');

const rimraf = promisify(rmrf);
const mkdirp = promisify(mkp);
const copy = promisify(ncp);

const os = platform();
const npm = os === 'win32' ? 'npm.cmd' : 'npm';

export const installDependencies = async (
  dir: string,
  electronVersion: string,
): Promise<void> => {
  spawnSync(npm, ['install', '--only=prod'], { cwd: dir });
  await rebuild({ buildPath: resolve(dir), electronVersion });
};

export const build = async (baseDir: string, dest: string): Promise<number> => {
  try {
    const resources = join(dest, 'resources');

    const operations: Promise<any>[] = [];

    await mkdirp(resources);

    const pkg = JSON.parse(
      await promises.readFile(join(baseDir, 'package.json'), 'utf8'),
    );

    let electronVersion: semver.SemVer;

    if (pkg) {
      if (pkg.devDependencies && pkg.devDependencies.electron) {
        electronVersion = semver.minVersion(pkg.devDependencies.electron);
      } else {
        throw new Error('Electron should be installed as a dev dependency.');
      }
    } else {
      throw new Error(
        'package.json file does not exist in the specified directory.',
      );
    }

    const files = await promises.readdir(baseDir);

    for (const file of files) {
      if (DEFAULT_EXCLUDE.includes(file) || file === dest) continue;

      const promise = copy(join(baseDir, file), join(resources, file));
      operations.push(promise);
    }

    await Promise.all(operations);

    await installDependencies(resources, electronVersion.version);

    await asar.createPackage(resources, join(dest, 'app.asar'));
    await rimraf(resources);

    return electronVersion.major;
  } catch (e) {
    console.log(e);
  }

  return null;
};

export const buildMac = async (
  dest: string,
  version: number,
): Promise<void> => {
  const appPath = join(dest, 'App.app/Contents/MacOS');

  await mkdirp(appPath);

  await promises.writeFile(join(appPath, 'electron_version'), version);

  await copy(join(dest, 'app.asar'), join(appPath, 'app.asar'));
  await rimraf(join(dest, 'app.asar'));

  await copy(join(__dirname, '../scripts/darwin.sh'), join(appPath, 'App'));

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
    x.tag_name.startsWith(`v${version}`),
  );

  const asset = release.assets.find(
    (x: any) => x.name === `electron-${release.tag_name}-darwin-x64.zip`,
  );

  await promises.writeFile(
    join(appPath, 'electron_url'),
    asset.browser_download_url,
  );
};

export const testBuild = async (
  baseDir: string,
  dest: string,
): Promise<void> => {
  try {
    const appPath = join(dest, 'Electron.app/Contents/MacOS');

    await mkdirp(appPath);

    const pkg = JSON.parse(
      await promises.readFile(join(baseDir, 'package.json'), 'utf8'),
    );

    let electronVersion: semver.SemVer;

    if (pkg) {
      if (pkg.devDependencies && pkg.devDependencies.electron) {
        electronVersion = semver.minVersion(pkg.devDependencies.electron);
      } else {
        throw new Error('Electron should be installed as a dev dependency.');
      }
    } else {
      throw new Error(
        'package.json file does not exist in the specified directory.',
      );
    }

    await promises.writeFile(
      join(appPath, 'electron_version'),
      electronVersion.major,
    );

    await copy(
      join(__dirname, '../scripts/darwin.sh'),
      join(appPath, 'Electron'),
    );

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
      x.tag_name.startsWith(`v${electronVersion.major}`),
    );

    const asset = release.assets.find(
      (x: any) => x.name === `electron-${release.tag_name}-darwin-x64.zip`,
    );

    await promises.writeFile(
      join(appPath, 'electron_url'),
      asset.browser_download_url,
    );
  } catch (e) {
    console.log(e);
  }
};
