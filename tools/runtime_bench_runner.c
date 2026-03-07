#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_millis(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

static void sleep_millis(unsigned millis)
{
    struct timespec req;

    req.tv_sec = (time_t)(millis / 1000u);
    req.tv_nsec = (long)((millis % 1000u) * 1000000u);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static int parse_uint(const char* text, uint64_t* value_out)
{
    char* end = NULL;
    unsigned long long parsed;

    if (!text || !value_out || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0') {
        return 0;
    }

    *value_out = (uint64_t)parsed;
    return 1;
}

static void print_usage(FILE* out)
{
    fprintf(out,
            "Usage: runtime_bench_runner [--print-now-ms] [--timeout-ms N] [--log-file PATH] -- CMD [ARGS...]\n");
}

int main(int argc, char** argv)
{
    const char* log_file = NULL;
    int print_now = 0;
    uint64_t timeout_ms = 0u;
    int cmd_index = -1;
    pid_t pid;
    uint64_t start_ms;
    uint64_t wall_ms;
    uint64_t term_deadline = 0u;
    const char* status_text = "ok";
    int timed_out = 0;
    int wait_status = 0;
    int rc = -1;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--timeout-ms") == 0) {
            if (i + 1 >= argc || !parse_uint(argv[i + 1], &timeout_ms)) {
                fprintf(stderr, "runtime_bench_runner: invalid --timeout-ms\n");
                return 2;
            }
            ++i;
        } else if (strcmp(argv[i], "--print-now-ms") == 0) {
            print_now = 1;
        } else if (strcmp(argv[i], "--log-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "runtime_bench_runner: missing --log-file path\n");
                return 2;
            }
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--") == 0) {
            cmd_index = i + 1;
            break;
        } else {
            fprintf(stderr, "runtime_bench_runner: unknown option %s\n", argv[i]);
            print_usage(stderr);
            return 2;
        }
    }

    if (print_now) {
        printf("%llu\n", (unsigned long long)monotonic_millis());
        return 0;
    }

    if (cmd_index < 0 || cmd_index >= argc) {
        print_usage(stderr);
        return 2;
    }

    if (!log_file || log_file[0] == '\0') {
        fprintf(stderr, "runtime_bench_runner: --log-file is required\n");
        return 2;
    }

    start_ms = monotonic_millis();
    pid = fork();
    if (pid < 0) {
        perror("runtime_bench_runner: fork");
        return 1;
    }

    if (pid == 0) {
        int out_fd = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int in_fd = open("/dev/null", O_RDONLY);
        if (out_fd < 0) {
            perror("runtime_bench_runner: open log");
            _exit(126);
        }
        if (in_fd >= 0) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        dup2(out_fd, STDOUT_FILENO);
        dup2(out_fd, STDERR_FILENO);
        if (out_fd > STDERR_FILENO) {
            close(out_fd);
        }
        execvp(argv[cmd_index], &argv[cmd_index]);
        perror("runtime_bench_runner: execvp");
        _exit(127);
    }

    for (;;) {
        pid_t waited = waitpid(pid, &wait_status, WNOHANG);
        uint64_t now_ms = monotonic_millis();

        if (waited == pid) {
            break;
        }
        if (waited < 0) {
            perror("runtime_bench_runner: waitpid");
            return 1;
        }

        if (timeout_ms > 0u && !timed_out && now_ms - start_ms >= timeout_ms) {
            status_text = "timeout";
            timed_out = 1;
            term_deadline = now_ms + 2000u;
            if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
                perror("runtime_bench_runner: kill(SIGTERM)");
                return 1;
            }
        } else if (timed_out == 1 && now_ms >= term_deadline) {
            status_text = "killed";
            timed_out = 2;
            if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
                perror("runtime_bench_runner: kill(SIGKILL)");
                return 1;
            }
        }

        sleep_millis(10u);
    }

    wall_ms = monotonic_millis() - start_ms;
    if (WIFEXITED(wait_status)) {
        rc = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        rc = -WTERMSIG(wait_status);
    }

    printf("status=%s\n", status_text);
    printf("rc=%d\n", rc);
    printf("wall_ms=%llu\n", (unsigned long long)wall_ms);
    return 0;
}
