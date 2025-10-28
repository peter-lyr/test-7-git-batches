#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define MAX_GROUP_SIZE (100 * 1024 * 1024LL) // 100MB
#define MAX_FILE_SIZE (50 * 1024 * 1024LL)   // 50MB
#define MAX_ITEMS 100000
#define MAX_DIRECTORY_QUEUE 10000

typedef enum { TYPE_FILE, TYPE_DIRECTORY } ItemType;

typedef struct {
  char path[MAX_PATH_LENGTH];
  long long size;
  ItemType type;
} FileItem;

typedef struct {
  FileItem *items;
  int count;
  int capacity;
  long long total_size;
} FileGroup;

typedef struct {
  char path[MAX_PATH_LENGTH];
  long long size;
} SkippedFile;

typedef struct {
  FileGroup *groups;
  int group_count;
  int groups_capacity;
  long long total_input_size;
  long long skipped_size;
  int total_files;
  int total_directories;
  SkippedFile *skipped_files; // 新增：跳过的大文件数组
  int skipped_count;          // 新增：跳过大文件数量
  int skipped_capacity;       // 新增：跳过大文件数组容量
} GroupResult;

typedef struct {
  wchar_t path[MAX_PATH_LENGTH];
  int depth;
} DirectoryEntry;

// 修改print_skipped_files函数，返回需要添加到分组的文件列表
typedef struct {
  char **gitignore_files; // .gitignore文件路径
  int gitignore_count;
  char **split_files; // 拆分文件路径
  int split_count;
  int has_large_files; // 新增：标记是否处理过大文件
} AdditionalFiles;

// 函数声明
void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);
wchar_t *char_to_wchar(const char *str);
char *wchar_to_char(const wchar_t *wstr);
int safe_path_join(wchar_t *dest, size_t dest_size, const wchar_t *path1,
                   const wchar_t *path2);
void normalize_path(char *path);
void format_size(long long size, char *buffer, size_t buffer_size);
void draw_progress_bar(int current, int total, const char *prefix);
long long calculate_directory_size_iterative(const wchar_t *wpath,
                                             long long *total_scanned_size);
int create_directory_recursive(const wchar_t *wpath);
int copy_file_with_backup(const char *src_path, const char *backup_base_path);
void collect_items_iterative(const wchar_t *wpath, FileItem *items,
                             int *item_count, long long *total_scanned_size,
                             long long *skipped_files_size,
                             GroupResult *result);
void process_input_path(const char *path, FileItem *items, int *item_count,
                        long long *total_input_size,
                        long long *total_scanned_size,
                        long long *skipped_files_size, GroupResult *result);
int compare_items(const void *a, const void *b);
GroupResult group_files(FileItem *items, int item_count);
void print_groups(const GroupResult *result);
AdditionalFiles print_skipped_files(GroupResult *result);
void print_statistics(const GroupResult *result, long long total_scanned_size,
                      long long skipped_files_size);
void validate_result(const GroupResult *result, long long input_total_size,
                     long long total_scanned_size,
                     long long skipped_files_size);
void free_group_result(GroupResult *result);
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size);
void execute_git_commands(const GroupResult *result,
                          const char *commit_info_file);
int run_grouping_test_with_git(char *paths[], int path_count,
                               const char *commit_info_file);
int run_grouping_test(char *paths[], int path_count);
char **get_git_status_paths(int *path_count);
void free_git_status_paths(char **paths, int path_count);

int update_gitignore_for_skipped_file(const char *skipped_file_path);

void collect_split_directory_files(const wchar_t *wdir_path, FileItem *items,
                                   int *item_count);

int is_split_complete(const char *file_path, const char *split_dir,
                      long long file_size);

void normalize_directory_path(char *path);

void print_detailed_group_info(const FileGroup *group, int group_index);

int delete_original_file(const char *file_path);

// 安全的内存分配函数
void *safe_malloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    fprintf(stderr, "错误: 内存分配失败 (申请大小: %zu 字节)\n", size);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

void *safe_realloc(void *ptr, size_t size) {
  void *new_ptr = realloc(ptr, size);
  if (!new_ptr && size > 0) {
    fprintf(stderr, "错误: 内存重新分配失败 (申请大小: %zu 字节)\n", size);
    free(ptr);
    exit(EXIT_FAILURE);
  }
  return new_ptr;
}

// 字符编码转换函数
wchar_t *char_to_wchar(const char *str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  if (len <= 0) {
    return NULL;
  }

  wchar_t *wstr = (wchar_t *)safe_malloc(len * sizeof(wchar_t));
  if (MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len) == 0) {
    free(wstr);
    return NULL;
  }
  return wstr;
}

char *wchar_to_char(const wchar_t *wstr) {
  if (!wstr)
    return NULL;

  int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  if (len <= 0) {
    return NULL;
  }

  char *str = (char *)safe_malloc(len);
  if (WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL) == 0) {
    free(str);
    return NULL;
  }
  return str;
}

// 安全的路径拼接函数
int safe_path_join(wchar_t *dest, size_t dest_size, const wchar_t *path1,
                   const wchar_t *path2) {
  if (_snwprintf_s(dest, dest_size, _TRUNCATE, L"%s\\%s", path1, path2) < 0) {
    dest[dest_size - 1] = L'\0';
    return 0;
  }
  return 1;
}

// 修改：改进的路径规范化函数
void normalize_path(char *path) {
  if (!path || strlen(path) == 0)
    return;

  size_t len = strlen(path);

  // 统一路径分隔符为反斜杠
  for (char *p = path; *p; p++) {
    if (*p == '/')
      *p = '\\';
  }

  // 移除末尾的反斜杠（保留根目录的情况）
  while (len > 1 && path[len - 1] == '\\') {
    // 检查是否是根目录或盘符根目录（如 "C:\"）
    if (len == 3 && path[1] == ':' && path[2] == '\\') {
      break; // 保留盘符根目录
    }
    if (len == 2 && path[0] == '\\' && path[1] == '\\') {
      break; // 保留网络根目录
    }
    path[len - 1] = '\0';
    len--;
  }
}

// 格式化文件大小显示
void format_size(long long size, char *buffer, size_t buffer_size) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double display_size = (double)size;

  while (display_size >= 1024.0 && unit_index < 4) {
    display_size /= 1024.0;
    unit_index++;
  }

  if (unit_index == 0) {
    snprintf(buffer, buffer_size, "%lld %s", size, units[unit_index]);
  } else {
    snprintf(buffer, buffer_size, "%.2f %s", display_size, units[unit_index]);
  }
}

// 新增：检查文件夹中是否包含超过MAX_FILE_SIZE的文件
int directory_contains_large_files(const wchar_t *wpath,
                                   long long *total_scanned_size) {
  int has_large_files = 0;
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  while (queue_front != queue_rear && !has_large_files) {
    DirectoryEntry current = queue[queue_front];
    queue_front = (queue_front + 1) % MAX_DIRECTORY_QUEUE;

    if (current.depth > 50) {
      continue;
    }

    wchar_t search_path[MAX_PATH_LENGTH];
    if (!safe_path_join(search_path, MAX_PATH_LENGTH, current.path, L"*")) {
      continue;
    }

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
      continue;
    }

    do {
      if (wcscmp(find_data.cFileName, L".") == 0 ||
          wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }

      wchar_t full_path[MAX_PATH_LENGTH];
      if (!safe_path_join(full_path, MAX_PATH_LENGTH, current.path,
                          find_data.cFileName)) {
        continue;
      }

      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
          wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
          queue[queue_rear].depth = current.depth + 1;
          queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;
        }
      } else {
        ULARGE_INTEGER file_size;
        file_size.LowPart = find_data.nFileSizeLow;
        file_size.HighPart = find_data.nFileSizeHigh;
        *total_scanned_size += file_size.QuadPart;

        // 检查文件大小是否超过限制
        if (file_size.QuadPart > MAX_FILE_SIZE) {
          has_large_files = 1;
          char *char_path = wchar_to_char(full_path);
          if (char_path) {
            printf("       发现大文件: %s", char_path);
            char size_str[32];
            format_size(file_size.QuadPart, size_str, sizeof(size_str));
            printf(" (%s)\n", size_str);
            free(char_path);
          }
          break; // 发现一个大文件就停止检查
        }
      }
    } while (FindNextFileW(hFind, &find_data) && !has_large_files);

    FindClose(hFind);
  }

  free(queue);
  return has_large_files;
}

// 绘制纯文本进度条
void draw_progress_bar(int current, int total, const char *prefix) {
  const int bar_width = 50;
  float progress = (float)current / total;
  int pos = (int)(bar_width * progress);

  printf("\r%s [", prefix);
  for (int i = 0; i < bar_width; ++i) {
    if (i < pos) {
      printf("=");
    } else if (i == pos) {
      printf(">");
    } else {
      printf(" ");
    }
  }
  printf("] %d/%d (%.1f%%)", current, total, progress * 100.0);
  fflush(stdout);
}

// 新增：检查目录是否已处理的辅助函数
int is_directory_already_processed(const char *dir_path, char **processed_dirs,
                                   int processed_count) {
  if (!dir_path || !processed_dirs) {
    return 0;
  }

  // 确保目录路径格式一致（以反斜杠结尾）
  char normalized_dir[MAX_PATH_LENGTH];
  strcpy_s(normalized_dir, MAX_PATH_LENGTH, dir_path);
  normalize_directory_path(normalized_dir);

  for (int i = 0; i < processed_count; i++) {
    char normalized_processed[MAX_PATH_LENGTH];
    strcpy_s(normalized_processed, MAX_PATH_LENGTH, processed_dirs[i]);
    normalize_directory_path(normalized_processed);

    if (strcmp(normalized_dir, normalized_processed) == 0) {
      return 1;
    }

    // 检查是否是子目录关系
    if (strstr(normalized_dir, normalized_processed) == normalized_dir) {
      // 当前目录是已处理目录的子目录
      return 1;
    }
    if (strstr(normalized_processed, normalized_dir) == normalized_processed) {
      // 已处理目录是当前目录的子目录
      return 1;
    }
  }

  return 0;
}

// 修改：改进的目录大小计算函数，避免重复统计
long long calculate_directory_size_iterative(const wchar_t *wpath,
                                             long long *total_scanned_size) {
  long long total_size = 0;
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  // 新增：用于跟踪已计算大小的目录，避免重复
  wchar_t **processed_dirs =
      (wchar_t **)safe_malloc(sizeof(wchar_t *) * MAX_DIRECTORY_QUEUE);
  int processed_count = 0;

  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  // 添加初始目录到已处理列表
  processed_dirs[processed_count] =
      (wchar_t *)safe_malloc((wcslen(wpath) + 1) * sizeof(wchar_t));
  wcscpy_s(processed_dirs[processed_count], MAX_PATH_LENGTH, wpath);
  processed_count++;

  while (queue_front != queue_rear) {
    DirectoryEntry current = queue[queue_front];
    queue_front = (queue_front + 1) % MAX_DIRECTORY_QUEUE;

    if (current.depth > 100) {
      continue;
    }

    wchar_t search_path[MAX_PATH_LENGTH];
    if (!safe_path_join(search_path, MAX_PATH_LENGTH, current.path, L"*")) {
      continue;
    }

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
      continue;
    }

    do {
      if (wcscmp(find_data.cFileName, L".") == 0 ||
          wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }

      wchar_t full_path[MAX_PATH_LENGTH];
      if (!safe_path_join(full_path, MAX_PATH_LENGTH, current.path,
                          find_data.cFileName)) {
        continue;
      }

      // 检查是否已处理过此目录
      int already_processed = 0;
      for (int i = 0; i < processed_count; i++) {
        if (wcscmp(processed_dirs[i], full_path) == 0) {
          already_processed = 1;
          break;
        }
      }

      if (already_processed) {
        continue;
      }

      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
          wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
          queue[queue_rear].depth = current.depth + 1;
          queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

          // 添加到已处理列表
          if (processed_count < MAX_DIRECTORY_QUEUE) {
            processed_dirs[processed_count] = (wchar_t *)safe_malloc(
                (wcslen(full_path) + 1) * sizeof(wchar_t));
            wcscpy_s(processed_dirs[processed_count], MAX_PATH_LENGTH,
                     full_path);
            processed_count++;
          }
        }
      } else {
        ULARGE_INTEGER file_size;
        file_size.LowPart = find_data.nFileSizeLow;
        file_size.HighPart = find_data.nFileSizeHigh;
        total_size += file_size.QuadPart;
        *total_scanned_size += file_size.QuadPart;
      }
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);
  }

  // 释放内存
  for (int i = 0; i < processed_count; i++) {
    free(processed_dirs[i]);
  }
  free(processed_dirs);
  free(queue);

  return total_size;
}

