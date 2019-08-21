PACKAGE_VERSION=$(cat package.json \
  | grep version \
  | head -1 \
  | awk -F: '{ print $2 }' \
  | sed 's/[",]//g' \
  | tr -d '[[:space:]]')

mkdir -p build

export TRAVIS_TAG="v${PACKAGE_VERSION}"

if [[ "$(uname)" == "Darwin" ]]; then
  make -f makefile.darwin
  OS="darwin"
else
  make -f makefile.linux
  OS="linux"
fi

echo $OS
echo $TRAVIS_TAG

zip -r "build/electron-launcher-${TRAVIS_TAG}-${OS}-ia32.zip" "build/electron-launcher"
