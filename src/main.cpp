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
#include <string>
#include <algorithm>
#include <iostream>
#ifndef _WIN32
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <experimental/filesystem>
#endif

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#define strdup _strdup
#endif

#include "lib/libui/ui.h"
#include "lib/semver.c/semver.h"
#include "lib/zip/src/zip.h"

#define PROGRAM_NAME "electron-runtime-launcher"
#define PROGRAM_VERSION "0.1"

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

struct Info {
    const char *status;
    int progress;
};

#ifdef _WIN32
HANDLE ui_thread;
HANDLE _ui_mutex;
HANDLE uiLoaded;
#else
pthread_t ui_thread;
pthread_mutex_t _ui_mutex;
sem_t uiLoaded;
#endif

bool uiEnabled = false;

bool windowVisible = false;
uiWindow *window = NULL;
uiLabel *label = NULL;
uiProgressBar *progressBar = NULL;

bool cancelled = false;

fs::path dest;
fs::path zipPath;

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

#ifdef _WIN32
    WaitForSingleObject(_ui_mutex, INFINITE);
    cancelled = true;
    ReleaseMutex(_ui_mutex);
#else
    pthread_mutex_lock(&_ui_mutex);
    cancelled = true;
    pthread_mutex_unlock(&_ui_mutex);
#endif
}

bool isCancelled() {
    bool result;
#ifdef _WIN32
    WaitForSingleObject(_ui_mutex, INFINITE);
    result = cancelled;
    ReleaseMutex(_ui_mutex);
#else
    pthread_mutex_lock(&_ui_mutex);
    result = cancelled;
    pthread_mutex_unlock(&_ui_mutex);
#endif
    return result;
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

    if (!uiEnabled) return;

    uiQueueMain(showError, (void *)buffer);
    
#ifdef _WIN32
    WaitForSingleObject(ui_thread, INFINITE);
#else
    pthread_join(ui_thread, NULL);
#endif

    uiEnabled = false;
}

void onCancelClicked(uiButton *b, void *data) { cancel(); }

int onWindowClose(uiWindow *w, void *data) {
    cancel();
    return 1;
}

static void *ui_main(void *arg) {
    uiInitOptions options;
    memset(&options, 0, sizeof(uiInitOptions));

    const char *error = uiInit(&options);
    if (error) {
        std::cout << "Failed to initialize window." << std::endl;
        std::cout << error << std::endl;

        uiFreeInitError(error);
#ifdef _WIN32
        ReleaseSemaphore(uiLoaded, 1, NULL);
#else
        sem_post(&uiLoaded);
#endif
        return NULL;
    }

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

#ifdef _WIN32
    ReleaseSemaphore(uiLoaded, 1, NULL);
#else
    sem_post(&uiLoaded);
#endif

    uiMain();

    return NULL;
}

#ifdef _WIN32
DWORD WINAPI ui_main_win32(LPVOID lpParam) {
    ui_main(NULL);
    return 0;
}
#endif

static void updateInfo(void *arg) {
    Info *info = (Info *)arg;
    
    if (info->status != NULL) {
        uiLabelSetText(label, info->status);
    }

    if (info->progress >= 0) {
        uiProgressBarSetValue(progressBar, info->progress);
    }

    if (!windowVisible) {
        windowVisible = true;
        uiControlShow(uiControl(window));
    }
}

void initUI() {
    if (uiEnabled) return;

    windowVisible = false;

#ifdef _WIN32
    _ui_mutex = CreateMutex(NULL, FALSE, NULL);
    uiLoaded = CreateSemaphore(NULL, 0, 1, NULL);

    ui_thread = CreateThread(NULL, 0, ui_main_win32, NULL, 0, NULL);
    if (!ui_thread) {
        std::cout << "Error creating UI thread" << std::endl;
        return;
    }

    WaitForSingleObject(uiLoaded, INFINITE);

#else
    pthread_mutex_init(&_ui_mutex, NULL);
    sem_init(&uiLoaded, 0, 0);

    if (pthread_create(&ui_thread, NULL, ui_main, NULL)) {
        std::cout << "Error creating UI thread" << std::endl;
        return;
    }

    sem_wait(&uiLoaded);
#endif

    uiEnabled = true;
}