// 创建目录递归函数
int create_directory_recursive(const wchar_t *wpath) {
  wchar_t temp_path[MAX_PATH_LENGTH];
  wchar_t *p = NULL;
  size_t len = wcslen(wpath);

  // 复制路径到临时缓冲区
  wcscpy_s(temp_path, MAX_PATH_LENGTH, wpath);

  // 逐级创建目录
  for (p = temp_path + 1; *p; p++) {
    if (*p == L'\\') {
      *p = L'\0';

      // 检查目录是否存在
      DWORD attr = GetFileAttributesW(temp_path);
      if (attr == INVALID_FILE_ATTRIBUTES) {
        // 目录不存在，创建它
        if (!CreateDirectoryW(temp_path, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_ALREADY_EXISTS) {
            wprintf(L"[错误] 无法创建目录: %s (错误: %lu)\n", temp_path, error);
            return 0;
          }
        }
      } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        wprintf(L"[错误] 路径不是目录: %s\n", temp_path);
        return 0;
      }

      *p = L'\\';
    }
  }

  // 创建最终目录
  DWORD attr = GetFileAttributesW(temp_path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryW(temp_path, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        wprintf(L"[错误] 无法创建目录: %s (错误: %lu)\n", temp_path, error);
        return 0;
      }
    }
  }

  return 1;
}

// 修改：改进的文件备份函数，增强路径处理
int copy_file_with_backup(const char *src_path, const char *backup_base_path) {
  // 转换源路径为宽字符
  wchar_t *wsrc_path = char_to_wchar(src_path);
  if (!wsrc_path) {
    printf("[错误] 无法转换源路径编码: %s\n", src_path);
    return 0;
  }

  // 首先验证源文件是否存在且可访问
  DWORD src_attr = GetFileAttributesW(wsrc_path);
  if (src_attr == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    printf("[错误] 源文件无法访问: %s (错误: %lu)\n", src_path, error);
    free(wsrc_path);
    return 0;
  }

  if (src_attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("[错误] 源路径是目录而不是文件: %s\n", src_path);
    free(wsrc_path);
    return 0;
  }

  // 获取当前工作目录作为Git仓库路径
  char git_repo_path[MAX_PATH_LENGTH];
  if (!GetCurrentDirectoryA(MAX_PATH_LENGTH, git_repo_path)) {
    printf("[错误] 无法获取当前工作目录\n");
    free(wsrc_path);
    return 0;
  }

  // 规范化路径
  normalize_path(git_repo_path);

  // 构造备份基础路径
  char backup_dir[MAX_PATH_LENGTH];
  snprintf(backup_dir, MAX_PATH_LENGTH, "%s-backup", git_repo_path);

  // 计算相对路径并构建完整的备份路径
  char backup_path[MAX_PATH_LENGTH];

  // 检查源路径是否在Git仓库路径下
  if (strstr(src_path, git_repo_path) == src_path) {
    // 源路径包含Git仓库路径，提取相对路径
    const char *relative_part = src_path + strlen(git_repo_path);

    // 跳过路径分隔符
    if (*relative_part == '\\' || *relative_part == '/') {
      relative_part++;
    }

    // 构建完整备份路径
    if (strlen(relative_part) > 0) {
      snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s", backup_dir,
               relative_part);
    } else {
      // 如果相对路径为空，说明就是仓库根目录下的文件
      const char *filename = strrchr(src_path, '\\');
      if (!filename) {
        filename = strrchr(src_path, '/');
      }
      if (filename) {
        filename++;
      } else {
        filename = src_path;
      }
      snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s", backup_dir, filename);
    }
  } else {
    // 源路径不在Git仓库路径下，使用完整路径但替换盘符等特殊字符
    char safe_path[MAX_PATH_LENGTH];
    strcpy_s(safe_path, MAX_PATH_LENGTH, src_path);

    // 将冒号替换为下划线（处理盘符）
    for (char *p = safe_path; *p; p++) {
      if (*p == ':') {
        *p = '_';
      }
      // 将路径分隔符统一为反斜杠
      if (*p == '/') {
        *p = '\\';
      }
    }

    // 如果路径以反斜杠开头，去掉它
    if (safe_path[0] == '\\') {
      snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s", backup_dir,
               safe_path + 1);
    } else {
      snprintf(backup_path, MAX_PATH_LENGTH, "%s\\%s", backup_dir, safe_path);
    }
  }

  // 规范化备份路径
  normalize_path(backup_path);

  // 转换备份路径为宽字符
  wchar_t *wbackup_path = char_to_wchar(backup_path);
  if (!wbackup_path) {
    printf("[错误] 无法转换备份路径编码: %s\n", backup_path);
    free(wsrc_path);
    return 0;
  }

  // 创建目标目录
  wchar_t backup_dir_wide[MAX_PATH_LENGTH];
  wcscpy_s(backup_dir_wide, MAX_PATH_LENGTH, wbackup_path);

  // 找到最后一个反斜杠
  wchar_t *last_slash = wcsrchr(backup_dir_wide, L'\\');
  if (last_slash) {
    *last_slash = L'\0';

    // 递归创建目录
    if (!create_directory_recursive(backup_dir_wide)) {
      printf("[错误] 无法创建备份目录结构\n");
      free(wsrc_path);
      free(wbackup_path);
      return 0;
    }
  }

  // 复制文件
  printf("    备份到: %s\n", backup_path);

  if (CopyFileW(wsrc_path, wbackup_path, FALSE)) {
    printf("    [成功] 文件备份完成\n");
    free(wsrc_path);
    free(wbackup_path);
    return 1;
  } else {
    DWORD error = GetLastError();
    printf("    [失败] 文件备份失败 (错误: %lu)\n", error);
    free(wsrc_path);
    free(wbackup_path);
    return 0;
  }
}

// 修改：改进的迭代收集函数，避免目录重复添加，正确处理大文件
void collect_items_iterative(const wchar_t *wpath, FileItem *items,
                             int *item_count, long long *total_scanned_size,
                             long long *skipped_files_size,
                             GroupResult *result) {
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  // 新增：用于跟踪已处理的目录路径，避免重复添加
  char **processed_dirs = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  int processed_count = 0;

  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  while (queue_front != queue_rear && *item_count < MAX_ITEMS) {
    DirectoryEntry current = queue[queue_front];
    queue_front = (queue_front + 1) % MAX_DIRECTORY_QUEUE;

    if (current.depth > 50) {
      continue;
    }

    wchar_t search_path[MAX_PATH_LENGTH];
    if (!safe_path_join(search_path, MAX_PATH_LENGTH, current.path, L"*")) {
      continue;
    }

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
      continue;
    }

    do {
      if (wcscmp(find_data.cFileName, L".") == 0 ||
          wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }

      wchar_t full_path[MAX_PATH_LENGTH];
      if (!safe_path_join(full_path, MAX_PATH_LENGTH, current.path,
                          find_data.cFileName)) {
        continue;
      }

      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        // 修改：不再在递归扫描中添加目录，只在初始处理时添加
        // 目录项由 process_input_path 函数统一添加，避免重复

        // 只将子目录加入队列进行递归扫描，不添加到items列表
        if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
          wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
          queue[queue_rear].depth = current.depth + 1;
          queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;
        }
      } else {
        ULARGE_INTEGER file_size;
        file_size.LowPart = find_data.nFileSizeLow;
        file_size.HighPart = find_data.nFileSizeHigh;
        *total_scanned_size += file_size.QuadPart;

        if (file_size.QuadPart > MAX_FILE_SIZE) {
          char *char_path = wchar_to_char(full_path);
          if (char_path) {
            printf("[跳过] 大文件: %s", char_path);
            char size_str[32];
            format_size(file_size.QuadPart, size_str, sizeof(size_str));
            printf(" (%s)\n", size_str);
            free(char_path);
          }
          *skipped_files_size += file_size.QuadPart;

          // 记录跳过大文件信息
          if (result->skipped_count < MAX_ITEMS) {
            if (result->skipped_count >= result->skipped_capacity) {
              int new_capacity = result->skipped_capacity == 0
                                     ? 10
                                     : result->skipped_capacity * 2;
              SkippedFile *new_skipped = (SkippedFile *)safe_realloc(
                  result->skipped_files, sizeof(SkippedFile) * new_capacity);
              result->skipped_files = new_skipped;
              result->skipped_capacity = new_capacity;
            }

            char *char_path = wchar_to_char(full_path);
            if (char_path) {
              normalize_path(char_path);
              if (strlen(char_path) < MAX_PATH_LENGTH - 1) {
                strcpy_s(result->skipped_files[result->skipped_count].path,
                         MAX_PATH_LENGTH, char_path);
                result->skipped_files[result->skipped_count].size =
                    file_size.QuadPart;
                result->skipped_count++;
              }
              free(char_path);
            }
          }
        } else {
          char *char_path = wchar_to_char(full_path);
          if (char_path) {
            if (strlen(char_path) < MAX_PATH_LENGTH - 1) {
              normalize_path(char_path);
              strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
              items[*item_count].size = file_size.QuadPart;
              items[*item_count].type = TYPE_FILE;
              (*item_count)++;
            }
            free(char_path);
          }
        }
      }
    } while (FindNextFileW(hFind, &find_data) && *item_count < MAX_ITEMS);

    FindClose(hFind);
  }

  // 释放已处理目录数组
  for (int i = 0; i < processed_count; i++) {
    free(processed_dirs[i]);
  }
  free(processed_dirs);
  free(queue);
}

// 修改：改进的路径处理函数，检查文件夹是否包含大文件
void process_input_path(const char *path, FileItem *items, int *item_count,
                        long long *total_input_size,
                        long long *total_scanned_size,
                        long long *skipped_files_size, GroupResult *result) {
  // 复制路径以便修改
  char normalized_path[MAX_PATH_LENGTH];
  strcpy_s(normalized_path, MAX_PATH_LENGTH, path);

  // 规范化路径（统一使用反斜杠）
  normalize_path(normalized_path);

  printf("[处理] 正在处理路径: %s\n", normalized_path);

  // 检查是否是目录路径（以反斜杠结尾）
  int is_directory_path = 0;
  size_t path_len = strlen(normalized_path);
  if (path_len > 0 && normalized_path[path_len - 1] == '\\') {
    is_directory_path = 1;
    printf("  [信息] 识别为目录路径\n");
  }

  wchar_t *wpath = char_to_wchar(normalized_path);
  if (!wpath) {
    printf("[警告] 无法转换路径编码: %s\n", normalized_path);
    return;
  }

  DWORD attr = GetFileAttributesW(wpath);

  if (attr == INVALID_FILE_ATTRIBUTES) {
    // 路径无法访问，但我们仍然根据路径特征判断类型
    if (is_directory_path) {
      // 目录路径但无法访问，可能是新目录或权限问题
      printf("  [警告] 无法访问目录，但根据路径特征识别为目录: %s\n",
             normalized_path);

      if (*item_count < MAX_ITEMS) {
        // 确保目录路径格式正确
        char dir_path[MAX_PATH_LENGTH];
        strcpy_s(dir_path, MAX_PATH_LENGTH, normalized_path);
        size_t dir_len = strlen(dir_path);
        if (dir_len > 0 && dir_path[dir_len - 1] != '\\') {
          strcat_s(dir_path, MAX_PATH_LENGTH, "\\");
        }

        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, dir_path);
        items[*item_count].size = 0; // 无法计算大小，设为0
        items[*item_count].type = TYPE_DIRECTORY;
        (*item_count)++;
        printf("  [信息] 已添加目录到处理列表（大小未知）\n");
      }
    } else {
      // 文件路径但无法访问，可能是新文件
      printf("  [信息] 路径可能为新文件: %s\n", normalized_path);

      if (*item_count < MAX_ITEMS) {
        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, normalized_path);
        items[*item_count].size = 0; // 新文件，大小为0
        items[*item_count].type = TYPE_FILE;
        (*item_count)++;
        printf("  [信息] 已添加文件到处理列表（新文件）\n");
      }
    }

    free(wpath);
    return;
  }

  // 路径可访问的情况
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("  [扫描] 文件夹: %s\n", normalized_path);
    long long dir_size =
        calculate_directory_size_iterative(wpath, total_scanned_size);
    *total_input_size += dir_size;

    char size_str[32];
    format_size(dir_size, size_str, sizeof(size_str));
    printf("       文件夹大小: %s\n", size_str);

    // 修改：检查文件夹是否包含大文件
    printf("       检查文件夹是否包含大文件...\n");
    long long temp_scanned_size = 0;
    int has_large_files =
        directory_contains_large_files(wpath, &temp_scanned_size);

    // 只有在文件夹大小合适且不包含大文件时才直接添加
    if (dir_size <= MAX_GROUP_SIZE && !has_large_files) {
      printf("       文件夹大小合适且不包含大文件，直接添加...\n");

      if (*item_count < MAX_ITEMS) {
        // 确保目录路径格式正确
        char dir_path[MAX_PATH_LENGTH];
        strcpy_s(dir_path, MAX_PATH_LENGTH, normalized_path);
        size_t dir_len = strlen(dir_path);
        if (dir_len > 0 && dir_path[dir_len - 1] != '\\') {
          strcat_s(dir_path, MAX_PATH_LENGTH, "\\");
        }

        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, dir_path);
        items[*item_count].size = dir_size;
        items[*item_count].type = TYPE_DIRECTORY;
        (*item_count)++;
        printf("       已添加目录: %s\n", dir_path);
      }
    } else {
      if (dir_size > MAX_GROUP_SIZE) {
        printf("       文件夹太大 (%s > %lld MB)，递归处理子项...\n", size_str,
               MAX_GROUP_SIZE / (1024 * 1024));
      }
      if (has_large_files) {
        printf("       文件夹包含大文件，递归处理子项...\n");
      }
    }

    // 无论文件夹大小如何，都递归扫描内容
    // 但只在文件夹大小超过限制或包含大文件时才在递归扫描中添加文件
    collect_items_iterative(wpath, items, item_count, total_scanned_size,
                            skipped_files_size, result);
  } else {
    printf("  [扫描] 文件: %s\n", normalized_path);
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
      DWORD sizeLow, sizeHigh;
      sizeLow = GetFileSize(hFile, &sizeHigh);
      CloseHandle(hFile);

      ULARGE_INTEGER file_size;
      file_size.LowPart = sizeLow;
      file_size.HighPart = sizeHigh;

      *total_input_size += file_size.QuadPart;
      *total_scanned_size += file_size.QuadPart;

      if (file_size.QuadPart > MAX_FILE_SIZE) {
        printf("       跳过] 大文件: %s", normalized_path);
        char size_str[32];
        format_size(file_size.QuadPart, size_str, sizeof(size_str));
        printf(" (%s)\n", size_str);
        *skipped_files_size += file_size.QuadPart;

        // 记录跳过大文件信息
        if (result->skipped_count < MAX_ITEMS) {
          if (result->skipped_count >= result->skipped_capacity) {
            int new_capacity = result->skipped_capacity == 0
                                   ? 10
                                   : result->skipped_capacity * 2;
            SkippedFile *new_skipped = (SkippedFile *)safe_realloc(
                result->skipped_files, sizeof(SkippedFile) * new_capacity);
            result->skipped_files = new_skipped;
            result->skipped_capacity = new_capacity;
          }

          strcpy_s(result->skipped_files[result->skipped_count].path,
                   MAX_PATH_LENGTH, normalized_path);
          result->skipped_files[result->skipped_count].size =
              file_size.QuadPart;
          result->skipped_count++;
        }
      } else {
        if (*item_count < MAX_ITEMS) {
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, normalized_path);
          items[*item_count].size = file_size.QuadPart;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;
        }
      }
    } else {
      printf("  [警告] 无法打开文件 %s (错误: %lu)\n", normalized_path,
             GetLastError());
      // 即使无法打开，也尝试添加到列表（大小为0）
      if (*item_count < MAX_ITEMS) {
        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, normalized_path);
        items[*item_count].size = 0;
        items[*item_count].type = TYPE_FILE;
        (*item_count)++;
      }
    }
  }

  free(wpath);
}

