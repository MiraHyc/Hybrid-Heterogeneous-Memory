#ifndef _SPECIALPRINT_H_
#define _SPECIALPRINT_H_

#include <string>
#include <cstdio>

class SpecialPrint {
private:
    SpecialPrint() = delete;
    ~SpecialPrint() = delete;
public:
    static void red(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(31, false, format, args);
        va_end(args);
    }

    static void redBold(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(31, true, format, args);
        va_end(args);
    }

    static void green(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(32, false, format, args);
        va_end(args);
    }

    static void greenBold(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(32, true, format, args);
        va_end(args);
    }

    static void blue(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(34, false, format, args);
        va_end(args);
    }

    static void blueBold(const char* format, ...) {
        va_list args;
        va_start(args, format);
        _printHelper(34, true, format, args);
        va_end(args);
    }
private:
    static void print(int color_code, bool bold, const char* format, ...) {
        const char* bold_code = bold ? "1;" : "";
        char buffer[4096]; // 缓冲区存储格式化后的字符串
        
        // 处理可变参数
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 输出带ANSI颜色代码的文本
        printf("[192.168.0.189 CN]: \033[%s%dm%s\033[0m\n", bold_code, color_code, buffer);
    }
    // 内部辅助函数处理可变参数
    static void _printHelper(int color_code, bool bold, const char* format, va_list args) {
        char buffer[4096];
        vsnprintf(buffer, sizeof(buffer), format, args);
        print(color_code, bold, "%s", buffer);
    }
};




#endif // _SPECIALPRINT_H_