static void _onHide(void *arg) {
    if (windowVisible) {
        windowVisible = false;
        uiControlHide(uiControl(window));
    }
}

void hide() {
    if (!uiEnabled) return;
    uiQueueMain(_onHide, NULL);
}

void queueInfoUpdate(const char* status, int progress) {
    if (!uiEnabled) return;

    Info* info = new Info{status, progress};
    uiQueueMain(updateInfo, info);
}

void setStatus(const char *status) {
    queueInfoUpdate(status, -1);
}

void setProgress(int progress) {
    queueInfoUpdate(NULL, progress);
}

bool extract(const char *archive, const char *path) {
    return zip_extract(archive, path, NULL, NULL) == 0;
}

static size_t _on_curl_write_memory(const char *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
    CurlBuffer *buffer = (CurlBuffer *)userdata;

    size_t chunkSize = size * nmemb;

    if (buffer->length + chunkSize + 1 >= buffer->size) {
        buffer->size = buffer->size + chunkSize + 1 + (64 * 1024);

        char *newBuffer = (char *)realloc(buffer->buffer, buffer->size);
        if (!newBuffer) {
            std::cout << "Out of memory" << std::endl;
            return 0;
        }

        buffer->buffer = newBuffer;
    }

    memcpy(&buffer->buffer[buffer->length], ptr, chunkSize);

    buffer->length += chunkSize;
    buffer->buffer[buffer->length] = '\0';

    return chunkSize;
}

static size_t _on_curl_write_file(const char *ptr, size_t size, size_t nmemb,
                                  void *userdata) {
    FILE *file = (FILE *)userdata;

    return fwrite(ptr, size, nmemb, file);
}

static int _on_curl_progress(void *clientp, double dltotal, double dlnow,
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

    if (isCancelled()) return 1;

    return 0;
}

void fetch(char **buffer, const char *url) {
    *buffer = 0;

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        error("Error initializing libcurl");
        return;
    }

    CurlBuffer curlBuffer = {
        .buffer = (char *)malloc(4096), .length = 0, .size = 4096};

    curlBuffer.buffer[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _on_curl_write_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlBuffer);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, _on_curl_progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);

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
                    error("Error retrieving %s\n%s", url,
                          curl_easy_strerror(response));
            }
        }

        free(curlBuffer.buffer);
        curlBuffer.buffer = NULL;
    }

    *buffer = curlBuffer.buffer;

    curl_easy_cleanup(curl);
}

bool download(const char* url, const char* filename) {
    setStatus("Downloading Electron...");

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        error("Error initializing curl");
        return false;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        error("Failed to write to %s", filename);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, PROGRAM_NAME "/" PROGRAM_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _on_curl_write_file);

    if (uiEnabled) {
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, _on_curl_progress);
    }

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

    CURLcode response = curl_easy_perform(curl);

    bool success = true;

    if (response != CURLE_OK) {
        if (response != CURLE_ABORTED_BY_CALLBACK) {
            error("Error downloading %s: %s", filename,
                  curl_easy_strerror(response));
        }

        success = false;
    }

    curl_easy_cleanup(curl);

    fclose(file);

    return success;
}

#ifdef _WIN32

#endif

int main(int argc, const char *argv[]) {
    dest = getHomePath("electron-runtime/6");

    if (!fs::exists(dest)) {
        initUI();

        std::string url = "https://github.com/electron/electron/releases/download/v6.0.2/electron-v6.0.2-win32-x64.zip";
        zipPath = getHomePath("electron-runtime/electron.zip");
    
        fs::create_directory(dest);

        if (!download(url.c_str(), zipPath.c_str())) {
            if (isCancelled()) return 0;

            return 1;
        }

        setStatus("Extracting Electron...");

        std::cout << "Extracting..." << std::endl;

        if (!extract(zipPath.c_str(), dest.c_str())) {
            if (isCancelled()) return 0;

            error(
                "An error occurred extracting the downloaded Electron "
                "archive");
            return 1;
        }

        fs::remove(zipPath);
    } else {
        std::cout << "Launching Electron..." << std::endl;
    }


#ifdef _WIN32
    hide();
#else

#endif

    return 0;
}