// 修改：比较函数，确保文件夹在前，按大小降序
int compare_items(const void *a, const void *b) {
  const FileItem *item1 = (const FileItem *)a;
  const FileItem *item2 = (const FileItem *)b;

  // 文件夹优先
  if (item1->type != item2->type) {
    return item2->type -
           item1->type; // TYPE_DIRECTORY(1) - TYPE_FILE(0) = 1，文件夹在前
  }

  // 按大小降序排列
  if (item1->size > item2->size)
    return -1;
  if (item1->size < item2->size)
    return 1;

  return strcmp(item1->path, item2->path);
}

// 新增：检查路径包含关系的函数
int is_path_contained(const char *child_path, const char *parent_path) {
  if (!child_path || !parent_path)
    return 0;

  char normalized_child[MAX_PATH_LENGTH];
  char normalized_parent[MAX_PATH_LENGTH];

  strcpy_s(normalized_child, MAX_PATH_LENGTH, child_path);
  strcpy_s(normalized_parent, MAX_PATH_LENGTH, parent_path);

  // 规范化路径
  normalize_path(normalized_child);
  normalize_path(normalized_parent);

  // 确保父路径以反斜杠结尾以便精确匹配
  size_t parent_len = strlen(normalized_parent);
  if (parent_len > 0 && normalized_parent[parent_len - 1] != '\\') {
    if (parent_len < MAX_PATH_LENGTH - 1) {
      strcat_s(normalized_parent, MAX_PATH_LENGTH, "\\");
    }
  }

  // 检查child_path是否以parent_path开头
  return (strstr(normalized_child, normalized_parent) == normalized_child);
}

