#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <experimental/filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <curl/curl.h>
#include "lib/libui/ui.h"
#include "lib/rapidjson/include/rapidjson/document.h"
#include "lib/zip/src/zip.h"

#define PROGRAM_NAME "electron-launcher"
#define PROGRAM_VERSION "0.1"

#define BIN_DIR ".electron-global"

#if defined(WIN32) || defined(_WIN32)
#define OS "win32"
#define HOME_ENV "HOMEPATH"
#elif defined(__APPLE__)
#define OS "darwin"
#define HOME_ENV "HOME"
#elif defined(__linux__)
#define OS "linux"
#define HOME_ENV "HOME"
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

namespace fs = std::experimental::filesystem;

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

std::string electronMajor;

bool done = false;

unsigned long getTime() {
  struct timeval now;
  gettimeofday(&now, 0);
  return now.tv_sec * 1000 + now.tv_usec / 1000.0;
}

fs::path getHomePath(std::string subpath) {
  return fs::path(getenv(HOME_ENV)) / subpath;
}

void cancel() {
  fs::remove_all(dest);
  fs::remove(zipPath);

  exit(0);
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

  fs::remove_all(dest);
  fs::remove(zipPath);

  uiQueueMain(showError, (void *)buffer);
}

void onCancelClicked(uiButton *b, void *data) { cancel(); }

int onWindowClose(uiWindow *w, void *data) {
  cancel();
  return 1;
}

void setStatus(const char *status) { uiLabelSetText(label, status); }

void setProgress(int progress) { uiProgressBarSetValue(progressBar, progress); }

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

  unsigned long now = getTime();

  if (now - lastUiTime > 1000 / 60) {
    lastUiTime = now;

    if (dltotal > 0) {
      setProgress((int)((dlnow * 100) / dltotal));
    } else {
      setProgress(0);
    }
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
    error("Error initializing curl");
    return false;
  }

  FILE *file = fopen(filename, "wb");
  if (!file) {
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
    if (versions[i].rfind(major, 0) == 0) {
      return versions[i];
    }
  }

  return NULL;
}

void threadproc(void) {
  auto version = getMatchingVersion(electronMajor);

  fs::path binPath = getHomePath(BIN_DIR);

  std::string url = "https://github.com/electron/electron/releases/download/v" +
                    version + "/electron-v" + version + "-" OS "-" ARCH ".zip";
  zipPath = binPath / "electron.zip";

  fs::create_directory(dest);

  if (!download(url, zipPath.string().c_str())) {
    return;
  }

  setStatus("Extracting Electron...");

  std::cout << "Extracting..." << std::endl;

  if (!extract(zipPath, dest)) {
    error(
        "An error occurred extracting the downloaded Electron "
        "archive");
    return;
  }

  fs::remove(zipPath);

  std::cout << "Done extracting" << std::endl;

  uiQuit();
}

int main() {
  electronMajor = "6";

  fs::path binPath = getHomePath(BIN_DIR);

  fs::create_directory(binPath);

  dest = binPath / electronMajor;

  if (!fs::exists(dest)) {
    uiInitOptions options;
    memset(&options, 0, sizeof(uiInitOptions));

    if (uiInit(&options) != NULL) abort();

    window = uiNewWindow("Downloading Electron", 350, 50, false);
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

    uiButton *cancelButton = uiNewButton("Cancel");
    uiButtonOnClicked(cancelButton, onCancelClicked, NULL);
    uiBoxAppend(actions, uiControl(cancelButton), false);

    uiControlShow(uiControl(window));

    setStatus("Downloading Electron...");

    std::thread thread(threadproc);

    uiMain();
  } else {
    std::cout << "Launching Electron..." << std::endl;
  }

  return 0;
}
