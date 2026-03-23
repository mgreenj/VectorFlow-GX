
#ifndef INCLUDE_COMMON_UTIL_H
#define INCLUDE_COMMON_UTIL_H

#define CHECK_RC(call, label, fmt, ...) \
    do { \
        res = (call); \
        if (res) { \
            fprintf(stderr, "%s: " fmt, __func__, ##__VA_ARGS__); \
            goto label; \
        } \
    } while (0)


#endif /* INCLUDE_COMMON_UTIL_H */