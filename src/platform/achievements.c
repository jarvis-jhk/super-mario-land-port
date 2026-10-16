/* RetroAchievements for the SDL frontend, see achievements.h. */
#include "achievements.h"

#include <SDL.h>
#include <stdarg.h>
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

#include "rc_client.h"
#include "rc_consoles.h"
#include "../third_party/rcheevos/src/rhash/md5.h"

#if defined(_WIN32)
#define OS_NAME "Windows"
#elif defined(SML_PORTMASTER)
#define OS_NAME "PortMaster"
#else
#define OS_NAME "Linux"
#endif

static rc_client_t *client;
static const Machine *machine;
static char dir[1024], version[32], hash[33];
static char fake_dir[1024];
static int report;
static long frames;
static int generation;          /* answers for an older client are dropped */
static char state_text[160];    /* last login/load outcome */

/* --- messages for the player ------------------------------------------ */

#define MSG_MAX 8
static char messages[MSG_MAX][120];
static int msg_head, msg_count;

static void message(const char *fmt, ...)
{
    va_list ap;
    if (msg_count == MSG_MAX) {
        msg_head = (msg_head + 1) % MSG_MAX;
        msg_count--;
    }
    char *m = messages[(msg_head + msg_count) % MSG_MAX];
    va_start(ap, fmt);
    vsnprintf(m, sizeof messages[0], fmt, ap);
    va_end(ap);
    msg_count++;
}

int ach_take_message(char *out, size_t len)
{
    int n = msg_count;
    if (!n)
        return 0;
    snprintf(out, len, "%s", messages[msg_head]);
    msg_head = (msg_head + 1) % MSG_MAX;
    msg_count--;
    return n;
}

int ach_messages_waiting(void) { return msg_count; }

/* The progress indicator is one line that is replaced, not queued. */
static char progress[80];
static int progress_new;

int ach_take_progress(char *out, size_t len)
{
    if (!progress_new)
        return 0;
    snprintf(out, len, "%s", progress);
    progress_new = 0;
    return 1;
}

/* --- server calls --------------------------------------------------------- */

typedef struct Call {
    struct Call *next;
    int generation;
    char *url, *post, *type, *agent;
    rc_client_server_callback_t callback;
    void *callback_data;
    char *body;
    size_t body_len;
    int status;
} Call;

static SDL_mutex *done_lock;
static Call *done_first, *done_last;
static SDL_atomic_t call_counter;

static char *dup_str(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    *len = 0;
    if (!f)
        return NULL;
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap + 1);
    while (buf) {
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (n < cap)
            break;
        char *bigger = cap < (64u << 20) ? realloc(buf, cap * 2 + 1) : NULL;
        if (!bigger) {
            free(buf);
            buf = NULL;
            break;
        }
        buf = bigger;
        cap *= 2;
    }
    fclose(f);
    if (buf) {
        buf[n] = 0;
        *len = n;
    }
    return buf;
}

/* Last "HTTP/x nnn" status line in a curl header dump. */
static int http_status(const char *path)
{
    size_t n;
    char *h = read_all(path, &n);
    int status = 0;
    for (char *p = h; p && (p = strstr(p, "HTTP/")); p++) {
        const char *sp = strchr(p, ' ');
        if (sp && (p == h || p[-1] == '\n'))
            status = atoi(sp + 1);
    }
    free(h);
    return status;
}

