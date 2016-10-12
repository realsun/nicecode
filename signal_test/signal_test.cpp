 
/**
 * @author realsun
 * @date 2015/09/11 16:50:35
 * @brief linux signal basic using
 *  
 **/

#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

volatile int child_status = 0;
volatile int current_child_pid = 0;
volatile int process = 0;

void sigchld_handler(int signo) {
    int pid = getpid();
    printf("my son has been killed, thispid[%d]\n", pid);
    child_status = 1;
}

void sigalrm_handler(int signo) {
    int pid = getpid();
    printf("this is an alrm signal, thispid[%d]\n", pid);
}

void sighup_handler(int signo) {
    int pid = getpid();
    printf("this is parent sighup, thispid[%d]\n", pid);
}

void master_quit_handler();
void worker_quit_handler();

void sigterm_handler(int signo) {
    int pid = getpid();
    printf("this is sigterm handler, thispid[%d]\n", pid);
    switch (process) {
        // master
        case 0:
            master_quit_handler();
            break;
        // worker
        case 1:
            worker_quit_handler();
            break;
    }
}

void child_sighup_handler(int signo) {
    int pid = getpid();
    printf("this is child sighup, thispid[%d]\n", pid);
    exit(0);
}

void master_quit_handler() {
    kill(current_child_pid, SIGTERM);

    int status;
    waitpid(current_child_pid, &status, 0);
 
    printf("master quit\n");
    exit(0);
}

void worker_quit_handler() {
    printf("worker quit\n");
    // consider parent process
    _exit(0);
}

// daemon
void daemonize(void) {
    int fd;

    if (fork() != 0) {
        exit(0);
    }
    // create a now session
    setsid();

    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
}

void child_handler(int fp) {
    // set worker
    process = 1;

    struct sigaction csa;
    memset(&csa, 0, sizeof(struct sigaction));
    sigemptyset(&csa.sa_mask);
    csa.sa_handler = child_sighup_handler;
    if (sigaction(SIGHUP, &csa, NULL) == -1) {
        exit(-1);
    }

    char child_str[40] = "enter children process\n";
    char child_while_str[40] = "enter children while\n";

    int thispid = getpid();
    printf("enter children process, children_pid[%d]\n", thispid);
    write(fp, child_str, sizeof(child_str));


    int i = 0;
    while (1) {
        i++;
        if (i == 10) {
            i = 0;
        }

        write(fp, child_while_str, sizeof(child_while_str));

        sleep(1);
    }
    close(fp);
    exit(0);
}

void reap_child(int fp) {
    int status;
    waitpid(current_child_pid, &status, 0);

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
    } else if (pid == 0) {
        return child_handler(fp);
    } else {
        current_child_pid = pid;
        printf("raise child success, pid[%d]\n", pid);
    }    
}

int main(int argc, char **argv) 
{
    // init signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigchld_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        exit(-1);
    }
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigalrm_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        exit(-1);
    }
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighup_handler;
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        exit(-1);
    }
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        exit(-1);
    }


    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    // 如果将SIGHUP信号放到set中，子进程将获取不到这个信号，可能这个信号需要suspend得到
    //sigaddset(&set, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        printf("sigprocmask failed\n");
        exit(-1);
    }

    sigemptyset(&set);

    daemonize();

    int fp;
    fp = open("the file you want to write",
            O_RDWR|O_CREAT|O_APPEND,
            0666);

    if (fp < 0) {
        return -1;
    }

    int childpid;
    if ((childpid = fork()) == 0) {
        child_handler(fp);

    } else {
        int parent_pid = getpid();
        current_child_pid = childpid;


        char str[40] = "enter father process\n";
        char enter_while_str[20] = "enter while\n";

        // father
        printf("enter father process, parent_pid[%d]\n", parent_pid);
        write(fp, str, sizeof(str));

        // trigger time set
        struct itimerval itv;
        itv.it_interval.tv_sec = 0;
        itv.it_interval.tv_usec = 0;
        itv.it_value.tv_sec = 1;
        itv.it_value.tv_usec = 0;
        if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
            printf("setitimer failed\n");
            exit(-1);
        }

        while (1) {
            sigsuspend(&set);

            if (child_status) {
                child_status = 0;
                reap_child(fp);
            }


            if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
                printf("setitimer failed\n");
                exit(-1);
            }
            printf("enter while, pid[%d]\n", parent_pid);
            write(fp, enter_while_str, sizeof(enter_while_str));
        }
        close(fp);
    }
}

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
