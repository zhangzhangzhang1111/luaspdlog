#ifndef LUASPDLOG_IOS_COMPAT_H
#define LUASPDLOG_IOS_COMPAT_H
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/** @brief iOS 无子进程能力，为历史 Lua 标准库提供明确的失败结果。 */
static inline int luaspdlog_ios_system(const char *command) {
    if (!command) return 0;
    errno = ENOSYS;
    return -1;
}
/** @brief iOS 不支持管道子进程，禁止调用 SDK 中标记 unavailable 的接口。 */
static inline FILE *luaspdlog_ios_popen(const char *command, const char *mode) {
    (void)command; (void)mode;
    errno = ENOSYS;
    return NULL;
}
/** @brief 与不支持的 popen 接口配对，始终报告 ENOSYS。 */
static inline int luaspdlog_ios_pclose(FILE *file) {
    (void)file;
    errno = ENOSYS;
    return -1;
}
#define system(command) luaspdlog_ios_system(command)
#define popen(command, mode) luaspdlog_ios_popen(command, mode)
#define pclose(file) luaspdlog_ios_pclose(file)
#endif