/* Runs curl for one call; fills body and status. */
static void http_call(Call *c)
{
    int id = SDL_AtomicAdd(&call_counter, 1);
    char post[1100], out[1100], head[1100];
    const char *agent = c->agent ? c->agent : "SuperMarioLandPort";
    snprintf(post, sizeof post, "%sra-%d-req.tmp", dir, id);
    snprintf(out, sizeof out, "%sra-%d-body.tmp", dir, id);
    snprintf(head, sizeof head, "%sra-%d-head.tmp", dir, id);
    c->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    int have_post = 0;
    if (c->post) {
        /* The request (it holds the token or password) goes through a
           private file, not the command line. */
#ifdef _WIN32
        FILE *f = fopen(post, "wb");
#else
        int fd = open(post, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
#endif
        if (!f)
            return;
        fputs(c->post, f);
        fclose(f);
        have_post = 1;
    }
    char data_arg[1200], type_arg[200];
    snprintf(data_arg, sizeof data_arg, "@%s", post);
    snprintf(type_arg, sizeof type_arg, "Content-Type: %s",
             c->type ? c->type : "application/x-www-form-urlencoded");
    int ok;
#ifdef _WIN32
    char cmd[4000];
    snprintf(cmd, sizeof cmd, "curl.exe -sS --connect-timeout 5 --max-time 30 -A \"%s\" %s%s%s%s%s "
             "-o \"%s\" -D \"%s\" \"%s\"", agent, have_post ? "-H \"" : "", have_post ? type_arg : "",
             have_post ? "\" --data-binary \"" : "", have_post ? data_arg : "", have_post ? "\"" : "",
             out, head, c->url);
    STARTUPINFOA start = {0};
    PROCESS_INFORMATION proc = {0};
    start.cb = sizeof start;
    ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &proc);
    DWORD code = 1;
    if (ok) {
        WaitForSingleObject(proc.hProcess, INFINITE);
        GetExitCodeProcess(proc.hProcess, &code);
        CloseHandle(proc.hThread);
        CloseHandle(proc.hProcess);
    }
    ok = ok && code == 0;
#else
    pid_t child = fork();
    if (child == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0); dup2(null, 1); dup2(null, 2);
        }
        if (have_post)
            execlp("curl", "curl", "-sS", "--connect-timeout", "5", "--max-time", "30", "-A", agent,
                   "-H", type_arg, "--data-binary", data_arg, "-o", out, "-D", head, c->url, (char *)NULL);
        else
            execlp("curl", "curl", "-sS", "--connect-timeout", "5", "--max-time", "30", "-A", agent,
                   "-o", out, "-D", head, c->url, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (child > 0)
        while (waitpid(child, &status, 0) < 0)
            ;
    ok = child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    if (ok) {
        c->body = read_all(out, &c->body_len);
        c->status = http_status(head);
        if (!c->body)
            c->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    }
    if (have_post)
        remove(post);
    remove(out);
    remove(head);
}

/* SML_RA_FAKE: the answer to "r=NAME" is <fake_dir>/NAME.json. */
static void fake_call(Call *c)
{
    const char *q = c->post ? c->post : strchr(c->url, '?');
    char name[64] = "", path[1200];
    const char *r = q ? strstr(q, "r=") : NULL;
    if (r && (r == q || r[-1] == '&' || r[-1] == '?')) {
        size_t n = strcspn(r + 2, "&");
        snprintf(name, sizeof name, "%.*s", (int)(n < sizeof name ? n : sizeof name - 1), r + 2);
    }
    snprintf(path, sizeof path, "%s/%s.json", fake_dir, name);
    c->body = read_all(path, &c->body_len);
    const char *id = q ? strstr(q, "&a=") : NULL;
    if (!strcmp(name, "awardachievement") && id) {
        char award[200];
        snprintf(award, sizeof award, "{\"Success\":true,\"Score\":0,\"SoftcoreScore\":5,"
                 "\"AchievementID\":%ld,\"AchievementsRemaining\":0}", atol(id + 3));
        free(c->body);
        c->body = dup_str(award);
    }
    c->status = c->body ? 200 : 404;
    if (!c->body)
        c->body = dup_str("{\"Success\":false,\"Error\":\"not in the test server\"}");
    if (c->body)
        c->body_len = strlen(c->body);
}

static void finish(Call *c)
{
    SDL_LockMutex(done_lock);
    c->next = NULL;
    if (done_last)
        done_last->next = c;
    else
        done_first = c;
    done_last = c;
    SDL_UnlockMutex(done_lock);
}

static int call_thread(void *p)
{
    http_call(p);
    finish(p);
    return 0;
}

static void RC_CCONV server_call(const rc_api_request_t *request, rc_client_server_callback_t callback,
                                 void *callback_data, rc_client_t *cl)
{
    (void)cl;
    Call *c = calloc(1, sizeof *c);
    if (!c)
        return;
    c->generation = generation;
    c->url = dup_str(request->url);
    c->post = dup_str(request->post_data);
    c->type = dup_str(request->content_type);
    char agent[200], clause[120] = "";
    rc_client_get_user_agent_clause(cl, clause, sizeof clause);
    snprintf(agent, sizeof agent, "SuperMarioLandPort/%s (%s) %s", version, OS_NAME, clause);
    c->agent = dup_str(agent);
    c->callback = callback;
    c->callback_data = callback_data;
    if (fake_dir[0]) {
        fake_call(c);
        finish(c);
        return;
    }
    SDL_Thread *t = SDL_CreateThread(call_thread, "achievements", c);
    if (t)
        SDL_DetachThread(t);
    else {
        c->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        finish(c);
    }
}

