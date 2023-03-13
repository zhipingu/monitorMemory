#include <signal.h>
#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
// #include <linux/user.h>
// #include <sys/reg.h>
#include <sys/user.h>
#include <execinfo.h>
#include <string.h>
#include <errno.h>


#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#ifndef sizeof_array
#define sizeof_array(a) ((sizeof(a)) / sizeof(a[0]))
#endif

#ifndef LOG
#define LOG(fmt, ...) printf(fmt, ## __VA_ARGS__)
// #define LOG(fmt, ...) serverLog(LL_WARNING | LL_RAW, #fmt, ##__VA_ARGS__)
#endif

static void print_trace(void)
{
     void *array[100];
     char **strings;
     int size, i;

     size = backtrace(array, sizeof_array(array));
     strings = backtrace_symbols(array, size);
     if (strings != NULL)
     {
         LOG("Obtained %d stack frames.\n", size);
         for (i = 0; i < size; i++)
         LOG("%s\n", strings[i]);
     }

     free(strings);
}

enum
{
     DR7_BREAK_ON_EXEC = 0,
     DR7_BREAK_ON_WRITE = 1,
     DR7_BREAK_ON_RW = 3,
};

enum
{
     DR7_LEN_1 = 0,
     DR7_LEN_2 = 1,
     DR7_LEN_4 = 3,
};

typedef struct
{
     char l0 : 1;
     char g0 : 1;
     char l1 : 1;
     char g1 : 1;
     char l2 : 1;
     char g2 : 1;
     char l3 : 1;
     char g3 : 1;
     char le:  1;
     char ge:  1;
     char reserved_10:    1;
     char rtm:            1;
     char reserved_12:    1;
     char gd:             1;
     char reserved_14_15: 2;
     char rw0 : 2;
     char len0 : 2;
     char rw1 : 2;
     char len1 : 2;
     char rw2 : 2;
     char len2 : 2;
     char rw3 : 2;
     char len3 : 2;
} dr7_t;

typedef void signal_handler_t(int, siginfo_t *, void *);

int watchpoint(void *addr, signal_handler_t handler)
{
     pid_t child;
     pid_t parent = getpid();
     struct sigaction trap_action;
     int child_stat = 0;

     sigaction(SIGTRAP, NULL, &trap_action);
     trap_action.sa_sigaction = handler;
     trap_action.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER;
     sigaction(SIGTRAP, &trap_action, NULL);

     if ((child = fork()) == 0)
     {
         int retval = EXIT_SUCCESS;

         dr7_t dr7 = {0};
         dr7.l0 = (char)1;
         dr7.le = 1;
         dr7.ge = 1;
         dr7.reserved_10 = 1;
         dr7.rw0 = DR7_BREAK_ON_WRITE;
         dr7.len0 = DR7_LEN_4;

         if (ptrace(PTRACE_ATTACH, parent, NULL, NULL))
         {
             LOG("Failed to attach pid %d: %s\n",
             parent, strerror(errno));
             return EXIT_FAILURE;
         }

         // sleep(1);
          /*
           父进程对子进程ptrace_attach时，陷入内核态，设置了子进程为traced状态，同时给子进程发送sigstop信号，而且会唤醒子进程，
           子进程被唤醒后，由于状态为traced,所以do_signal里会给父进程发送sigchld信号，之后父进程waitpid就返回了，继续往下
          */
         waitpid(parent,NULL,0);

         if (ptrace(PTRACE_POKEUSER, parent, offsetof(struct user, u_debugreg[0]), addr))
         {
             LOG("Failed to poke user %d: %s\n",
             parent, strerror(errno));
             return EXIT_FAILURE;
         }

         if (ptrace(PTRACE_POKEUSER, parent, offsetof(struct user, u_debugreg[7]), dr7))
         {
             LOG("Failed to poke user %d: %s\n",
             parent, strerror(errno));
             return EXIT_FAILURE;
         }

         if (ptrace(PTRACE_DETACH, parent, NULL, NULL))
         {
             LOG("Failed to detach %d: %s\n",
             parent, strerror(errno));
             return EXIT_FAILURE;
         }
         printf("----------------child will exit----------------\n");
         exit(retval);
     }
     waitpid(child, &child_stat, 0);

     if (WIFEXITED(child_stat))
     {
         LOG("child of pid %d exit!\n", parent);
         return 0;
     }
     printf("-----------not expected-----------------\n");
     return 1;
}

typedef struct testType{
    int a;
    char *p;
} testType;

extern testType t;

void trap(int sig, siginfo_t *info, void *context);
void trap(int sig, siginfo_t *info, void *context)
{
    static int cnt = 0;
    UNUSED(sig);
    UNUSED(info);
    UNUSED(context);

//    void *tail;
//    bool tail_is_null;

//    tail = server.clients->tail;
//    tail_is_null = tail == NULL;

//    if (!tail_is_null)
//    {
//    // return;
//    }
//    LOG("cached exception: %p %d\n", tail, tail_is_null);
//    printf("-------------cnt:%d--------------\n",++cnt);
    if(t.p == NULL){
        printf("t.p is null,t.a is %d\n", t.a);
        print_trace();
    }

}

testType t;

void func(int *p){
    t.a = 1000;
    *p = 0;
    int a = 100;
    t.a = a;
}

int main(){
    printf("--------------main start---------------\n");
    printf("--------------parent pid: %d----------------\n",getpid());
    watchpoint(&t.p,trap);
    for(int i=10; i >= 0; --i){
        t.a = i;
        t.p = 2*i;
//        printf("------------%d---------------\n",i);
    }
    func(&t.p);
    printf("--------------main end------------------\n");
}




