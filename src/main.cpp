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
#ifndef _WIN32
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
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
#define PATH_SEPARATOR "\\"
#elif defined(__APPLE__)
#define OS "darwin"
#define PATH_SEPARATOR "/"
#elif defined(__linux__)
#define OS "linux"
#define PATH_SEPARATOR "/"
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

struct CurlBuffer {
    char *buffer;
    size_t length;
    size_t size;
};

struct UIUpdate {
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

unsigned long getTime() {
    struct timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec * 1000 + now.tv_usec / 1000.0;
}

void cancel() {
#ifdef _WIN32
    WaitForSingleObject(_ui_mutex, INFINITE);
    cancelled = true;
    ReleaseMutex(_ui_mutex);
#else
    pthread_mutex_lock(&_ui_mutex);
    _ui_cancelled = true;
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
#ifdef _WIN32
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
}

void error(const char *message, ...) {
    static char buffer[512];
    va_list args;
    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), message, args);
    va_end(args);

    if (!uiEnabled) return;

    uiQueueMain(showError, (void *)message);
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
        fprintf(stderr, "Failed to initialize libui\n");
        fprintf(stderr, "  %s\n", error);
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

    uiBox *spacer;

    uiBox *rows = uiNewVerticalBox();
    uiBoxSetPadded(rows, true);
    uiWindowSetChild(window, uiControl(rows));

    spacer = uiNewVerticalBox();
    uiBoxAppend(rows, uiControl(spacer), true);

    label = uiNewLabel("");
    uiBoxAppend(rows, uiControl(label), false);

    progressBar = uiNewProgressBar();
    uiBoxAppend(rows, uiControl(progressBar), false);

    uiBox *actions = uiNewHorizontalBox();
    uiBoxAppend(rows, uiControl(actions), false);

    spacer = uiNewHorizontalBox();
    uiBoxAppend(actions, uiControl(spacer), true);

    uiButton *cancelButton = uiNewButton("Cancel");
    uiButtonOnClicked(cancelButton, onCancelClicked, NULL);
    uiBoxAppend(actions, uiControl(cancelButton), false);

    spacer = uiNewVerticalBox();
    uiBoxAppend(rows, uiControl(spacer), true);

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

static void updateUI(void *arg) {
    UIUpdate *update = (UIUpdate *)arg;

    if (update->status != NULL) {
        uiLabelSetText(label, update->status);
    }

    if (update->progress >= 0) {
        uiProgressBarSetValue(progressBar, update->progress);
    }

    // show the window the first time it is given information
    if (!windowVisible) {
        windowVisible = true;
        uiControlShow(uiControl(window));
    }
}

void initUI() {
    if (uiEnabled) return;

    windowVisible = false;  // reset this before it belongs to the thread

#ifdef _WIN32
    _ui_mutex = CreateMutex(NULL, FALSE, NULL);
    uiLoaded = CreateSemaphore(NULL, 0, 1, NULL);

    ui_thread = CreateThread(NULL, 0, ui_main_win32, NULL, 0, NULL);
    if (!ui_thread) {
        fprintf(stderr, "Error creating ui thread\n");
        return;
    }

    WaitForSingleObject(uiLoaded, INFINITE);

#else
    pthread_mutex_init(&_ui_mutex, NULL);
    sem_init(&uiLoaded, 0, 0);

    if (pthread_create(&ui_thread, NULL, ui_main, NULL)) {
        fprintf(stderr, "Error creating ui thread\n");
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

void setStatus(const char *status) {
    if (!uiEnabled) return;
    UIUpdate update{status, -1};
    uiQueueMain(updateUI, &update);
}

void setProgress(int progress) {
    if (!uiEnabled) return;
    UIUpdate update{NULL, progress};
    uiQueueMain(updateUI, &update);
}

bool extract(const char *archive, const char *path) {
    bool success = zip_extract(archive, path, NULL, NULL) == 0;

    return success;
}

static size_t _on_curl_write_memory(const char *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
    CurlBuffer *buffer = (CurlBuffer *)userdata;

    size_t chunkSize = size * nmemb;

    if (buffer->length + chunkSize + 1 >= buffer->size) {
        buffer->size = buffer->size + chunkSize + 1 + (64 * 1024);

        char *newBuffer = (char *)realloc(buffer->buffer, buffer->size);
        if (!newBuffer) {
            fprintf(stderr, "Out of memory making web request\n");
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

    if (now - lastUiTime > 1000 / 30) {
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

bool download(const char *url, const char *filename) {
    setStatus("Downloading...");

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        error("Error initializing libcurl");
        return false;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        error("Unable to write to \"%s\"", filename);
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
            error("Error downloading \"%s\"\n  %s", url,
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
    char *bestVersionString = NULL;

    /*if (!bestVersionString) {
        initUI();

        const char *apiUrl =
            "https://api.github.com/repos/electron/electron/releases";
        char *api;
        fetch(&api, apiUrl);

        if (isCancelled()) return 0;

        if (!bestVersionUrl) {
            error(
                "Unable to find a compatible version of Electron for download");
            return 1;
        }

        if (!download(bestVersionUrl, downloadDestination)) {
            if (isCancelled()) return 0;

            return 1;
        }

        printf("Extracting...\n");

#ifdef _WIN32
        if (mkdir(extractDestination) < 0) {
            error("Unable to create path for writing: %s", extractDestination);
        }
#else
        if (mkdir(extractDestination, 0700) < 0) {
            error("Unable to create path for writing: %s", extractDestination);
        }
#endif

        if (!extract_files(downloadDestination, extractDestination)) {
            rmdir(extractDestination);

            if (isCancelled()) return 0;

            error(
                "An error occurred extracting the downloaded Electron "
                "archive");
            return 1;
        }

        remove(downloadDestination);
    }*/

    initUI();

    if (!download("https://github.com/electron/electron/releases/download/"
                  "v6.0.2/electron-v6.0.2-win32-x64.zip",
                  "electron.zip")) {
        if (isCancelled()) return 0;

        return 1;
    }

    printf("Extracting...\n");

#ifdef _WIN32
    if (mkdir("electron") < 0) {
        error("Unable to create path for writing: %s", "electron");
    }
#else
    if (mkdir(extractDestination, 0700) < 0) {
        error("Unable to create path for writing: %s", extractDestination);
    }
#endif

    if (!extract("electron.zip", "electron")) {
        rmdir("electron");

        if (isCancelled()) return 0;

        error(
            "An error occurred extracting the downloaded Electron "
            "archive");
        return 1;
    }

#ifdef _WIN32
    hide();
#else

#endif

    return 0;
}