import { ncp } from 'ncp';
import * as mkp from 'mkdirp';
import { promisify } from 'util';
import { join } from 'path';
import { promises } from 'fs';
import * as semver from 'semver';
import * as phin from 'phin';

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

export const getElectronURL = async (major: number): Promise<string> => {
  const res = await phin({
    url: 'https://api.github.com/repos/electron/electron/releases',
    parse: 'json',
    headers: {
      'User-Agent':
        'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_14_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/76.0.3809.100 Safari/537.36',
    },
  });

  const release = res.body.find((x: any) => x.tag_name.startsWith(`v${major}`));

  const asset = release.assets.find(
    (x: any) => x.name === `electron-${release.tag_name}-darwin-x64.zip`,
  );

  return asset.browser_download_url;
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

    await mkdirp(join(contentsPath, 'MacOS'));
    await mkdirp(join(helperContentsPath, 'MacOS'));

    const electronVersion = await getElectronVersion(baseDir);

    await promises.writeFile(
      join(contentsPath, 'MacOS/electron_version'),
      electronVersion.major,
    );

    await promises.writeFile(
      join(helperContentsPath, 'MacOS/Electron Helper'),
      '',
    );

    const url = await getElectronURL(electronVersion.major);

    await promises.writeFile(join(contentsPath, 'MacOS/electron_url'), url);

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
