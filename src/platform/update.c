/*
 * Safe automatic updates, see update.h and docs/UPDATES.md.
 *
 * The release list comes from the GitHub API; each release carries
 * update.txt with the size and SHA-256 of every update file. The new file is
 * downloaded next to the program (same file system), verified and then
 * renamed over it, so an interrupted or corrupt download never replaces a
 * working program. Downloads use the system's curl in a child process (on
 * Windows the curl.exe that ships with Windows 10 and later).
 */
#include "update.h"
#include "update_core.h"
#include "../util/sha256.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef SML_VERSION
#define SML_VERSION "M0-dev"
#endif
#define REPO "jarvis-jhk/super-mario-land-port"
#define RELEASES_API "https://api.github.com/repos/" REPO "/releases?per_page=30"
#define DOWNLOADS "https://github.com/" REPO "/releases/download"
#define RELEASE_PAGE "github.com/" REPO "/releases"

#if defined(_WIN32)
#define UPDATE_ASSET "sml-windows-x86_64.exe"
#define ARCH "x86_64"
#elif defined(__aarch64__)
#define ARCH "aarch64"
#else
#define ARCH "x86_64"
#endif
#if defined(SML_PORTMASTER)
#define UPDATE_ASSET "sml-portmaster-aarch64"
#elif !defined(UPDATE_ASSET)
#define UPDATE_ASSET "sml-linux-" ARCH
#endif

static char dir[1024];
static char message[300];
static SDL_atomic_t ready;

/* Runs curl to fetch url into path; 0 on success. */
static int fetch(const char *url, const char *path)
{
#ifdef _WIN32
    char cmd[3000];
    snprintf(cmd, sizeof cmd, "curl.exe -fsSL --connect-timeout 5 --max-time 600 -o \"%s\" \"%s\"", path, url);
    STARTUPINFOA start = {0};
    PROCESS_INFORMATION proc = {0};
    start.cb = sizeof start;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &proc))
        return -1;
    WaitForSingleObject(proc.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(proc.hProcess, &code);
    CloseHandle(proc.hThread);
    CloseHandle(proc.hProcess);
    return code == 0 ? 0 : -1;
#else
    pid_t child = fork();
    if (child < 0)
        return -1;
    if (child == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0); dup2(null, 1); dup2(null, 2);
        }
        execlp("curl", "curl", "-fsSL", "--connect-timeout", "5", "--max-time", "600",
               "-o", path, url, (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0)
        ;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
#endif
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = malloc(1 << 20);
    size_t n = buf ? fread(buf, 1, (1 << 20) - 1, f) : 0;
    fclose(f);
    if (buf)
        buf[n] = 0;
    return buf;
}

/* The file this program is started from, which the update replaces. */
static int target_path(char *out, size_t len)
{
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)len);
    return n > 0 && n < len ? 0 : -1;
#else
    const char *appimage = getenv("APPIMAGE");
    if (appimage && appimage[0] == '/') {
        snprintf(out, len, "%s", appimage);
        return 0;
    }
    ssize_t n = readlink("/proc/self/exe", out, len - 1);
    if (n <= 0)
        return -1;
    out[n] = 0;
    return 0;
#endif
}

static const char *asset_name(void)
{
#ifndef _WIN32
    if (getenv("APPIMAGE"))
        return "Super_Mario_Land_Port-" ARCH ".AppImage";
#endif
    return UPDATE_ASSET;
}

static int replace_file(const char *fresh, const char *target)
{
#ifdef _WIN32
    /* A running .exe can be renamed but not overwritten. */
    char old[1100];
    snprintf(old, sizeof old, "%s.old", target);
    if (!MoveFileExA(target, old, MOVEFILE_REPLACE_EXISTING))
        return -1;
    if (!MoveFileExA(fresh, target, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExA(old, target, MOVEFILE_REPLACE_EXISTING);
        return -1;
    }
    return 0;
#else
    struct stat st;
    chmod(fresh, stat(target, &st) == 0 ? (st.st_mode & 0777) | 0100 : 0755);
    return rename(fresh, target);
#endif
}

static void say(const char *fmt, const char *version)
{
    snprintf(message, sizeof message, fmt, version);
    SDL_AtomicSet(&ready, 1);
}

static int SDLCALL check(void *unused)
{
    (void)unused;
    const char *test = getenv("SML_UPDATE_URL");   /* local test server */
    char url[1200], list[1100], manifest_path[1100], target[1000], fresh[1100];
    snprintf(list, sizeof list, "%supdate-releases.json", dir);
    snprintf(manifest_path, sizeof manifest_path, "%supdate.txt", dir);
    if (target_path(target, sizeof target))
        return 0;
    snprintf(fresh, sizeof fresh, "%s.new", target);
    remove(fresh);
#ifdef _WIN32
    char old[1100];
    snprintf(old, sizeof old, "%s.old", target);
    remove(old);
#endif

    char base[600];
    snprintf(base, sizeof base, "%s%s", test ? test : DOWNLOADS, test ? "/download" : "");
    if (test)
        snprintf(url, sizeof url, "%s/releases.json", test);
    else
        snprintf(url, sizeof url, "%s", RELEASES_API);
    char latest[32];
    char *json = fetch(url, list) == 0 ? read_text(list) : NULL;
    remove(list);
    int found = json && update_latest_tag(json, latest, sizeof latest) == 0;
    free(json);
    if (!found || version_compare(latest, SML_VERSION) <= 0)
        return 0;

    snprintf(url, sizeof url, "%s/v%s/update.txt", base, latest);
    char *manifest = fetch(url, manifest_path) == 0 ? read_text(manifest_path) : NULL;
    remove(manifest_path);
    uint64_t size = 0;
    char sha[65];
    int listed = manifest && update_manifest_find(manifest, latest, asset_name(), &size, sha) == 0;
    free(manifest);
    if (!listed)
        return 0;

    /* Read-only installs (system packages, a mounted card) only learn about it. */
    FILE *probe = fopen(fresh, "wb");
    if (!probe) {
        say("Version %s is available on " RELEASE_PAGE, latest);
        return 0;
    }
    fclose(probe);
    snprintf(url, sizeof url, "%s/v%s/%s", base, latest, asset_name());
    char got[65];
    uint64_t got_size = 0;
    if (fetch(url, fresh) || sha256_file(fresh, got, &got_size) || got_size != size || strcmp(got, sha)) {
        remove(fresh);
        return 0;
    }
    if (replace_file(fresh, target)) {
        remove(fresh);
        say("Version %s is available on " RELEASE_PAGE, latest);
        return 0;
    }
    say("Version %s installed - restart to play it", latest);
    return 0;
}

void update_start(const char *data_dir)
{
    if (version_compare(SML_VERSION, SML_VERSION) == -2 || !data_dir[0])
        return;
    snprintf(dir, sizeof dir, "%s", data_dir);
    SDL_Thread *t = SDL_CreateThread(check, "update", NULL);
    if (t)
        SDL_DetachThread(t);
}

const char *update_message(void)
{
    if (SDL_AtomicGet(&ready) != 1)
        return NULL;
    SDL_AtomicSet(&ready, 2);
    return message;
}