// 新增：检查项是否被任何已分组文件夹包含
int is_item_contained_by_any_group(const FileItem *item,
                                   const FileGroup *groups, int group_count) {
  if (item->type == TYPE_FILE) {
    for (int i = 0; i < group_count; i++) {
      for (int j = 0; j < groups[i].count; j++) {
        if (groups[i].items[j].type == TYPE_DIRECTORY) {
          if (is_path_contained(item->path, groups[i].items[j].path)) {
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

// 修改：改进的分组算法，避免文件夹和文件的重复分组
GroupResult group_files(FileItem *items, int item_count) {
  GroupResult result = {0};

  int initial_group_count = (item_count > 100) ? (item_count / 100) + 1 : 10;
  result.groups =
      (FileGroup *)safe_malloc(sizeof(FileGroup) * initial_group_count);
  result.group_count = 0;
  result.groups_capacity = initial_group_count;

  printf("[处理] 正在排序 %d 个项...\n", item_count);
  qsort(items, item_count, sizeof(FileItem), compare_items);

  // 初始化分组数组
  for (int i = 0; i < initial_group_count; i++) {
    result.groups[i].items = (FileItem *)safe_malloc(sizeof(FileItem) * 10);
    result.groups[i].count = 0;
    result.groups[i].capacity = 10;
    result.groups[i].total_size = 0;
  }

  printf("[处理] 开始分组处理...\n");

  // 第一遍：先添加所有合适的文件夹
  for (int i = 0; i < item_count; i++) {
    if (items[i].type == TYPE_DIRECTORY && items[i].size <= MAX_GROUP_SIZE) {
      int best_group = -1;
      long long best_remaining = MAX_GROUP_SIZE;

      for (int j = 0; j < result.group_count; j++) {
        long long remaining = MAX_GROUP_SIZE - result.groups[j].total_size;
        if (remaining >= items[i].size && remaining < best_remaining) {
          best_remaining = remaining;
          best_group = j;
        }
      }

      if (best_group != -1) {
        FileGroup *group = &result.groups[best_group];

        if (group->count >= group->capacity) {
          int new_capacity = group->capacity * 2;
          FileItem *new_items = (FileItem *)safe_realloc(
              group->items, sizeof(FileItem) * new_capacity);
          group->items = new_items;
          group->capacity = new_capacity;
        }

        group->items[group->count++] = items[i];
        group->total_size += items[i].size;

        printf("  添加文件夹到分组 %d: %s (%lld bytes)\n", best_group + 1,
               items[i].path, items[i].size);
      } else {
        if (result.group_count >= result.groups_capacity) {
          int new_capacity = result.groups_capacity * 2;
          FileGroup *new_groups = (FileGroup *)safe_realloc(
              result.groups, sizeof(FileGroup) * new_capacity);

          for (int k = result.group_count; k < new_capacity; k++) {
            new_groups[k].items =
                (FileItem *)safe_malloc(sizeof(FileItem) * 10);
            new_groups[k].count = 0;
            new_groups[k].capacity = 10;
            new_groups[k].total_size = 0;
          }

          result.groups = new_groups;
          result.groups_capacity = new_capacity;
        }

        FileGroup *new_group = &result.groups[result.group_count];
        if (new_group->count >= new_group->capacity) {
          int new_capacity = new_group->capacity * 2;
          FileItem *new_items = (FileItem *)safe_realloc(
              new_group->items, sizeof(FileItem) * new_capacity);
          new_group->items = new_items;
          new_group->capacity = new_capacity;
        }

        new_group->items[new_group->count++] = items[i];
        new_group->total_size += items[i].size;
        result.group_count++;

        printf("  创建新分组 %d 并添加文件夹: %s (%lld bytes)\n",
               result.group_count, items[i].path, items[i].size);
      }
    }
  }

  // 第二遍：添加未被包含的文件
  for (int i = 0; i < item_count; i++) {
    if (i % 1000 == 0 || i == item_count - 1) {
      draw_progress_bar(i + 1, item_count, "分组进度");
    }

    // 跳过文件夹（已经在第一遍处理）
    if (items[i].type == TYPE_DIRECTORY) {
      continue;
    }

    // 检查文件是否已经被某个分组的文件夹包含
    if (is_item_contained_by_any_group(&items[i], result.groups,
                                       result.group_count)) {
      printf("  跳过已被包含的文件: %s\n", items[i].path);
      continue;
    }

    int best_group = -1;
    long long best_remaining = MAX_GROUP_SIZE;

    for (int j = 0; j < result.group_count; j++) {
      long long remaining = MAX_GROUP_SIZE - result.groups[j].total_size;
      if (remaining >= items[i].size && remaining < best_remaining) {
        best_remaining = remaining;
        best_group = j;
      }
    }

    if (best_group != -1) {
      FileGroup *group = &result.groups[best_group];

      if (group->count >= group->capacity) {
        int new_capacity = group->capacity * 2;
        FileItem *new_items = (FileItem *)safe_realloc(
            group->items, sizeof(FileItem) * new_capacity);
        group->items = new_items;
        group->capacity = new_capacity;
      }

      group->items[group->count++] = items[i];
      group->total_size += items[i].size;
    } else {
      if (result.group_count >= result.groups_capacity) {
        int new_capacity = result.groups_capacity * 2;
        FileGroup *new_groups = (FileGroup *)safe_realloc(
            result.groups, sizeof(FileGroup) * new_capacity);

        for (int k = result.group_count; k < new_capacity; k++) {
          new_groups[k].items = (FileItem *)safe_malloc(sizeof(FileItem) * 10);
          new_groups[k].count = 0;
          new_groups[k].capacity = 10;
          new_groups[k].total_size = 0;
        }

        result.groups = new_groups;
        result.groups_capacity = new_capacity;
      }

      FileGroup *new_group = &result.groups[result.group_count];
      if (new_group->count >= new_group->capacity) {
        int new_capacity = new_group->capacity * 2;
        FileItem *new_items = (FileItem *)safe_realloc(
            new_group->items, sizeof(FileItem) * new_capacity);
        new_group->items = new_items;
        new_group->capacity = new_capacity;
      }

      new_group->items[new_group->count++] = items[i];
      new_group->total_size += items[i].size;
      result.group_count++;
    }
  }

  printf("\n[完成] 分组完成，共 %d 个分组\n", result.group_count);

  // 压缩分组数组
  if (result.group_count < result.groups_capacity) {
    result.groups = (FileGroup *)safe_realloc(
        result.groups, sizeof(FileGroup) * result.group_count);
    result.groups_capacity = result.group_count;
  }

  // 打印分组统计
  printf("[统计] 分组详情:\n");
  for (int i = 0; i < result.group_count; i++) {
    int file_count = 0, dir_count = 0;
    for (int j = 0; j < result.groups[i].count; j++) {
      if (result.groups[i].items[j].type == TYPE_FILE) {
        file_count++;
      } else {
        dir_count++;
      }
    }
    char size_str[32];
    format_size(result.groups[i].total_size, size_str, sizeof(size_str));
    printf("  分组 %d: %s (%d文件, %d文件夹)\n", i + 1, size_str, file_count,
           dir_count);
  }

  return result;
}

// 修改：在print_groups函数中调用详细分组信息
void print_groups(const GroupResult *result) {
  printf("\n========================================\n");
  printf("              分组结果\n");
  printf("========================================\n\n");

  int max_groups_to_show = 10;
  int groups_to_show = result->group_count < max_groups_to_show
                           ? result->group_count
                           : max_groups_to_show;

  for (int i = 0; i < groups_to_show; i++) {
    print_detailed_group_info(&result->groups[i], i);
  }

  if (result->group_count > max_groups_to_show) {
    printf("[信息] 还有 %d 个分组未显示\n\n",
           result->group_count - max_groups_to_show);
  }
}

// 新增：清空目录内容的函数
int clear_directory(const wchar_t *wdir_path) {
  printf("    正在清空目录内容...\n");

  wchar_t search_path[MAX_PATH_LENGTH];
  if (!safe_path_join(search_path, MAX_PATH_LENGTH, wdir_path, L"*")) {
    printf("    [错误] 无法构造搜索路径\n");
    return 0;
  }

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    // 目录可能为空，这不算错误
    return 1;
  }

  int success = 1;

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t full_path[MAX_PATH_LENGTH];
    if (!safe_path_join(full_path, MAX_PATH_LENGTH, wdir_path,
                        find_data.cFileName)) {
      success = 0;
      continue;
    }

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // 递归删除子目录
      if (!clear_directory(full_path)) {
        success = 0;
      }

      // 删除空目录
      if (!RemoveDirectoryW(full_path)) {
        DWORD error = GetLastError();
        printf("    [警告] 无法删除目录 %ls (错误: %lu)\n", find_data.cFileName,
               error);
        success = 0;
      }
    } else {
      // 删除文件
      if (!DeleteFileW(full_path)) {
        DWORD error = GetLastError();
        printf("    [警告] 无法删除文件 %ls (错误: %lu)\n", find_data.cFileName,
               error);
        success = 0;
      }
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);
  return success;
}

// 修改：改进的大文件拆分函数，增强错误处理
int split_large_file(const char *file_path, const char *split_dir,
                     long long file_size) {
  printf("    正在处理大文件拆分...\n");

  // 首先检查拆分是否已经完整（双重检查）
  if (is_split_complete(file_path, split_dir, file_size)) {
    printf("    [信息] 拆分目录已存在且完整，跳过拆分步骤\n");
    return 1;
  }

  // 转换路径为宽字符
  wchar_t *wfile_path = char_to_wchar(file_path);
  wchar_t *wsplit_dir = char_to_wchar(split_dir);

  if (!wfile_path || !wsplit_dir) {
    printf("    [错误] 无法转换路径编码\n");
    if (wfile_path)
      free(wfile_path);
    if (wsplit_dir)
      free(wsplit_dir);
    return 0;
  }

  // 新增：验证源文件是否存在且可访问
  DWORD source_attr = GetFileAttributesW(wfile_path);
  if (source_attr == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    printf("    [错误] 源文件无法访问: %s (错误: %lu)\n", file_path, error);
    free(wfile_path);
    free(wsplit_dir);
    return 0;
  }

  if (source_attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("    [错误] 源路径是目录而不是文件: %s\n", file_path);
    free(wfile_path);
    free(wsplit_dir);
    return 0;
  }

  // 检查拆分目录是否存在，如果存在且不完整则清空
  DWORD dir_attr = GetFileAttributesW(wsplit_dir);
  if (dir_attr != INVALID_FILE_ATTRIBUTES &&
      (dir_attr & FILE_ATTRIBUTE_DIRECTORY)) {
    printf("    拆分目录已存在但不完整，正在清空...\n");
    if (!clear_directory(wsplit_dir)) {
      printf("    [警告] 清空拆分目录失败，继续尝试拆分...\n");
    } else {
      printf("    [成功] 拆分目录已清空\n");
    }
  } else {
    // 创建拆分目录
    if (!create_directory_recursive(wsplit_dir)) {
      printf("    [错误] 无法创建拆分目录\n");
      free(wfile_path);
      free(wsplit_dir);
      return 0;
    }
  }

  // 打开源文件
  HANDLE hSource = CreateFileW(wfile_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hSource == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    printf("    [错误] 无法打开源文件 (错误: %lu)\n", error);
    free(wfile_path);
    free(wsplit_dir);
    return 0;
  }

  // 计算需要拆分成多少部分
  const long long PART_SIZE = 50 * 1024 * 1024; // 50MB
  int total_parts = (int)((file_size + PART_SIZE - 1) / PART_SIZE);

  printf("    文件大小: %lld bytes, 需要拆分成 %d 个部分\n", file_size,
         total_parts);

  int success_parts = 0;
  BYTE *buffer = (BYTE *)safe_malloc((size_t)PART_SIZE);

  for (int part_num = 1; part_num <= total_parts; part_num++) {
    // 计算当前部分的大小
    long long part_size = PART_SIZE;
    if (part_num == total_parts) {
      part_size = file_size - (PART_SIZE * (total_parts - 1));
    }

    // 生成部分文件名
    char part_filename[MAX_PATH_LENGTH];
    const char *filename = strrchr(file_path, '\\');
    if (!filename) {
      filename = strrchr(file_path, '/');
    }
    if (filename) {
      filename++;
    } else {
      filename = file_path;
    }

    // 分离文件名和扩展名
    char file_base[MAX_PATH_LENGTH] = {0};
    char file_ext[MAX_PATH_LENGTH] = {0};
    char *dot_pos = strrchr(filename, '.');

    if (dot_pos && dot_pos != filename) {
      size_t base_len = dot_pos - filename;
      strncpy_s(file_base, MAX_PATH_LENGTH, filename, base_len);
      file_base[base_len] = '\0';
      strcpy_s(file_ext, MAX_PATH_LENGTH, dot_pos);
    } else {
      strcpy_s(file_base, MAX_PATH_LENGTH, filename);
      file_ext[0] = '\0';
    }

    // 生成部分文件路径
    snprintf(part_filename, MAX_PATH_LENGTH, "%s\\%s-part%04d%s", split_dir,
             file_base, part_num, file_ext);

    // 转换部分文件路径为宽字符
    wchar_t *wpart_filename = char_to_wchar(part_filename);
    if (!wpart_filename) {
      printf("    [错误] 无法转换部分文件路径编码: %s\n", part_filename);
      continue;
    }

    // 创建目标文件
    HANDLE hTarget = CreateFileW(wpart_filename, GENERIC_WRITE, 0, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTarget == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      printf("    [错误] 无法创建部分文件 %s (错误: %lu)\n", part_filename,
             error);
      free(wpart_filename);
      continue;
    }

    // 读取和写入数据
    DWORD bytes_read, bytes_written;
    long long remaining = part_size;
    int write_success = 1;

    while (remaining > 0) {
      DWORD to_read = (DWORD)((remaining > PART_SIZE) ? PART_SIZE : remaining);

      if (!ReadFile(hSource, buffer, to_read, &bytes_read, NULL) ||
          bytes_read == 0) {
        printf("    [错误] 读取源文件失败\n");
        write_success = 0;
        break;
      }

      if (!WriteFile(hTarget, buffer, bytes_read, &bytes_written, NULL) ||
          bytes_written != bytes_read) {
        printf("    [错误] 写入部分文件失败\n");
        write_success = 0;
        break;
      }

      remaining -= bytes_read;
    }

    CloseHandle(hTarget);
    free(wpart_filename);

    if (write_success) {
      success_parts++;
      printf("    [成功] 创建部分文件: %s (%lld bytes)\n", part_filename,
             part_size);
    } else {
      printf("    [失败] 创建部分文件失败: %s\n", part_filename);
    }
  }

  free(buffer);
  CloseHandle(hSource);

  // 最终验证拆分是否完整
  if (success_parts == total_parts) {
    printf("    [成功] 文件拆分完成，共 %d 个部分\n", total_parts);

    // 最终完整性检查
    if (is_split_complete(file_path, split_dir, file_size)) {
      printf("    [验证] 拆分完整性验证通过\n");

      // 新增：拆分成功后删除原文件（但不在这个函数中删除，由调用者控制）
      printf("    [信息] 拆分完成，原文件将在备份后被删除\n");

      free(wfile_path);
      free(wsplit_dir);
      return 1;
    } else {
      printf("    [警告] 拆分完整性验证失败\n");
      free(wfile_path);
      free(wsplit_dir);
      return 0;
    }
  } else {
    printf("    [警告] 文件拆分部分成功: %d/%d 个部分\n", success_parts,
           total_parts);
    free(wfile_path);
    free(wsplit_dir);
    return 0;
  }
}

// 修改：改进拆分完整性检查，使用宽字符API
int is_split_complete(const char *file_path, const char *split_dir,
                      long long file_size) {
  // 转换路径为宽字符
  wchar_t *wsplit_dir = char_to_wchar(split_dir);
  if (!wsplit_dir)
    return 0;

  // 检查拆分目录是否存在
  DWORD attr = GetFileAttributesW(wsplit_dir);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    printf("    拆分目录不存在: %s\n", split_dir);
    free(wsplit_dir);
    return 0; // 目录不存在，需要拆分
  }

  // 计算拆分目录中所有文件的总大小
  long long split_total_size = 0;
  int part_count = 0;

  wchar_t search_path[MAX_PATH_LENGTH];
  if (!safe_path_join(search_path, MAX_PATH_LENGTH, wsplit_dir, L"*")) {
    free(wsplit_dir);
    return 0;
  }

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(find_data.cFileName, L".") == 0 ||
          wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }

      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // 是文件，累加大小
        ULARGE_INTEGER part_size;
        part_size.LowPart = find_data.nFileSizeLow;
        part_size.HighPart = find_data.nFileSizeHigh;
        split_total_size += part_size.QuadPart;
        part_count++;

        // 调试信息：显示找到的拆分文件
        char *part_filename = wchar_to_char(find_data.cFileName);
        if (part_filename) {
          printf("      找到拆分文件: %s (%llu bytes)\n", part_filename,
                 part_size.QuadPart);
          free(part_filename);
        }
      }
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);
  }

  free(wsplit_dir);

  // 检查拆分是否完整
  if (part_count == 0) {
    printf("    拆分目录为空: %s\n", split_dir);
    return 0; // 目录为空，需要拆分
  }

  long long size_difference = llabs(split_total_size - file_size);
  double difference_ratio = (double)size_difference / file_size;

  printf("    拆分检查: 原文件 %lld bytes, 拆分文件 %lld bytes (共%d个文件), "
         "差异 %.4f%%\n",
         file_size, split_total_size, part_count, difference_ratio * 100);

  // 允许微小的差异（可能是由于文件系统或计算误差）
  if (size_difference > 1024) { // 允许1KB以内的差异
    printf("    文件大小不匹配，需要重新拆分\n");
    return 0;
  }

  printf("    拆分目录完整，跳过拆分步骤\n");
  return 1;
}

// 新增：文件状态验证函数，用于调试
void debug_file_status(const char *file_path) {
  printf("    [调试] 检查文件状态: %s\n", file_path);

  // 使用ANSI API检查
  DWORD attr_a = GetFileAttributesA(file_path);
  if (attr_a == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    printf("      ANSI API: 无法访问 (错误: %lu)\n", error);
  } else {
    if (attr_a & FILE_ATTRIBUTE_DIRECTORY) {
      printf("      ANSI API: 目录\n");
    } else {
      printf("      ANSI API: 文件\n");
    }
  }

  // 使用Unicode API检查
  wchar_t *wpath = char_to_wchar(file_path);
  if (wpath) {
    DWORD attr_w = GetFileAttributesW(wpath);
    if (attr_w == INVALID_FILE_ATTRIBUTES) {
      DWORD error = GetLastError();
      printf("      Unicode API: 无法访问 (错误: %lu)\n", error);
    } else {
      if (attr_w & FILE_ATTRIBUTE_DIRECTORY) {
        printf("      Unicode API: 目录\n");
      } else {
        printf("      Unicode API: 文件\n");
      }
    }
    free(wpath);
  } else {
    printf("      Unicode API: 无法转换路径\n");
  }
}

// 修改：改进的删除原文件函数，解决路径识别问题
int delete_original_file(const char *file_path) {
  if (!file_path || strlen(file_path) == 0) {
    printf("    [错误] 文件路径为空\n");
    return 0;
  }

  // 新增：调试信息
  printf("    [调试] 准备删除文件: %s\n", file_path);
  debug_file_status(file_path);

  // 首先使用宽字符API检查文件状态，避免中文路径问题
  wchar_t *wfile_path = char_to_wchar(file_path);
  if (!wfile_path) {
    printf("    [错误] 无法转换文件路径编码: %s\n", file_path);
    return 0;
  }

  DWORD attr = GetFileAttributesW(wfile_path);

  if (attr == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    // 文件确实不存在
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      printf("    [信息] 文件已不存在: %s\n", file_path);
      free(wfile_path);
      return 1; // 文件不存在，视为删除成功
    } else {
      // 其他错误，可能是权限问题等
      printf("    [错误] 无法访问文件 %s (错误: %lu)\n", file_path, error);
      free(wfile_path);
      return 0;
    }
  }

  // 检查是否是目录
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("    [错误] 路径是目录而不是文件: %s\n", file_path);
    free(wfile_path);
    return 0;
  }

  // 尝试删除文件
  if (DeleteFileW(wfile_path)) {
    printf("    [成功] 文件删除成功: %s\n", file_path);
    free(wfile_path);
    return 1;
  } else {
    DWORD error = GetLastError();
    printf("    [错误] 无法删除文件 %s (错误: %lu)\n", file_path, error);

    // 根据错误代码提供更详细的错误信息
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
      printf("        文件不存在\n");
      break;
    case ERROR_ACCESS_DENIED:
      printf("        访问被拒绝，文件可能正在被使用或没有权限\n");
      break;
    case ERROR_SHARING_VIOLATION:
      printf("        文件正在被其他进程使用\n");
      break;
    default:
      printf("        未知错误\n");
      break;
    }

    free(wfile_path);
    return 0;
  }
}

