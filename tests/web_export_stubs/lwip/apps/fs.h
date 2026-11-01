#ifndef TEST_LWIP_FS_H
#define TEST_LWIP_FS_H
typedef struct fs_file_extension fs_file_extension;
struct fs_file {
    const char *data;
    int len;
    int index;
    int flags;
    fs_file_extension *pextension;
};
#define FS_FILE_FLAGS_HEADER_INCLUDED 1
#endif