/* Hands finished answers to rc_client (on the main thread). */
static void deliver(void)
{
    if (!done_lock)
        return;
    for (;;) {
        SDL_LockMutex(done_lock);
        Call *c = done_first;
        if (c) {
            done_first = c->next;
            if (!done_first)
                done_last = NULL;
        }
        SDL_UnlockMutex(done_lock);
        if (!c)
            return;
        if (client && c->generation == generation) {
            rc_api_server_response_t r;
            memset(&r, 0, sizeof r);
            r.body = c->body ? c->body : "";
            r.body_length = c->body ? c->body_len : 0;
            r.http_status_code = c->status;
            c->callback(&r, c->callback_data);
        }
        free(c->url);
        free(c->post);
        free(c->type);
        free(c->agent);
        free(c->body);
        free(c);
    }
}

/* --- rc_client ------------------------------------------------------------ */

static uint32_t RC_CCONV read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *cl)
{
    (void)cl;
    uint32_t i;
    for (i = 0; i < num_bytes && address + i <= 0xFFFF; i++)
        buffer[i] = machine_peek(machine, (uint16_t)(address + i));
    return i;
}

static void RC_CCONV log_message(const char *text, const rc_client_t *cl)
{
    (void)cl;
    fprintf(stderr, "achievements: %s\n", text);
}

static void RC_CCONV on_event(const rc_client_event_t *e, rc_client_t *cl)
{
    (void)cl;
    switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        progress[0] = 0;
        progress_new = 1;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        message("Unlocked: %s (%u points)", e->achievement->title, e->achievement->points);
        if (report)
            printf("achievement %u frame %ld %s\n", e->achievement->id, frames, e->achievement->title);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        snprintf(progress, sizeof progress, "%s %s", e->achievement->title, e->achievement->measured_progress);
        progress_new = 1;
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        message("Leaderboard attempt: %s", e->leaderboard->title);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        message("Leaderboard attempt failed: %s", e->leaderboard->title);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        message("Submitted %s: %s", e->leaderboard->tracker_value, e->leaderboard->title);
        if (report)
            printf("leaderboard %u frame %ld %s\n", e->leaderboard->id, frames, e->leaderboard->tracker_value);
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        message("Set mastered: all achievements unlocked");
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        message("RetroAchievements: %s", e->server_error->error_message);
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        message("RetroAchievements offline; unlocks are sent later");
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        message("RetroAchievements online again");
        break;
    default:
        break;
    }
}

static void login_path(char *out, size_t len) { snprintf(out, len, "%sretroachievements.login", dir); }

static void save_login(void)
{
    const rc_client_user_t *u = rc_client_get_user_info(client);
    if (!u || fake_dir[0])
        return;
    char path[1100];
    login_path(path, sizeof path);
#ifdef _WIN32
    FILE *f = fopen(path, "wb");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
#endif
    if (!f)
        return;
    fprintf(f, "%s\n%s\n", u->username, u->token);
    fclose(f);
}

static void RC_CCONV game_loaded(int result, const char *error, rc_client_t *cl, void *user)
{
    (void)cl; (void)user;
    if (result == RC_OK) {
        rc_client_user_game_summary_t s;
        rc_client_get_user_game_summary(client, &s);
        snprintf(state_text, sizeof state_text, "%u of %u achievements", s.num_unlocked_achievements,
                 s.num_core_achievements);
        message("RetroAchievements: %u of %u unlocked", s.num_unlocked_achievements, s.num_core_achievements);
        if (report)
            printf("achievements loaded game %u, %u achievements\n", rc_client_get_game_info(client)->id,
                   s.num_core_achievements);
    } else {
        snprintf(state_text, sizeof state_text, "no set loaded: %s", error ? error : rc_error_str(result));
        message("RetroAchievements: %s", state_text);
        if (report)
            printf("achievements not loaded: %s\n", state_text);
    }
}

static void RC_CCONV logged_in(int result, const char *error, rc_client_t *cl, void *user)
{
    (void)cl; (void)user;
    if (result == RC_OK) {
        save_login();
        state_text[0] = 0;
        rc_client_begin_load_game(client, hash, game_loaded, NULL);
        return;
    }
    snprintf(state_text, sizeof state_text, "sign-in failed: %s", error ? error : rc_error_str(result));
    message("RetroAchievements %s", state_text);
    if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED) {
        char path[1100];
        login_path(path, sizeof path);
        remove(path);
    }
}