// 修改：在函数末尾添加处理标记
AdditionalFiles print_skipped_files(GroupResult *result) {
  AdditionalFiles additional = {0};
  additional.gitignore_files = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  additional.split_files = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  additional.has_large_files = 0; // 初始化为没有处理大文件

  if (result->skipped_count == 0) {
    printf("\n[信息] 没有跳过的大文件\n");
    return additional;
  }

  printf("\n========================================\n");
  printf("          跳过的大文件列表\n");
  printf("========================================\n\n");

  printf("共跳过 %d 个大文件 (超过 %lld MB):\n\n", result->skipped_count,
         MAX_FILE_SIZE / (1024 * 1024));

  long long total_skipped_size = 0;
  int backup_success_count = 0;
  int gitignore_update_count = 0;
  int split_success_count = 0;
  int backup_attempt_count = 0;
  int gitignore_attempt_count = 0;
  int delete_success_count = 0; // 新增：成功删除的文件计数

  // 获取当前工作目录作为Git仓库路径
  char git_repo_path[MAX_PATH_LENGTH];
  if (!GetCurrentDirectoryA(MAX_PATH_LENGTH, git_repo_path)) {
    printf("[错误] 无法获取当前工作目录\n");
    return additional;
  }

  normalize_path(git_repo_path);
  printf("Git仓库路径: %s\n", git_repo_path);

  char backup_base_dir[MAX_PATH_LENGTH];
  snprintf(backup_base_dir, MAX_PATH_LENGTH, "%s-backup", git_repo_path);
  printf("备份基础路径: %s\n\n", backup_base_dir);

  for (int i = 0; i < result->skipped_count; i++) {
    total_skipped_size += result->skipped_files[i].size;

    char size_str[32];
    format_size(result->skipped_files[i].size, size_str, sizeof(size_str));

    printf("%3d. %s\n", i + 1, result->skipped_files[i].path);
    printf("     大小: %s\n", size_str);

    // 构建拆分目录路径
    char split_dir[MAX_PATH_LENGTH];
    snprintf(split_dir, MAX_PATH_LENGTH, "%s-split",
             result->skipped_files[i].path);

    // 检查是否需要拆分
    int needs_split =
        !is_split_complete(result->skipped_files[i].path, split_dir,
                           result->skipped_files[i].size);

    if (needs_split) {
      printf("    需要拆分大文件...\n");

      // 先进行拆分
      if (split_large_file(result->skipped_files[i].path, split_dir,
                           result->skipped_files[i].size)) {
        split_success_count++;
        printf("    [成功] 大文件拆分完成\n");

        // 拆分完成后进行备份（只有拆分成功才需要备份原文件）
        printf("    正在备份原文件...\n");
        backup_attempt_count++;

        if (copy_file_with_backup(result->skipped_files[i].path,
                                  backup_base_dir)) {
          backup_success_count++;
          printf("    [成功] 原文件备份完成\n");

          // 新增：备份成功后删除原文件
          printf("    正在删除原文件...\n");
          if (delete_original_file(result->skipped_files[i].path)) {
            delete_success_count++;
            printf("    [成功] 原文件已删除\n");
          } else {
            printf("    [警告] 原文件删除失败\n");
          }
        } else {
          printf("    [警告] 原文件备份失败，跳过删除步骤\n");
        }

        // 记录拆分目录路径，后续添加到分组
        if (additional.split_count < MAX_ITEMS) {
          additional.split_files[additional.split_count] =
              (char *)safe_malloc(strlen(split_dir) + 1);
          strcpy(additional.split_files[additional.split_count], split_dir);
          additional.split_count++;
        }

        // 更新.gitignore文件以忽略原文件
        gitignore_attempt_count++;
        if (update_gitignore_for_skipped_file(result->skipped_files[i].path)) {
          gitignore_update_count++;

          // 记录.gitignore文件路径，后续添加到分组
          char gitignore_path[MAX_PATH_LENGTH];
          char dir_path[MAX_PATH_LENGTH];
          strcpy_s(dir_path, MAX_PATH_LENGTH, result->skipped_files[i].path);

          char *last_slash = strrchr(dir_path, '\\');
          if (!last_slash) {
            last_slash = strrchr(dir_path, '/');
          }

          if (last_slash) {
            *last_slash = '\0';
            snprintf(gitignore_path, MAX_PATH_LENGTH, "%s\\.gitignore",
                     dir_path);

            if (additional.gitignore_count < MAX_ITEMS) {
              additional.gitignore_files[additional.gitignore_count] =
                  (char *)safe_malloc(strlen(gitignore_path) + 1);
              strcpy(additional.gitignore_files[additional.gitignore_count],
                     gitignore_path);
              additional.gitignore_count++;
            }
          }
        }
      } else {
        printf("    [失败] 大文件拆分失败，跳过备份和.gitignore更新\n");
      }
    } else {
      printf("    [信息] 大文件已拆分且完整，跳过拆分步骤\n");
      split_success_count++;

      // 即使已拆分，也要记录拆分目录
      if (additional.split_count < MAX_ITEMS) {
        additional.split_files[additional.split_count] =
            (char *)safe_malloc(strlen(split_dir) + 1);
        strcpy(additional.split_files[additional.split_count], split_dir);
        additional.split_count++;
      }

      // 检查是否需要备份和删除（如果原文件仍然存在）
      DWORD attr = GetFileAttributesA(result->skipped_files[i].path);
      if (attr != INVALID_FILE_ATTRIBUTES &&
          !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("    原文件仍然存在，进行备份...\n");
        backup_attempt_count++;

        if (copy_file_with_backup(result->skipped_files[i].path,
                                  backup_base_dir)) {
          backup_success_count++;
          printf("    [成功] 原文件备份完成\n");

          // 新增：备份成功后删除原文件
          printf("    正在删除原文件...\n");
          if (delete_original_file(result->skipped_files[i].path)) {
            delete_success_count++;
            printf("    [成功] 原文件已删除\n");
          } else {
            printf("    [警告] 原文件删除失败\n");
          }

          // 只有备份成功才需要更新.gitignore
          gitignore_attempt_count++;
          if (update_gitignore_for_skipped_file(
                  result->skipped_files[i].path)) {
            gitignore_update_count++;
          }
        } else {
          printf("    [警告] 原文件备份失败，跳过删除步骤\n");
        }
      } else {
        printf("    [信息] 原文件已不存在，无需备份和删除\n");
      }
    }

    // 每显示1个文件后空一行
    if (i != result->skipped_count - 1) {
      printf("\n");
    }
  }

  char total_skipped_str[32];
  format_size(total_skipped_size, total_skipped_str, sizeof(total_skipped_str));

  printf("\n跳过大文件总大小: %s\n", total_skipped_str);
  printf("需要拆分的文件: %d/%d 个\n", split_success_count,
         result->skipped_count);

  // 使用实际尝试备份的数量作为分母
  if (backup_attempt_count > 0) {
    printf("备份成功: %d/%d 个文件\n", backup_success_count,
           backup_attempt_count);
  } else {
    printf("备份成功: 0/0 个文件 (无需备份)\n");
  }

  // 新增：显示删除统计
  printf("删除成功: %d/%d 个文件\n", delete_success_count,
         backup_success_count);

  // 使用实际尝试更新.gitignore的数量作为分母
  if (gitignore_attempt_count > 0) {
    printf(".gitignore更新: %d/%d 个目录\n", gitignore_update_count,
           gitignore_attempt_count);
  } else {
    printf(".gitignore更新: 0/0 个目录 (无需更新)\n");
  }

  // 修复警告信息的逻辑
  if (backup_attempt_count > 0 && backup_success_count < backup_attempt_count) {
    printf("[警告] 部分文件备份失败，请检查权限和磁盘空间\n");
  } else if (backup_success_count > 0) {
    printf("[成功] 所有需要备份的大文件已备份到: %s-backup\n", git_repo_path);
  } else {
    printf("[信息] 没有需要备份的文件\n");
  }

  // 新增：删除结果提示
  if (delete_success_count > 0) {
    printf("[成功] %d 个原文件已删除\n", delete_success_count);
  }
  if (backup_success_count > 0 && delete_success_count < backup_success_count) {
    printf("[警告] 部分文件备份成功但删除失败\n");
  }

  if (split_success_count < result->skipped_count) {
    printf("[警告] 部分大文件拆分失败\n");
  } else {
    printf("[成功] 所有大文件拆分处理完成\n");
  }

  if (gitignore_attempt_count > 0 &&
      gitignore_update_count < gitignore_attempt_count) {
    printf("[警告] 部分.gitignore文件更新失败\n");
  } else if (gitignore_update_count > 0) {
    printf("[成功] 所有相关.gitignore文件已更新\n");
  } else {
    printf("[信息] 没有需要更新的.gitignore文件\n");
  }

  // 在处理循环结束后，设置标记
  if (split_success_count > 0 || backup_success_count > 0 ||
      gitignore_update_count > 0) {
    additional.has_large_files = 1;
    printf("\n[信息] 检测到大文件处理操作，程序将重启以重新扫描\n");
  }

  return additional;
}

// 释放AdditionalFiles内存
void free_additional_files(AdditionalFiles *additional) {
  if (additional) {
    for (int i = 0; i < additional->gitignore_count; i++) {
      free(additional->gitignore_files[i]);
    }
    free(additional->gitignore_files);

    for (int i = 0; i < additional->split_count; i++) {
      free(additional->split_files[i]);
    }
    free(additional->split_files);
  }
}

// 将额外文件添加到分组中
void add_additional_files_to_groups(GroupResult *result,
                                    AdditionalFiles *additional) {
  if (!additional ||
      (!additional->gitignore_count && !additional->split_count)) {
    return;
  }

  printf("\n[处理] 正在将额外文件添加到分组...\n");

  // 首先收集所有需要添加的文件信息
  FileItem *new_items = (FileItem *)safe_malloc(sizeof(FileItem) * MAX_ITEMS);
  int new_item_count = 0;

  // 添加.gitignore文件
  for (int i = 0; i < additional->gitignore_count && new_item_count < MAX_ITEMS;
       i++) {
    char *path = additional->gitignore_files[i];

    // 检查文件是否存在
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
      // 获取文件大小
      HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile != INVALID_HANDLE_VALUE) {
        DWORD sizeLow, sizeHigh;
        sizeLow = GetFileSize(hFile, &sizeHigh);
        CloseHandle(hFile);

        ULARGE_INTEGER file_size;
        file_size.LowPart = sizeLow;
        file_size.HighPart = sizeHigh;

        strcpy_s(new_items[new_item_count].path, MAX_PATH_LENGTH, path);
        new_items[new_item_count].size = file_size.QuadPart;
        new_items[new_item_count].type = TYPE_FILE;
        new_item_count++;

        printf("  添加.gitignore文件: %s (%lld bytes)\n", path,
               file_size.QuadPart);
      }
    }
  }

  // 添加拆分目录中的所有文件
  for (int i = 0; i < additional->split_count && new_item_count < MAX_ITEMS;
       i++) {
    char *split_dir = additional->split_files[i];

    // 转换路径为宽字符
    wchar_t *wsplit_dir = char_to_wchar(split_dir);
    if (wsplit_dir) {
      // 递归收集拆分目录中的所有文件
      collect_split_directory_files(wsplit_dir, new_items, &new_item_count);
      free(wsplit_dir);
    }
  }

  if (new_item_count > 0) {
    printf("  共找到 %d 个额外文件需要添加到分组\n", new_item_count);

    // 将新文件添加到现有分组中
    for (int i = 0; i < new_item_count; i++) {
      FileItem *item = &new_items[i];

      int best_group = -1;
      long long best_remaining = MAX_GROUP_SIZE;

      // 寻找最佳分组
      for (int j = 0; j < result->group_count; j++) {
        long long remaining = MAX_GROUP_SIZE - result->groups[j].total_size;
        if (remaining >= item->size && remaining < best_remaining) {
          best_remaining = remaining;
          best_group = j;
        }
      }

      if (best_group != -1) {
        // 添加到现有分组
        FileGroup *group = &result->groups[best_group];

        if (group->count >= group->capacity) {
          int new_capacity = group->capacity * 2;
          FileItem *new_items_array = (FileItem *)safe_realloc(
              group->items, sizeof(FileItem) * new_capacity);
          group->items = new_items_array;
          group->capacity = new_capacity;
        }

        group->items[group->count++] = *item;
        group->total_size += item->size;

        printf("    已添加 '%s' 到分组 %d\n", item->path, best_group + 1);
      } else {
        // 创建新分组
        if (result->group_count >= result->groups_capacity) {
          int new_capacity = result->groups_capacity * 2;
          FileGroup *new_groups = (FileGroup *)safe_realloc(
              result->groups, sizeof(FileGroup) * new_capacity);

          for (int k = result->group_count; k < new_capacity; k++) {
            new_groups[k].items =
                (FileItem *)safe_malloc(sizeof(FileItem) * 10);
            new_groups[k].count = 0;
            new_groups[k].capacity = 10;
            new_groups[k].total_size = 0;
          }

          result->groups = new_groups;
          result->groups_capacity = new_capacity;
        }

        FileGroup *new_group = &result->groups[result->group_count];
        if (new_group->count >= new_group->capacity) {
          int new_capacity = new_group->capacity * 2;
          FileItem *new_items_array = (FileItem *)safe_realloc(
              new_group->items, sizeof(FileItem) * new_capacity);
          new_group->items = new_items_array;
          new_group->capacity = new_capacity;
        }

        new_group->items[new_group->count++] = *item;
        new_group->total_size += item->size;
        result->group_count++;

        printf("    已创建新分组 %d 并添加 '%s'\n", result->group_count,
               item->path);
      }
    }
  } else {
    printf("  没有找到需要添加的额外文件\n");
  }

  free(new_items);
}

