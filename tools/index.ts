import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises, createWriteStream } from 'fs';
import * as semver from 'semver';
import { request } from 'https';
import * as extract from 'extract-zip';

const pkg = require('../package.json');

const mkdirp = promisify(mkp);
const copy = promisify(ncp);

export const getElectronVersion = async (
  baseDir: string,
): Promise<semver.SemVer> => {
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

  if (!electronVersion) throw new Error('Failed to parse Electron version.');

  return electronVersion;
};

export const downloadBinaries = (
  os: 'win32' | 'linux' | 'darwin',
): Promise<void> => {
  return new Promise(
    async (resolve, reject): Promise<void> => {
      const url = `https://github.com/sentialx/electron-global/releases/download/v${pkg.version}/electron-v${pkg.version}-${os}-ia32.zip`;
      const zipPath = join(
        __dirname,
        `../download/${os}/electron-v${pkg.version}-${os}-ia32.zip`,
      );

      const stream = createWriteStream(zipPath);

      const req = request(
        url,
        {
          headers: {
            'User-Agent': `electron-global/${pkg.version}`,
          },
        },
        res => {
          res.pipe(stream);

          res.on('end', () => {
            extract(
              zipPath,
              { dir: join(__dirname, `../download/${os}`) },
              err => {
                if (err) return reject(err);
                resolve();
              },
            );
          });
        },
      );

      req.on('error', err => {
        reject(err);
      });
    },
  );
};

export const createDistWindows = async (
  baseDir: string,
  dest: string,
): Promise<void> => {
  try {
    await mkdirp(dest);

    const electronVersion = await getElectronVersion(baseDir);

    await promises.writeFile(
      join(dest, 'electron_version'),
      electronVersion.major,
    );

    await copy(
      join(__dirname, '../download/win32/electron.exe'),
      join(dest, 'electron.exe'),
    );
  } catch (e) {
    console.error(e);
  }
};

export const createDistLinux = async (
  baseDir: string,
  dest: string,
): Promise<void> => {
  try {
    await mkdirp(dest);

    const electronVersion = await getElectronVersion(baseDir);

    await promises.writeFile(
      join(dest, 'electron_version'),
      electronVersion.major,
    );

    await copy(
      join(__dirname, '../download/linux/electron'),
      join(dest, 'electron'),
    );
  } catch (e) {
    console.error(e);
  }
};

export const createDistMac = async (
  baseDir: string,
  dest: string,
): Promise<void> => {
  try {
    const contentsPath = join(dest, 'Electron.app/Contents');
    const helperContentsPath = join(
      contentsPath,
      'Frameworks/Electron Helper.app/Contents',
    );

    await mkdirp(dest);
    await mkdirp(join(contentsPath, 'MacOS'));
    await mkdirp(join(helperContentsPath, 'MacOS'));
    await mkdirp(join(contentsPath, 'Resources'));

    const electronVersion = await getElectronVersion(baseDir);

    await promises.writeFile(
      join(contentsPath, 'Resources/electron_version'),
      electronVersion.major,
    );

    await promises.writeFile(
      join(helperContentsPath, 'MacOS/Electron Helper'),
      '',
    );

    await Promise.all([
      copy(
        join(__dirname, '../download/darwin/electron'),
        join(contentsPath, 'MacOS/Electron'),
      ),
      copy(
        join(__dirname, '../resources/darwin/electron.plist'),
        join(contentsPath, 'Info.plist'),
      ),
      copy(
        join(__dirname, '../resources/darwin/electron-helper.plist'),
        join(helperContentsPath, 'Info.plist'),
      ),
    ]);
  } catch (e) {
    console.log(e);
  }
};
