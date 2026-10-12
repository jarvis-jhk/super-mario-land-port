/* Public fault relay. Network work happens in a separate process, never in
   the game or its signal handler. No messages, paths or memory are uploaded. */
#include "fault.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifndef SML_VERSION
#define SML_VERSION "M0-dev"
#endif
#define ENDPOINT "https://ntfy.sh/sml-faults-2356a04c1b4f5a9a12759c1a8626411d"
#if defined(_WIN32)
#include <windows.h>
#define PLATFORM "Windows x86_64"
static LONG WINAPI on_exception(EXCEPTION_POINTERS *e)
{
    (void)e;
    fault_report();
    return EXCEPTION_EXECUTE_HANDLER;
}
void fault_install(void)
{
    SetUnhandledExceptionFilter(on_exception);
}
void fault_report(void)
{
    static LONG sent;
    if (getenv("SML_NO_REPORT") && !strcmp(getenv("SML_NO_REPORT"), "1")) return;
    if (InterlockedExchange(&sent, 1)) return;
    char command[] = "curl.exe -fsS --connect-timeout 3 --max-time 8 -o NUL -d \""
        "super-mario-land-port " SML_VERSION " " PLATFORM " application fault\" " ENDPOINT;
    STARTUPINFOA start = {0};
    PROCESS_INFORMATION process = {0};
    start.cb = sizeof start;
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#if defined(__aarch64__)
#define PLATFORM "Linux aarch64"
#else
#define PLATFORM "Linux x86_64"
#endif
static int report_fd = -1;
static volatile sig_atomic_t sent;
void fault_report(void)
{
    if (report_fd < 0 || sent) return;
    sent = 1;
    char marker = 'F';
    /* One byte, once, to an empty pipe: nonblocking and signal-safe. */
    if (write(report_fd, &marker, 1) < 0) {}
}
static void on_crash(int sig)
{
    fault_report();
    _exit(128 + sig);
}
void fault_install(void)
{
    if (getenv("SML_NO_REPORT") && !strcmp(getenv("SML_NO_REPORT"), "1")) return;
    int fds[2];
    if (pipe(fds) != 0) return;
    pid_t child = fork();
    if (child < 0) { close(fds[0]); close(fds[1]); return; }
    if (child == 0) {
        close(fds[1]);
        char marker;
        ssize_t n;
        do { n = read(fds[0], &marker, 1); } while (n < 0 && errno == EINTR);
        close(fds[0]);
        if (n == 1 && marker == 'F') {
            int nullfd = open("/dev/null", O_RDWR);
            if (nullfd >= 0) {
                dup2(nullfd, 0); dup2(nullfd, 1); dup2(nullfd, 2);
                if (nullfd > 2) close(nullfd);
            }
            execlp("curl", "curl", "-fsS", "--connect-timeout", "3", "--max-time", "8",
                   "-o", "/dev/null", "-d", "super-mario-land-port " SML_VERSION " " PLATFORM
                   " application fault", ENDPOINT, (char *)NULL);
        }
        _exit(0);
    }
    close(fds[0]);
    report_fd = fds[1];
    fcntl(report_fd, F_SETFD, FD_CLOEXEC);
    fcntl(report_fd, F_SETFL, O_NONBLOCK);
    /* A failed helper must never terminate the game on a pipe write. */
    signal(SIGPIPE, SIG_IGN);
#ifndef __SANITIZE_ADDRESS__
    signal(SIGSEGV, on_crash);
    signal(SIGBUS, on_crash);
    signal(SIGFPE, on_crash);
    signal(SIGILL, on_crash);
    signal(SIGABRT, on_crash);
#endif
}
#endif