// 递归收集拆分目录中的文件
void collect_split_directory_files(const wchar_t *wdir_path, FileItem *items,
                                   int *item_count) {
  wchar_t search_path[MAX_PATH_LENGTH];
  if (!safe_path_join(search_path, MAX_PATH_LENGTH, wdir_path, L"*")) {
    return;
  }

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t full_path[MAX_PATH_LENGTH];
    if (!safe_path_join(full_path, MAX_PATH_LENGTH, wdir_path,
                        find_data.cFileName)) {
      continue;
    }

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // 递归处理子目录
      collect_split_directory_files(full_path, items, item_count);
    } else {
      // 添加文件
      if (*item_count < MAX_ITEMS) {
        char *char_path = wchar_to_char(full_path);
        if (char_path) {
          normalize_path(char_path);

          ULARGE_INTEGER file_size;
          file_size.LowPart = find_data.nFileSizeLow;
          file_size.HighPart = find_data.nFileSizeHigh;

          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
          items[*item_count].size = file_size.QuadPart;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;

          printf("    找到拆分文件: %s (%lld bytes)\n", char_path,
                 file_size.QuadPart);

          free(char_path);
        }
      }
    }
  } while (FindNextFileW(hFind, &find_data) && *item_count < MAX_ITEMS);

  FindClose(hFind);
}

// 修改：改进.gitignore更新逻辑，避免重复添加
int update_gitignore_for_skipped_file(const char *skipped_file_path) {
  // 提取目录路径
  char dir_path[MAX_PATH_LENGTH];
  strcpy_s(dir_path, MAX_PATH_LENGTH, skipped_file_path);

  // 找到最后一个路径分隔符
  char *last_slash = strrchr(dir_path, '\\');
  if (!last_slash) {
    last_slash = strrchr(dir_path, '/');
  }

  if (!last_slash) {
    printf("    [警告] 无法提取目录路径: %s\n", skipped_file_path);
    return 0;
  }

  // 截断得到目录路径
  *last_slash = '\0';

  // 构造.gitignore文件路径
  char gitignore_path[MAX_PATH_LENGTH];
  snprintf(gitignore_path, MAX_PATH_LENGTH, "%s\\.gitignore", dir_path);

  // 提取文件名（不含路径）
  const char *filename = last_slash + 1;

  // 生成备份文件的文件名（在原文件名基础上添加-merged后缀）
  char backup_filename[MAX_PATH_LENGTH];
  char file_base[MAX_PATH_LENGTH] = {0};
  char file_ext[MAX_PATH_LENGTH] = {0};

  // 分离文件名和扩展名
  char *dot_pos = strrchr(filename, '.');
  if (dot_pos && dot_pos != filename) { // 有扩展名且不是隐藏文件
    size_t base_len = dot_pos - filename;
    strncpy_s(file_base, MAX_PATH_LENGTH, filename, base_len);
    file_base[base_len] = '\0';
    strcpy_s(file_ext, MAX_PATH_LENGTH, dot_pos);
  } else {
    // 没有扩展名
    strcpy_s(file_base, MAX_PATH_LENGTH, filename);
    file_ext[0] = '\0';
  }

  // 生成备份文件名
  snprintf(backup_filename, MAX_PATH_LENGTH, "%s-merged%s", file_base,
           file_ext);

  // 转换路径为宽字符用于Windows API
  wchar_t *wgitignore_path = char_to_wchar(gitignore_path);
  if (!wgitignore_path) {
    printf("    [错误] 无法转换.gitignore路径编码: %s\n", gitignore_path);
    return 0;
  }

  // 检查.gitignore文件是否存在
  DWORD attr = GetFileAttributesW(wgitignore_path);
  int file_exists =
      (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));

  // 读取现有内容（如果文件存在）
  char *existing_content = NULL;
  size_t existing_size = 0;

  if (file_exists) {
    FILE *file = _wfopen(wgitignore_path, L"rb");
    if (file) {
      fseek(file, 0, SEEK_END);
      long file_size = ftell(file);
      fseek(file, 0, SEEK_SET);

      if (file_size > 0 && file_size < 10 * 1024 * 1024) { // 限制文件大小10MB
        existing_content = (char *)safe_malloc(file_size + 1);
        size_t read_size = fread(existing_content, 1, file_size, file);
        existing_content[read_size] = '\0';
        existing_size = read_size;
      }
      fclose(file);
    }
  }

  // 改进的检查逻辑：使用更精确的匹配
  int already_ignored = 0;
  if (existing_content) {
    // 逐行检查，避免部分匹配
    char *content_copy = (char *)safe_malloc(existing_size + 1);
    strcpy_s(content_copy, existing_size + 1, existing_content);

    char *line = strtok(content_copy, "\n");
    while (line != NULL) {
      // 跳过空行和注释行
      if (strlen(line) > 0 && line[0] != '#') {
        // 去除行首尾的空格
        char *trimmed_line = line;
        while (*trimmed_line == ' ' || *trimmed_line == '\t') {
          trimmed_line++;
        }

        char *end = trimmed_line + strlen(trimmed_line) - 1;
        while (end > trimmed_line &&
               (*end == ' ' || *end == '\t' || *end == '\r')) {
          *end = '\0';
          end--;
        }

        // 精确匹配
        if (strcmp(trimmed_line, backup_filename) == 0) {
          already_ignored = 1;
          printf("    [信息] 备份文件已在.gitignore中忽略: %s\n",
                 backup_filename);
          break;
        }
      }
      line = strtok(NULL, "\n");
    }

    free(content_copy);
  }

  // 如果尚未忽略，则添加到.gitignore
  if (!already_ignored) {
    FILE *file =
        _wfopen(wgitignore_path, L"ab"); // 追加模式，二进制写入避免编码问题
    if (!file) {
      // 尝试创建新文件
      file = _wfopen(wgitignore_path, L"wb");
    }

    if (file) {
      // 如果是新文件或空文件，添加UTF-8 BOM和适当的注释
      if (!file_exists || (existing_size == 0)) {
        // 写入UTF-8 BOM
        unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, 1, 3, file);

        // 写入文件头注释
        char header[] = "# 自动生成的.gitignore文件\n# 忽略备份的大文件\n\n";
        fwrite(header, 1, strlen(header), file);
      } else if (existing_size > 0) {
        // 确保以换行符结尾
        if (existing_content[existing_size - 1] != '\n') {
          fputc('\n', file);
        }
      }

      // 写入忽略条目
      fprintf(file, "%s\n", backup_filename);
      fclose(file);

      printf("    [成功] 已更新.gitignore: %s -> %s\n", gitignore_path,
             backup_filename);
    } else {
      printf("    [错误] 无法创建或打开.gitignore文件: %s\n", gitignore_path);
      free(wgitignore_path);
      if (existing_content)
        free(existing_content);
      return 0;
    }
  }

  free(wgitignore_path);
  if (existing_content)
    free(existing_content);

  return !already_ignored; // 返回1表示进行了更新，0表示已存在
}

// 打印统计信息
void print_statistics(const GroupResult *result, long long total_scanned_size,
                      long long skipped_files_size) {
  printf("========================================\n");
  printf("              统计信息\n");
  printf("========================================\n\n");

  long long total_grouped_size = 0;
  int total_grouped_files = 0;
  int total_grouped_dirs = 0;

  for (int i = 0; i < result->group_count; i++) {
    total_grouped_size += result->groups[i].total_size;
    for (int j = 0; j < result->groups[i].count; j++) {
      if (result->groups[i].items[j].type == TYPE_FILE) {
        total_grouped_files++;
      } else {
        total_grouped_dirs++;
      }
    }
  }

  double avg_group_size = result->group_count > 0
                              ? (double)total_grouped_size / result->group_count
                              : 0;

  // 计算各种比例
  double grouped_percent = total_scanned_size > 0 ? (double)total_grouped_size /
                                                        total_scanned_size * 100
                                                  : 0;
  double skipped_percent = total_scanned_size > 0 ? (double)skipped_files_size /
                                                        total_scanned_size * 100
                                                  : 0;

  printf("总体统计:\n");
  printf("  总分组数: %d 个\n", result->group_count);

  char avg_size_str[32];
  format_size((long long)avg_group_size, avg_size_str, sizeof(avg_size_str));
  printf("  平均每组大小: %s\n", avg_size_str);
  printf("\n");

  printf("项目统计:\n");
  printf("  分组总项数: %d 个\n", total_grouped_files + total_grouped_dirs);
  printf("  分组文件数: %d 个\n", total_grouped_files);
  printf("  分组文件夹数: %d 个\n", total_grouped_dirs);
  printf("\n");

  printf("大小统计:\n");
  char grouped_size_str[32], scanned_size_str[32], skipped_size_str[32];
  format_size(total_grouped_size, grouped_size_str, sizeof(grouped_size_str));
  format_size(total_scanned_size, scanned_size_str, sizeof(scanned_size_str));
  format_size(skipped_files_size, skipped_size_str, sizeof(skipped_size_str));

  printf("  扫描总大小: %s\n", scanned_size_str);
  printf("  分组总大小: %s (%.1f%%)\n", grouped_size_str, grouped_percent);
  printf("  跳过大文件: %s (%.1f%%)\n", skipped_size_str, skipped_percent);
}

// 验证结果
void validate_result(const GroupResult *result, long long input_total_size,
                     long long total_scanned_size,
                     long long skipped_files_size) {
  printf("\n========================================\n");
  printf("              验证结果\n");
  printf("========================================\n\n");

  long long total_grouped_size = 0;
  for (int i = 0; i < result->group_count; i++) {
    total_grouped_size += result->groups[i].total_size;
  }

  long long calculated_total = total_grouped_size + skipped_files_size;
  long long difference = input_total_size - calculated_total;
  double difference_percent =
      input_total_size > 0 ? (double)difference / input_total_size * 100 : 0;

  char input_str[32], grouped_str[32], skipped_str[32], scanned_str[32],
      diff_str[32];
  format_size(input_total_size, input_str, sizeof(input_str));
  format_size(total_grouped_size, grouped_str, sizeof(grouped_str));
  format_size(skipped_files_size, skipped_str, sizeof(skipped_str));
  format_size(total_scanned_size, scanned_str, sizeof(scanned_str));
  format_size(difference, diff_str, sizeof(diff_str));

  printf("    输入总大小: %s vs\n", input_str);
  printf("    分组总大小: %s\n", grouped_str);
  printf("\n");

  if (total_grouped_size == input_total_size) {
    printf("  [成功] 验证成功: 没有文件遗漏\n");
  } else if (total_grouped_size < input_total_size) {
    printf("  [警告] 可能有文件遗漏\n");
    printf("        相差: %s (%.2f%%)\n", diff_str, difference_percent);
    printf("        可能原因:\n");
    printf("          1. 隐藏文件或系统文件未被统计\n");
    printf("          2. 权限问题导致某些文件无法访问\n");
    printf("          3. 符号链接或特殊文件类型\n");
  } else {
    printf("  [错误] 数据不一致\n");
    printf("        计算值比输入值大: %s (%.2f%%)\n", diff_str,
           difference_percent);
  }
}

// 释放内存
void free_group_result(GroupResult *result) {
  if (result) {
    if (result->groups) {
      for (int i = 0; i < result->groups_capacity; i++) {
        if (result->groups[i].items) {
          free(result->groups[i].items);
          result->groups[i].items = NULL;
        }
      }
      free(result->groups);
      result->groups = NULL;
    }

    if (result->skipped_files) {
      free(result->skipped_files);
      result->skipped_files = NULL;
    }

    result->group_count = 0;
    result->groups_capacity = 0;
    result->skipped_count = 0;
    result->skipped_capacity = 0;
  }
}

