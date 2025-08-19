#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <cstdio>

void launch(const char* cmd, int px, int py, const char* display_name) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (display_name) setenv("DISPLAY", display_name, 1);
        const char* xauth = getenv("XAUTHORITY");
        if (xauth) setenv("XAUTHORITY", xauth, 1);
        char bufx[32], bufy[32];
        snprintf(bufx, sizeof(bufx), "%d", px);
        snprintf(bufy, sizeof(bufy), "%d", py);
        setenv("PRISM_LAUNCH_X", bufx, 1);
        setenv("PRISM_LAUNCH_Y", bufy, 1);
        int fd = open("/tmp/prism.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) fd = open("/dev/null", O_WRONLY);
        if (fd != -1) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        execlp("sh", "sh", "-c", cmd, nullptr);
        std::exit(1);
    }
}