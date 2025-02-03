#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <errno.h>
#include <sys/un.h>
#include <linux/unistd.h>

#define USER_TOOL_SOCKET "/tmp/user_tool.sock"

// Helper function to get syscall name
const char* get_syscall_name(long syscall_nr) {
    switch(syscall_nr) {
        case SYS_socket:
            return "socket";
        case SYS_connect:
            return "connect";
        default:
            return "unknown";
    }
}

// Function to connect to user-tool
int connect_to_user_tool() {
    printf("[Supervisor] Connecting to user-tool daemon...\n");
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("[Supervisor] Failed to create socket for user-tool connection");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, USER_TOOL_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("[Supervisor] Failed to connect to user-tool");
        close(sock);
        return -1;
    }
    printf("[Supervisor] Successfully connected to user-tool daemon\n");
    return sock;
}

// Function to ask user-tool for permission
int ask_permission(int user_tool_sock, int syscall_nr) {
    char request[64];
    snprintf(request, sizeof(request), "SYSCALL:%d", syscall_nr);
    
    printf("[Supervisor] Requesting permission for syscall %s (%d)\n", 
           get_syscall_name(syscall_nr), syscall_nr);
    
    if (write(user_tool_sock, request, strlen(request)) == -1) {
        perror("[Supervisor] Failed to send request to user-tool");
        return -1;
    }

    char response[8];
    ssize_t bytes = read(user_tool_sock, response, sizeof(response) - 1);
    if (bytes <= 0) {
        perror("[Supervisor] Failed to read response from user-tool");
        return -1;
    }
    response[bytes] = '\0';
    printf("[Supervisor] User-tool response: %s\n", response);

    return strcmp(response, "ALLOW") == 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    printf("\n[Supervisor] Starting supervision of program: %s\n", argv[1]);
    
    // Build absolute path for target program
    char abs_path[1024];
    if (argv[1][0] != '/') {
        if (realpath(argv[1], abs_path) == NULL) {
            perror("[Supervisor] Failed to resolve program path");
            return 1;
        }
        argv[1] = abs_path;
    }
    printf("[Supervisor] Full program path: %s\n", argv[1]);

    int user_tool_sock = connect_to_user_tool();
    if (user_tool_sock == -1) {
        fprintf(stderr, "[Supervisor] ERROR: Make sure user-tool is running first!\n");
        return 1;
    }

    pid_t child = fork();
    if (child == -1) {
        perror("[Supervisor] Fork failed");
        return 1;
    }

    if (child == 0) {
        // Child process - this will become the monitored program
        printf("[Supervisor] Starting monitored program (PID: %d)\n", getpid());
        
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
            perror("[Supervisor] Failed to set up ptrace");
            exit(1);
        }

        // Stop to let parent set up monitoring
        raise(SIGSTOP);

        // Set up seccomp
        printf("[Supervisor] Setting up system call filtering...\n");
        prctl(PR_SET_NO_NEW_PRIVS, 1);
        
        struct sock_filter filter[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 
                     (offsetof(struct seccomp_data, nr))),
            
            // Check for socket syscall
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE),
            
            // Check for connect syscall
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_connect, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE),
            
            // Allow all other syscalls
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
        };

        struct sock_fprog prog = {
            .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
            .filter = filter,
        };

        if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == -1) {
            perror("[Supervisor] Failed to set up seccomp filter");
            exit(1);
        }

        printf("[Supervisor] Executing monitored program: %s\n", argv[1]);
        execv(argv[1], &argv[1]);
        perror("[Supervisor] Failed to execute program");
        exit(1);
    }

    // Parent process - this is the supervisor
    printf("[Supervisor] Monitoring process PID: %d\n", child);
    
    // Wait for child to stop
    int status;
    if (waitpid(child, &status, 0) == -1) {
        perror("[Supervisor] Initial waitpid failed");
        return 1;
    }

    // Set up tracing options
    if (ptrace(PTRACE_SETOPTIONS, child, 0, 
               PTRACE_O_TRACESYSGOOD | 
               PTRACE_O_TRACESECCOMP) == -1) {
        perror("[Supervisor] Failed to set ptrace options");
        return 1;
    }

    // Continue the child
    ptrace(PTRACE_CONT, child, 0, 0);

    printf("[Supervisor] Beginning system call monitoring...\n\n");

    while (1) {
        if (waitpid(child, &status, 0) == -1) {
            perror("[Supervisor] waitpid failed");
            break;
        }

        if (WIFEXITED(status)) {
            printf("[Supervisor] Monitored program exited with status %d\n", 
                   WEXITSTATUS(status));
            break;
        }

        if (WIFSTOPPED(status)) {
            int stopsig = WSTOPSIG(status);
            if (stopsig == (SIGTRAP | 0x80) || 
                (stopsig == SIGTRAP && status >> 8 == (SIGTRAP | PTRACE_EVENT_SECCOMP << 8))) {
                
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, child, 0, &regs) == -1) {
                    perror("[Supervisor] Failed to get registers");
                    continue;
                }

                #ifdef __x86_64__
                long syscall = regs.orig_rax;
                #else
                long syscall = regs.orig_eax;
                #endif

                if (syscall == SYS_socket || syscall == SYS_connect) {
                    printf("[Supervisor] Intercepted %s system call\n", 
                           get_syscall_name(syscall));
                    
                    int allow = ask_permission(user_tool_sock, syscall);
                    if (!allow) {
                        printf("[Supervisor] Blocking %s system call\n", 
                               get_syscall_name(syscall));
                        #ifdef __x86_64__
                        regs.rax = -EPERM;
                        #else
                        regs.eax = -EPERM;
                        #endif
                        if (ptrace(PTRACE_SETREGS, child, 0, &regs) == -1) {
                            perror("[Supervisor] Failed to set registers");
                        }
                    } else {
                        printf("[Supervisor] Allowing %s system call\n", 
                               get_syscall_name(syscall));
                    }
                }
            }
            ptrace(PTRACE_SYSCALL, child, 0, 0);
        }
    }

    close(user_tool_sock);
    return 0;
}