void ach_start(const char *data_dir, const char *ver, const Machine *m, const uint8_t *rom, size_t rom_size)
{
    if (client)
        return;
    snprintf(dir, sizeof dir, "%s", data_dir);
    snprintf(version, sizeof version, "%s", ver);
    const char *fake = getenv("SML_RA_FAKE");
    snprintf(fake_dir, sizeof fake_dir, "%s", fake ? fake : "");   /* "" = the real server */
    machine = m;
    md5_state_t md5;
    md5_byte_t digest[16];
    md5_init(&md5);
    md5_append(&md5, rom, (int)rom_size);
    md5_finish(&md5, digest);
    for (int i = 0; i < 16; i++)
        snprintf(hash + 2 * i, 3, "%02x", digest[i]);
    if (!done_lock)
        done_lock = SDL_CreateMutex();
    generation++;
    frames = 0;
    client = rc_client_create(read_memory, server_call);
    if (!client)
        return;
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_WARN, log_message);
    rc_client_set_event_handler(client, on_event);
    rc_client_set_hardcore_enabled(client, 0);
    state_text[0] = 0;
    if (fake_dir[0]) {
        rc_client_begin_login_with_token(client, "test", "test", logged_in, NULL);
        return;
    }
    char path[1100], user[128] = "", token[128] = "";
    login_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) {
        if (fgets(user, sizeof user, f))
            user[strcspn(user, "\r\n")] = 0;
        if (fgets(token, sizeof token, f))
            token[strcspn(token, "\r\n")] = 0;
        fclose(f);
    }
    if (user[0] && token[0]) {
        snprintf(state_text, sizeof state_text, "signing in as %s...", user);
        rc_client_begin_login_with_token(client, user, token, logged_in, NULL);
    }
}

void ach_stop(void)
{
    if (!client)
        return;
    generation++;
    rc_client_destroy(client);
    client = NULL;
    deliver();                  /* frees what is already back */
}

int ach_running(void) { return client != NULL; }
void ach_set_report(int on) { report = on; }

void ach_frame(void)
{
    if (!client)
        return;
    frames++;
    deliver();
    rc_client_do_frame(client);
}

void ach_idle(void)
{
    if (!client)
        return;
    deliver();
    rc_client_idle(client);
}

void ach_login(const char *user, const char *password)
{
    if (!client)
        return;
    rc_client_logout(client);
    snprintf(state_text, sizeof state_text, "signing in as %s...", user);
    rc_client_begin_login_with_password(client, user, password, logged_in, NULL);
}

void ach_logout(void)
{
    if (!client)
        return;
    rc_client_logout(client);
    char path[1100];
    login_path(path, sizeof path);
    remove(path);
    state_text[0] = 0;
}

int ach_logged_in(void) { return client && rc_client_get_user_info(client) != NULL; }

void ach_status(char *out, size_t len)
{
    if (!client) {
        snprintf(out, len, "off");
        return;
    }
    const rc_client_user_t *u = rc_client_get_user_info(client);
    if (!u) {
        snprintf(out, len, "%s", state_text[0] ? state_text : "not signed in");
        return;
    }
    snprintf(out, len, "%s (%u points)%s%s", u->display_name, u->score_softcore,
             state_text[0] ? ", " : "", state_text);
}

int ach_list(AchRow *rows, int max, char *summary, size_t len)
{
    summary[0] = 0;
    if (!client || !rc_client_is_game_loaded(client))
        return 0;
    rc_client_user_game_summary_t s;
    rc_client_get_user_game_summary(client, &s);
    snprintf(summary, len, "%u of %u unlocked, %u of %u points", s.num_unlocked_achievements,
             s.num_core_achievements, s.points_unlocked, s.points_core);
    rc_client_achievement_list_t *list = rc_client_create_achievement_list(
        client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    int n = 0;
    if (!list)
        return 0;
    for (uint32_t b = 0; b < list->num_buckets; b++)
        for (uint32_t i = 0; i < list->buckets[b].num_achievements && n < max; i++) {
            const rc_client_achievement_t *a = list->buckets[b].achievements[i];
            AchRow *r = &rows[n++];
            snprintf(r->title, sizeof r->title, "%s", a->title ? a->title : "");
            snprintf(r->description, sizeof r->description, "%s", a->description ? a->description : "");
            snprintf(r->progress, sizeof r->progress, "%s", a->unlocked ? "" : a->measured_progress);
            r->points = a->points;
            r->unlocked = a->unlocked != 0;
        }
    rc_client_destroy_achievement_list(list);
    return n;
}
