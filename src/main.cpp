#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>
#include <thread>

#include <curl/curl.h>
#include "lib/filesystem.hpp"
#include "lib/libui/ui.h"
#include "lib/rapidjson/include/rapidjson/document.h"
#include "lib/zip/src/zip.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define PROGRAM_NAME "electron-launcher"
#define PROGRAM_VERSION "0.1"

#define BIN_DIR ".electron-global"

#if defined(WIN32) || defined(_WIN32)
#define OS "win32"
#define HOME_ENV "HOMEPATH"
#define ELECTRON_VERSION_PATH "electron_version"
#define ASAR_PATH "resources/app.asar"
#elif defined(__APPLE__)
#define OS "darwin"
#define HOME_ENV "HOME"
#define ELECTRON_VERSION_PATH "../Resources/electron_version"
#define ASAR_PATH "../Resources/app.asar"
#elif defined(__linux__)
#define OS "linux"
#define HOME_ENV "HOME"
#define ELECTRON_VERSION_PATH "electron_version"
#define ASAR_PATH "resources/app.asar"
#else
#error OS not detected
#endif

#if defined(__i386__) || (defined(_WIN32) && !defined(_WIN64))
#define ARCH "ia32"
#elif defined(__x86_64__) || defined(_WIN64)
#define ARCH "x64"
#elif defined(__ARM_ARCH_7__)
#define ARCH "armv7l"
#elif defined(__aarch64__)
#define ARCH "arm64"
#else
#error CPU architecture not detected
#endif

#define BUILDARCHSTRING OS "-" ARCH

namespace fs = ghc::filesystem;

struct CurlBuffer {
  char *buffer;
  size_t length;
  size_t size;
};

uiWindow *window = NULL;
uiLabel *label = NULL;
uiProgressBar *progressBar = NULL;

fs::path dest;
fs::path zipPath;
fs::path binPath;

std::string electronVersion;

bool done = false;

fs::path getHomePath(std::string subpath) {
  return fs::path(getenv(HOME_ENV)) / subpath;
}

void cancel() {
  fs::remove_all(dest);
  fs::remove(zipPath);

  exit(0);
}

void clean() {
  fs::remove_all(dest);
  fs::remove(zipPath);
}

static void showError(void *arg) {
  const char *message = (const char *)arg;

  uiMsgBoxError(window, "Error installing Electron", message);

  exit(1);
}

void error(const char *message, ...) {
  static char buffer[512];
  va_list args;
  va_start(args, message);
  vsnprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), message, args);
  va_end(args);

  std::cout << buffer << std::endl;

  uiQueueMain(showError, (void *)buffer);
}

void onCancelClicked(uiButton *b, void *data) { cancel(); }

int onWindowClose(uiWindow *w, void *data) {
  cancel();
  return 1;
}

void setStatus(const char *status) { uiLabelSetText(label, status); }

bool extract(fs::path archive, fs::path path) {
  return zip_extract((const char *)archive.c_str(), (const char *)path.c_str(),
                     NULL, NULL) == 0;
}

static size_t onWrite(const char *contents, size_t size, size_t nmemb,
                      void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

static size_t onWriteFile(const char *ptr, size_t size, size_t nmemb,
                          void *userdata) {
  FILE *file = (FILE *)userdata;

  return fwrite(ptr, size, nmemb, file);
}

static int onProgress(void *clientp, double dltotal, double dlnow,
                      double ultotal, double ulnow) {
  static unsigned long lastUiTime = 0;

  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  if (now - lastUiTime > 1000 / 60) {
    lastUiTime = now;

    uiProgressBarSetValue(progressBar,
                          dltotal > 0 ? (int)((dlnow / dltotal) * 100) : 0);
  }

  return 0;
}

std::string fetch(const char *url) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    error("Error initializing libcurl");
    return "";
  }

  std::string readBuffer;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWrite);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

  CURLcode response = curl_easy_perform(curl);

  if (response != CURLE_OK) {
    if (response != CURLE_ABORTED_BY_CALLBACK) {
      switch (response) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
          error(
              "Could not connect (%i)\nPlease ensure you have access "
              "to the internet",
              response);
          break;
        default:
          error("Error retrieving %s\n%s", url, curl_easy_strerror(response));
      }
    }
  }

  curl_easy_cleanup(curl);

  return readBuffer;
}

bool download(std::string url, const char *filename) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    clean();
    error("Error initializing curl");
    return false;
  }

  FILE *file = fopen(filename, "wb");
  if (!file) {
    clean();
    error("Failed to write to %s", filename);
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWriteFile);
  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, onProgress);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

  CURLcode response = curl_easy_perform(curl);

  bool success = true;

  if (response != CURLE_OK) {
    if (response != CURLE_ABORTED_BY_CALLBACK) {
      clean();
      error("Error downloading %s: %s", filename, curl_easy_strerror(response));
    }

    success = false;
  }

  curl_easy_cleanup(curl);

  fclose(file);

  return success;
}

