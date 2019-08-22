import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises } from 'fs';
import * as semver from 'semver';

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

export const createElectronDistMac = async (
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
        join(__dirname, '../resources/darwin/start.sh'),
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
