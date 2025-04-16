#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

// 执行命令并返回输出
class Utils {
private:
    Utils() = delete;
    ~Utils() = delete;
public:
    static int get_pid() {
        return getpid();
    }
    static char* execute_command(const char* cmd) {
        FILE* pipe = popen(cmd, "r");
        if (!pipe) return NULL;

        static char buffer[4096];
        char* result = (char*)malloc(4096);
        result[0] = '\0';

        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            strcat(result, buffer);
        }

        pclose(pipe);
        return result;
    }
    static void numactl_p(){
        int pid = getpid();
        // 构造 numastat 命令
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "numastat -p %d", pid);



        // 执行命令并获取输出
        char* output = execute_command(cmd);
        if (output) {
            printf("\n===== NUMA 状态 (PID: %d) =====\n%s", pid, output);
            free(output);
        } else {
            perror("执行 numastat 失败\n");
        }
        printf("\n");
    }
};








#endif /* _UTILS_H_ */