std::string getMatchingVersion(std::string major) {
  auto data = fetch("http://registry.npmjs.org/electron");

  rapidjson::Document document;
  document.Parse(data.c_str());

  auto versionsObj = document["versions"].GetObject();

  std::vector<std::string> versions;

  for (rapidjson::Value::ConstMemberIterator itr = versionsObj.MemberBegin();
       itr != versionsObj.MemberEnd(); ++itr) {
    versions.push_back(std::string(itr->name.GetString()));
  }

  for (int i = versions.size() - 1; i >= 0; i--) {
    if (versions[i][0] == major[0]) {
      return versions[i];
    }
  }

  return "";
}

template <std::size_t N>
int execvp2(const char *file, const char *const (&argv)[N]) {
  assert((N > 0) && (argv[N - 1] == nullptr));

  return execvp(file, const_cast<char *const *>(argv));
}

#ifdef _WIN32
int execvp_win32(char *file, char argv[], int *launchError) {
  STARTUPINFO startupInfo;
  ZeroMemory(&startupInfo, sizeof(startupInfo));

  startupInfo.cb = sizeof(startupInfo);

  PROCESS_INFORMATION processInfo;

  if (!CreateProcess(file, argv, NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE,
                     NULL, NULL, &startupInfo, &processInfo)) {
    *launchError = GetLastError();
    return 1;
  }

  int result = WaitForSingleObject(processInfo.hProcess, INFINITE);

  CloseHandle(processInfo.hProcess);
  CloseHandle(processInfo.hThread);

  return result;
}
#endif

int launchElectron(std::string major) {
#if defined(WIN32) || defined(_WIN32)
  std::string arg = (dest / "electron.exe").string() + " " ASAR_PATH;
  char *argv = strdup(arg.c_str());

  int launchError;
  int result =
      execvp_win32(strdup((dest / "electron.exe").c_str()), argv, &launchError);

  if (launchError) {
    error("Error %i launching %s", launchError,
          strdup((dest / "electron.exe").c_str()));
    result = 1;
  }

  return result;
#elif defined(__APPLE__)
  const char *const argv[] = {(dest / "Electron.app").c_str(), ASAR_PATH,
                              nullptr};
  return execvp2((dest / "Electron.app").c_str(), argv);
#elif defined(__linux__)
  const char *const argv[] = {(dest / "electron").c_str(), ASAR_PATH, nullptr};
  return execvp2((dest / "electron").c_str(), argv);
#else
#error OS not detected
#endif
}

void downloadThread(void) {
  std::string url = "https://github.com/electron/electron/releases/download/v" +
                    electronVersion + "/electron-v" + electronVersion +
                    "-" OS "-" ARCH ".zip";
  zipPath = binPath / "electron.zip";

  fs::create_directory(dest);

  if (!download(url, zipPath.string().c_str())) {
    return;
  }

  setStatus("Extracting Electron...");

  std::cout << "Extracting..." << std::endl;

  if (!extract(zipPath, dest)) {
    clean();
    error(
        "An error occurred extracting the downloaded Electron "
        "archive");
    return;
  }

  fs::remove(zipPath);

  std::cout << "Done extracting" << std::endl;

  exit(0);
}

void initUI() {
  uiInitOptions options;
  memset(&options, 0, sizeof(uiInitOptions));

  if (uiInit(&options) != NULL) abort();

  window = uiNewWindow("Downloading Electron", 300, 50, false);
  uiWindowSetMargined(window, true);
  uiWindowOnClosing(window, onWindowClose, NULL);

  uiBox *verticalBox = uiNewVerticalBox();
  uiBoxSetPadded(verticalBox, true);
  uiWindowSetChild(window, uiControl(verticalBox));

  label = uiNewLabel("");
  uiBoxAppend(verticalBox, uiControl(label), false);

  progressBar = uiNewProgressBar();
  uiBoxAppend(verticalBox, uiControl(progressBar), false);

  uiBox *actions = uiNewHorizontalBox();
  uiBoxAppend(verticalBox, uiControl(actions), false);

  uiBox *spacer = uiNewHorizontalBox();
  uiBoxAppend(actions, uiControl(spacer), true);

  uiButton *cancelButton = uiNewButton("  Cancel  ");
  uiButtonOnClicked(cancelButton, onCancelClicked, NULL);
  uiBoxAppend(actions, uiControl(cancelButton), false);

  uiControlShow(uiControl(window));

  setStatus("Downloading Electron...");
}

int main() {
  std::ifstream ifstream(ELECTRON_VERSION_PATH);

  std::string major =
      std::string(1, std::string((std::istreambuf_iterator<char>(ifstream)),
                                 std::istreambuf_iterator<char>())[0]);

  electronVersion = getMatchingVersion(major);

  if (electronVersion == "") {
    error("Invalid Electron version");
    return 1;
  }

  binPath = getHomePath(BIN_DIR);

  fs::create_directory(binPath);

  dest = binPath / major;

  if (!fs::exists(dest)) {
    initUI();

    std::thread thread(downloadThread);

    uiMain();
  } else {
    std::cout << "Launching Electron..." << std::endl;
    int result = launchElectron(major);

    if (result) {
      std::cout << "Error launching Electron" << std::endl;
      return 1;
    }
  }

  return 0;
}