// 处理输入路径并生成分组结果
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size) {
  FileItem *items = (FileItem *)safe_malloc(sizeof(FileItem) * MAX_ITEMS);
  int item_count = 0;
  long long total_input_size = 0;

  *total_scanned_size = 0;
  *skipped_files_size = 0;

  // 初始化GroupResult
  GroupResult result = {0};
  result.groups_capacity = 10;
  result.groups =
      (FileGroup *)safe_malloc(sizeof(FileGroup) * result.groups_capacity);
  result.skipped_capacity = 10;
  result.skipped_files =
      (SkippedFile *)safe_malloc(sizeof(SkippedFile) * result.skipped_capacity);
  result.skipped_count = 0;

  // 初始化groups数组
  for (int i = 0; i < result.groups_capacity; i++) {
    result.groups[i].items = (FileItem *)safe_malloc(sizeof(FileItem) * 10);
    result.groups[i].count = 0;
    result.groups[i].capacity = 10;
    result.groups[i].total_size = 0;
  }

  printf("[开始] 正在扫描文件和文件夹...\n\n");

  for (int i = 0; i < path_count; i++) {
    process_input_path(paths[i], items, &item_count, &total_input_size,
                       total_scanned_size, skipped_files_size, &result);
  }

  printf("\n[完成] 扫描完成:\n");
  printf("  共收集到 %d 个有效项\n", item_count);

  char input_size_str[32], scanned_size_str[32], skipped_size_str[32];
  format_size(total_input_size, input_size_str, sizeof(input_size_str));
  format_size(*total_scanned_size, scanned_size_str, sizeof(scanned_size_str));
  format_size(*skipped_files_size, skipped_size_str, sizeof(skipped_size_str));

  printf("  输入总大小: %s\n", input_size_str);
  printf("  扫描总大小: %s\n", scanned_size_str);
  printf("  跳过大文件: %s\n\n", skipped_size_str);

  printf("[处理] 正在进行分组...\n");

  // 调用分组函数
  GroupResult grouping_result = group_files(items, item_count);

  // 复制分组结果
  result.groups = grouping_result.groups;
  result.group_count = grouping_result.group_count;
  result.groups_capacity = grouping_result.groups_capacity;
  result.total_input_size = total_input_size;
  result.skipped_size = *skipped_files_size;

  free(items);
  return result;
}

// 修改：在执行git add命令时打印累积大小和分组总大小信息
void execute_git_commands(const GroupResult *result,
                          const char *commit_info_file) {
  printf("\n========================================\n");
  printf("              执行Git操作\n");
  printf("========================================\n\n");

  if (result->group_count == 0) {
    printf("[信息] 没有分组需要处理\n");
    return;
  }

  // 检查当前目录是否是Git仓库
  if (_wsystem(L"git status >nul 2>&1") != 0) {
    printf("[错误] 当前目录不是Git仓库或git命令不可用\n");
    return;
  }

  int total_commands = 0;
  int success_commands = 0;
  int total_paths_processed = 0;

  for (int group_idx = 0; group_idx < result->group_count; group_idx++) {
    const FileGroup *group = &result->groups[group_idx];

    printf("\n处理分组 %d/%d (包含 %d 个项):\n", group_idx + 1,
           result->group_count, group->count);

    // 计算当前分组的总大小
    long long current_group_total_size = 0;
    for (int i = 0; i < group->count; i++) {
      current_group_total_size += group->items[i].size;
    }

    char group_total_size_str[32];
    format_size(current_group_total_size, group_total_size_str,
                sizeof(group_total_size_str));
    printf("  分组总大小: %s\n", group_total_size_str);

    // 使用宽字符构建命令
    wchar_t command_buffer[32768] = L"git add";
    size_t buffer_len = wcslen(command_buffer);
    int current_command_path_count = 0;
    long long current_command_total_size = 0; // 当前命令添加的文件总大小
    long long cumulative_group_size = 0;      // 当前分组累积已添加的大小

    for (int item_idx = 0; item_idx < group->count; item_idx++) {
      const FileItem *item = &group->items[item_idx];

      // 将UTF-8路径转换为宽字符
      wchar_t *wpath = char_to_wchar(item->path);
      if (!wpath) {
        printf("    [警告] 无法转换路径编码: %s\n", item->path);
        continue;
      }

      // 构造带引号的宽字符路径
      wchar_t quoted_path[MAX_PATH_LENGTH + 10];
      if (wcschr(wpath, L' ') != NULL || wcschr(wpath, L'&') != NULL ||
          wcschr(wpath, L'|') != NULL || wcschr(wpath, L'>') != NULL ||
          wcschr(wpath, L'<') != NULL || wcschr(wpath, L'^') != NULL) {
        _snwprintf_s(quoted_path, _countof(quoted_path), _TRUNCATE, L" \"%s\"",
                     wpath);
      } else {
        _snwprintf_s(quoted_path, _countof(quoted_path), _TRUNCATE, L" %s",
                     wpath);
      }

      size_t quoted_path_len = wcslen(quoted_path);

      // 检查是否超过命令长度限制
      if (buffer_len + quoted_path_len >= 4000) {
        char command_size_str[32];
        format_size(current_command_total_size, command_size_str,
                    sizeof(command_size_str));

        // 计算累积到当前的大小
        cumulative_group_size += current_command_total_size;
        char cumulative_size_str[32];
        format_size(cumulative_group_size, cumulative_size_str,
                    sizeof(cumulative_size_str));

        printf(
            "  执行命令: git add [%d个路径, 本次添加: %s, 累积添加: %s/%s]\n",
            current_command_path_count, command_size_str, cumulative_size_str,
            group_total_size_str);

        int ret = _wsystem(command_buffer);
        total_commands++;
        if (ret == 0) {
          success_commands++;
          printf("    [成功] 命令执行成功\n");
        } else {
          printf("    [失败] 命令返回代码: %d\n", ret);
          // 打印失败的命令用于调试
          char *cmd_str = wchar_to_char(command_buffer);
          if (cmd_str) {
            printf("    失败命令: %s\n", cmd_str);
            free(cmd_str);
          }
        }

        // 重置命令缓冲区和计数器
        wcscpy_s(command_buffer, _countof(command_buffer), L"git add");
        buffer_len = wcslen(command_buffer);
        total_paths_processed += current_command_path_count;
        current_command_path_count = 0;
        current_command_total_size = 0;
      }

      // 添加路径到命令
      if (buffer_len + quoted_path_len < _countof(command_buffer)) {
        wcscat_s(command_buffer, _countof(command_buffer), quoted_path);
        buffer_len += quoted_path_len;
        current_command_path_count++;
        current_command_total_size += item->size;

        // 打印当前添加的文件信息
        char item_size_str[32];
        format_size(item->size, item_size_str, sizeof(item_size_str));
        const char *type_str = item->type == TYPE_FILE ? "文件" : "文件夹";
        printf("    添加%s: %s (%s)\n", type_str, item->path, item_size_str);
      }

      free(wpath);
    }

    // 执行分组最后一个命令（如果有内容）
    if (current_command_path_count > 0) {
      char command_size_str[32];
      format_size(current_command_total_size, command_size_str,
                  sizeof(command_size_str));

      // 计算最终累积大小
      cumulative_group_size += current_command_total_size;
      char cumulative_size_str[32];
      format_size(cumulative_group_size, cumulative_size_str,
                  sizeof(cumulative_size_str));

      printf("  执行命令: git add [%d个路径, 本次添加: %s, 累积添加: %s/%s]\n",
             current_command_path_count, command_size_str, cumulative_size_str,
             group_total_size_str);

      int ret = _wsystem(command_buffer);
      total_commands++;
      if (ret == 0) {
        success_commands++;
        printf("    [成功] 命令执行成功\n");
      } else {
        printf("    [失败] 命令返回代码: %d\n", ret);
        // 打印失败的命令用于调试
        char *cmd_str = wchar_to_char(command_buffer);
        if (cmd_str) {
          printf("    失败命令: %s\n", cmd_str);
          free(cmd_str);
        }
      }
      total_paths_processed += current_command_path_count;
    }

    // 执行git commit（使用宽字符）
    if (commit_info_file && commit_info_file[0] != '\0') {
      printf("\n执行提交: git commit -F %s\n", commit_info_file);

      wchar_t commit_command[512];
      wchar_t *wcommit_file = char_to_wchar(commit_info_file);
      if (wcommit_file) {
        _snwprintf_s(commit_command, _countof(commit_command), _TRUNCATE,
                     L"git commit -F \"%s\"", wcommit_file);

        int ret = _wsystem(commit_command);
        total_commands++;
        if (ret == 0) {
          success_commands++;
          printf("[成功] 提交完成\n");
        } else {
          printf("[失败] 提交命令返回代码: %d\n", ret);
        }
        free(wcommit_file);
      } else {
        printf("[错误] 无法转换提交信息文件路径编码\n");
      }
    } else {
      printf("\n[警告] 未提供提交信息文件，跳过提交步骤\n");
    }

    // 执行git push
    printf("\n执行推送: git push\n");
    int ret = _wsystem(L"git push");
    total_commands++;
    if (ret == 0) {
      success_commands++;
      printf("[成功] 推送完成\n");
    } else {
      printf("[失败] 推送命令返回代码: %d\n", ret);
    }

    printf("  分组 %d 处理完成，共处理 %d 个路径\n", group_idx + 1,
           group->count);
  }

  // 输出统计
  printf("\nGit操作统计:\n");
  printf("  总命令数: %d\n", total_commands);
  printf("  成功命令: %d\n", success_commands);
  printf("  失败命令: %d\n", total_commands - success_commands);
  printf("  总处理路径: %d\n", total_paths_processed);
  printf("  成功率: %.1f%%\n",
         total_commands > 0 ? (double)success_commands / total_commands * 100
                            : 0.0);
}

// 修改：改进的分组信息显示，清晰显示包含关系
void print_detailed_group_info(const FileGroup *group, int group_index) {
  printf("分组 %d 详细信息:\n", group_index + 1);

  long long total_size = 0;
  int file_count = 0, dir_count = 0;

  // 统计基本信息
  for (int i = 0; i < group->count; i++) {
    total_size += group->items[i].size;
    if (group->items[i].type == TYPE_FILE) {
      file_count++;
    } else {
      dir_count++;
    }
  }

  char size_str[32];
  format_size(total_size, size_str, sizeof(size_str));

  printf("  总大小: %s\n", size_str);
  printf("  包含: %d 个文件, %d 个文件夹\n", file_count, dir_count);

  // 显示使用率
  double usage_rate = (double)total_size / MAX_GROUP_SIZE * 100;
  printf("  使用率: %.1f%%\n", usage_rate);

  // 按类型分别显示
  printf("  文件夹列表:\n");
  for (int i = 0; i < group->count; i++) {
    if (group->items[i].type == TYPE_DIRECTORY) {
      char item_size_str[32];
      format_size(group->items[i].size, item_size_str, sizeof(item_size_str));
      printf("    [文件夹] %s (%s)\n", group->items[i].path, item_size_str);

      // 显示该文件夹包含的文件
      for (int j = 0; j < group->count; j++) {
        if (group->items[j].type == TYPE_FILE &&
            is_path_contained(group->items[j].path, group->items[i].path)) {
          char file_size_str[32];
          format_size(group->items[j].size, file_size_str,
                      sizeof(file_size_str));
          printf("      └─ [文件] %s (%s)\n", group->items[j].path,
                 file_size_str);
        }
      }
    }
  }

  // 显示独立的文件（不被任何文件夹包含）
  printf("  独立文件列表:\n");
  int has_independent_files = 0;
  for (int i = 0; i < group->count; i++) {
    if (group->items[i].type == TYPE_FILE) {
      int is_contained = 0;
      for (int j = 0; j < group->count; j++) {
        if (group->items[j].type == TYPE_DIRECTORY &&
            is_path_contained(group->items[i].path, group->items[j].path)) {
          is_contained = 1;
          break;
        }
      }
      if (!is_contained) {
        char file_size_str[32];
        format_size(group->items[i].size, file_size_str, sizeof(file_size_str));
        printf("    [文件] %s (%s)\n", group->items[i].path, file_size_str);
        has_independent_files = 1;
      }
    }
  }

  if (!has_independent_files) {
    printf("    无独立文件\n");
  }

  printf("\n");
}

// 新增：检查是否处理过大文件
int has_processed_large_files(AdditionalFiles *additional) {
  if (!additional)
    return 0;

  // 如果有拆分文件或.gitignore文件被处理，则认为处理过大文件
  return (additional->split_count > 0 || additional->gitignore_count > 0);
}

// 修改：在处理大文件后检查是否需要重启
int run_grouping_test_with_git(char *paths[], int path_count,
                               const char *commit_info_file) {
  long long total_scanned_size, skipped_files_size;

  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size);

  // 先调用print_skipped_files，它会返回需要添加的额外文件
  AdditionalFiles additional = print_skipped_files(&result);

  // 检查是否处理过大文件，如果是则重启程序
  if (additional.has_large_files) {
    printf("\n[重启] 检测到大文件处理，准备重启程序...\n");

    // 释放内存
    // free_additional_files(&additional);
    // free_group_result(&result);

    // 重启程序（这里需要传递argv参数，稍后在main函数中处理）
    // 重启逻辑将在main函数中实现
    // return 1;
  }

  // 如果没有重启，继续正常流程
  // 将额外文件添加到分组中
  add_additional_files_to_groups(&result, &additional);

  // 然后打印更新后的分组结果
  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  // 执行Git操作
  // execute_git_commands(&result, commit_info_file);

  // 释放内存
  free_additional_files(&additional);
  free_group_result(&result);
  return 0;
}

// 修改：非Git模式也添加重启检查
int run_grouping_test(char *paths[], int path_count) {
  long long total_scanned_size, skipped_files_size;

  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size);

  // 处理大文件
  AdditionalFiles additional = print_skipped_files(&result);

  // 检查是否处理过大文件，如果是则重启程序
  if (additional.has_large_files) {
    printf("\n[重启] 检测到大文件处理，准备重启程序...\n");

    // 释放内存
    // free_additional_files(&additional);
    // free_group_result(&result);

    return 1;
  }

  // 如果没有重启，继续正常流程
  add_additional_files_to_groups(&result, &additional);
  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  free_additional_files(&additional);
  free_group_result(&result);
  return 0;
}

// 修改：完全重写的git状态路径获取函数，解决路径访问问题
char **get_git_status_paths(int *path_count) {
  printf("[Git] 正在执行 git status --porcelain...\n");

  FILE *pipe = _popen("git status --porcelain", "r");
  if (!pipe) {
    printf("[错误] 无法执行git命令\n");
    return NULL;
  }

  char **paths = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  *path_count = 0;
  char line[MAX_PATH_LENGTH * 2];

  while (fgets(line, sizeof(line), pipe) && *path_count < MAX_ITEMS) {
    // git status --porcelain 格式示例：
    // " M README.md"
    // "MM file.txt"
    // "?? newfile.txt"
    // " R oldfile.txt -> newfile.txt"
    // "?? \"file with spaces.txt\""
    // " M 目录名/"  <-- 目录后面有斜杠

    char *path_start = line;

    // 跳过状态字符（最多3个）
    int status_chars = 0;
    while (*path_start &&
           (*path_start == ' ' || *path_start == 'M' || *path_start == 'A' ||
            *path_start == 'D' || *path_start == 'R' || *path_start == 'C' ||
            *path_start == 'U' || *path_start == '?' || *path_start == '!')) {
      path_start++;
      status_chars++;
      if (status_chars >= 3)
        break;
    }

    // 跳过状态字符后的空格
    while (*path_start == ' ') {
      path_start++;
    }

    // 移除行尾的换行符
    size_t len = strlen(path_start);
    if (len > 0 && path_start[len - 1] == '\n') {
      path_start[len - 1] = '\0';
      len--;
    }

    // 处理重命名的情况 "R  oldfile -> newfile"
    char *arrow_pos = strstr(path_start, " -> ");
    if (arrow_pos) {
      path_start = arrow_pos + 4; // 跳过 " -> "
      len = strlen(path_start);
    }

    // 现在处理路径，可能被引号包围
    char *final_path = path_start;

    // 如果路径以引号开始和结束，去除引号
    if (len >= 2 && path_start[0] == '"' && path_start[len - 1] == '"') {
      path_start[len - 1] = '\0';  // 去除结尾引号
      final_path = path_start + 1; // 跳过开头引号
      len -= 2;
    }

    // 去除可能的前后空格
    char *trimmed_path = final_path;
    while (*trimmed_path == ' ') {
      trimmed_path++;
    }

    char *end = trimmed_path + strlen(trimmed_path) - 1;
    while (end > trimmed_path && *end == ' ') {
      *end = '\0';
      end--;
    }

    // 如果路径不为空，则添加到列表中
    if (strlen(trimmed_path) > 0) {
      // 新的处理逻辑：不再检查路径是否存在，直接根据Git状态处理

      // 检查是否是目录指示（以斜杠结尾）
      int is_directory_indicated = 0;
      size_t trimmed_len = strlen(trimmed_path);
      if (trimmed_len > 0 && (trimmed_path[trimmed_len - 1] == '/' ||
                              trimmed_path[trimmed_len - 1] == '\\')) {
        is_directory_indicated = 1;

        // 确保目录路径格式正确（使用反斜杠）
        char dir_path[MAX_PATH_LENGTH];
        strcpy_s(dir_path, MAX_PATH_LENGTH, trimmed_path);

        // 替换所有正斜杠为反斜杠
        for (char *p = dir_path; *p; p++) {
          if (*p == '/')
            *p = '\\';
        }

        // 确保以反斜杠结尾
        size_t dir_len = strlen(dir_path);
        if (dir_len > 0 && dir_path[dir_len - 1] != '\\') {
          if (dir_len < MAX_PATH_LENGTH - 1) {
            strcat_s(dir_path, MAX_PATH_LENGTH, "\\");
          }
        }

        paths[*path_count] = (char *)safe_malloc(strlen(dir_path) + 1);
        strcpy(paths[*path_count], dir_path);
        (*path_count)++;
        printf("  [目录] 添加目录路径: '%s'\n", dir_path);
      } else {
        // 文件路径
        char file_path[MAX_PATH_LENGTH];
        strcpy_s(file_path, MAX_PATH_LENGTH, trimmed_path);

        // 替换所有正斜杠为反斜杠
        for (char *p = file_path; *p; p++) {
          if (*p == '/')
            *p = '\\';
        }

        paths[*path_count] = (char *)safe_malloc(strlen(file_path) + 1);
        strcpy(paths[*path_count], file_path);
        (*path_count)++;
        printf("  [文件] 添加文件路径: '%s'\n", file_path);
      }
    }
  }

  _pclose(pipe);

  printf("[Git] 找到 %d 个变更项\n", *path_count);

  // 打印所有找到的文件用于调试
  if (*path_count > 0) {
    printf("[Git] 变更项列表:\n");
    for (int i = 0; i < *path_count; i++) {
      DWORD attr = GetFileAttributesA(paths[i]);
      if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("  %d. '%s' [无法访问]\n", i + 1, paths[i]);
      } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        printf("  %d. '%s' [目录]\n", i + 1, paths[i]);
      } else {
        printf("  %d. '%s' [文件]\n", i + 1, paths[i]);
      }
    }
  }

  return paths;
}

// 修改：改进的目录路径规范化函数
void normalize_directory_path(char *path) {
  if (!path || strlen(path) == 0)
    return;

  // 首先调用基本规范化
  normalize_path(path);

  // 确保目录路径以反斜杠结尾（除非是根目录）
  size_t len = strlen(path);
  if (len > 0 && path[len - 1] != '\\') {
    // 检查是否是盘符根目录（如 "C:"）
    if (len == 2 && path[1] == ':') {
      // 盘符根目录，添加反斜杠
      if (len < MAX_PATH_LENGTH - 1) {
        path[len] = '\\';
        path[len + 1] = '\0';
      }
    } else if (!(len == 3 && path[1] == ':' && path[2] == '\\')) {
      // 不是盘符根目录，确保以反斜杠结尾
      if (len < MAX_PATH_LENGTH - 1) {
        path[len] = '\\';
        path[len + 1] = '\0';
      }
    }
  }
}

// 释放git状态路径内存
void free_git_status_paths(char **paths, int path_count) {
  for (int i = 0; i < path_count; i++) {
    free(paths[i]);
  }
  free(paths);
}

// 修改：改进重启程序函数，使用正确的命令提示符
void restart_program(char *argv[], int has_processed_large_files) {
  printf("\n========================================\n");
  printf("              程序重启\n");
  printf("========================================\n\n");

  if (has_processed_large_files) {
    printf("[信息] 检测到大文件已处理，正在重启程序以重新扫描...\n");

    // 获取当前工作目录
    char current_dir[MAX_PATH_LENGTH];
    if (!GetCurrentDirectoryA(MAX_PATH_LENGTH, current_dir)) {
      printf("[错误] 无法获取当前工作目录\n");
      return;
    }

    // 获取当前程序路径
    char exe_path[MAX_PATH_LENGTH];
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH_LENGTH) == 0) {
      printf("[错误] 无法获取程序路径，无法自动重启\n");
      return;
    }

    // 构建cmd命令
    char cmd[MAX_PATH_LENGTH * 3] = {0};
    if (argv[1] != NULL) {
      // 有提交信息文件参数
      snprintf(cmd, sizeof(cmd), "cmd /k \"\"%s\" \"%s\"\"", exe_path, argv[1]);
    } else {
      // 没有参数
      snprintf(cmd, sizeof(cmd), "cmd /k \"\"%s\"\"", exe_path);
    }

    printf("重启命令: %s\n", cmd);
    printf("工作目录: %s\n", current_dir);

    // 创建进程信息
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // 启动新进程（使用cmd /k 保持窗口打开）
    if (CreateProcessA(NULL,               // 不使用模块名
                       cmd,                // 命令行
                       NULL,               // 进程句柄不可继承
                       NULL,               // 线程句柄不可继承
                       FALSE,              // 不继承句柄
                       CREATE_NEW_CONSOLE, // 创建新控制台
                       NULL,               // 使用父进程环境块
                       current_dir,        // 指定工作目录
                       &si,                // STARTUPINFO 指针
                       &pi                 // PROCESS_INFORMATION 指针
                       )) {
      printf("[成功] 新进程已启动 (PID: %lu)，当前进程将退出\n",
             pi.dwProcessId);

      // 等待一段时间确保新进程启动
      Sleep(2000);

      // 关闭进程和线程句柄
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);

      // 退出当前进程
      exit(0);
    } else {
      DWORD error = GetLastError();
      printf("[错误] 无法启动新进程 (错误代码: %lu)\n", error);
      printf("[信息] 请手动重新运行程序\n");

      // 提供手动重启的指令
      printf("\n手动重启指令:\n");
      if (argv[1] != NULL) {
        printf("  \"%s\" \"%s\"\n", exe_path, argv[1]);
      } else {
        printf("  \"%s\"\n", exe_path);
      }
    }
  } else {
    printf("[信息] 没有大文件需要处理，无需重启\n");
  }
}

int main(int argc, char *argv[]) {
  // 设置控制台输出为UTF-8
  SetConsoleOutputCP(CP_UTF8);

  printf("========================================\n");
  printf("          文件分组工具 (带Git功能)\n");
  printf("========================================\n\n");

  // 显示当前工作目录用于调试
  char current_dir[MAX_PATH_LENGTH];
  if (GetCurrentDirectoryA(MAX_PATH_LENGTH, current_dir)) {
    printf("当前工作目录: %s\n", current_dir);
  }

  // 检查命令行参数
  const char *commit_info_file = NULL;
  int use_git = 0;

  if (argc >= 2) {
    commit_info_file = argv[1];
    use_git = 1;
    printf("提交信息文件: %s\n\n", commit_info_file);
  } else {
    printf("[信息] 未指定提交信息文件，将跳过Git操作\n");
    printf("用法: %s <commit-info.txt>\n\n", argv[0]);
  }

  // 从git status获取文件路径
  int path_count = 0;
  char **input_paths = get_git_status_paths(&path_count);

  if (path_count == 0 || !input_paths) {
    printf("[错误] 无法从git status获取文件列表或没有变更文件\n");

    // 使用备用路径（原来的硬编码路径）
    printf("[信息] 使用备用路径\n");
    char *backup_paths[] = {
        ".",
    };
    input_paths = backup_paths;
    path_count = sizeof(backup_paths) / sizeof(backup_paths[0]);
  }

  printf("\n最终扫描路径 (%d 个文件):\n", path_count);
  for (int i = 0; i < (path_count > 10 ? 10 : path_count); i++) {
    printf("  %d. '%s'\n", i + 1, input_paths[i]);
  }
  if (path_count > 10) {
    printf("  ... 还有 %d 个文件\n", path_count - 10);
  }
  printf("\n");

  int need_restart = 0;
  // 根据参数选择是否使用Git功能
  if (use_git) {
    need_restart =
        run_grouping_test_with_git(input_paths, path_count, commit_info_file);
  } else {
    need_restart = run_grouping_test(input_paths, path_count);
  }

  // 检查是否需要重启
  if (need_restart) {
    restart_program(argv, 1);
    // 如果重启函数返回，说明重启失败，继续执行
    printf("[警告] 重启失败，继续执行后续操作\n");
  }

  // 如果是动态分配的内存，需要释放
  if (input_paths != NULL && path_count > 0) {
    // 检查是否是动态分配的（通过get_git_status_paths）
    // 这里简单判断：如果第一个路径不是"."，则认为是动态分配的
    if (strcmp(input_paths[0], ".") != 0) {
      free_git_status_paths(input_paths, path_count);
    }
  }

  printf("\n[完成] 所有处理完成！\n");
  return 0;
}
