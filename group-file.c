#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define MAX_GROUP_SIZE (100 * 1024 * 1024LL) // 100MB
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
  FileGroup *groups;
  int group_count;
  int groups_capacity;
  long long total_input_size;
  long long skipped_size;
  int total_files;
  int total_directories;
} GroupResult;

typedef struct {
  wchar_t path[MAX_PATH_LENGTH];
  int depth;
} DirectoryEntry;

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
    // 路径被截断
    dest[dest_size - 1] = L'\0';
    return 0; // 失败
  }
  return 1; // 成功
}

// 路径规范化函数：移除尾部斜杠，统一格式
void normalize_path(char *path) {
  if (!path || strlen(path) == 0)
    return;

  // 移除尾部斜杠和反斜杠
  size_t len = strlen(path);
  while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
    path[len - 1] = '\0';
    len--;
  }

  // 统一斜杠方向为反斜杠（Windows风格）
  for (char *p = path; *p; p++) {
    if (*p == '/')
      *p = '\\';
  }
}

// 迭代计算文件夹大小（避免递归深度问题）
long long calculate_directory_size_iterative(const wchar_t *wpath,
                                             long long *total_scanned_size) {
  long long total_size = 0;
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  // 初始化队列
  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  while (queue_front != queue_rear) {
    // 出队列
    DirectoryEntry current = queue[queue_front];
    queue_front = (queue_front + 1) % MAX_DIRECTORY_QUEUE;

    // 如果深度过大，跳过（防止恶意目录结构）
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

      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        // 入队列处理子目录
        if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
          wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
          queue[queue_rear].depth = current.depth + 1;
          queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;
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

  free(queue);
  return total_size;
}

// 迭代收集文件和文件夹信息
void collect_items_iterative(const wchar_t *wpath, FileItem *items,
                             int *item_count, long long *total_scanned_size,
                             long long *skipped_files_size) {
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  // 初始化队列
  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  // 用于跟踪已处理的目录，避免重复
  char **processed_dirs = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  int processed_count = 0;

  while (queue_front != queue_rear && *item_count < MAX_ITEMS) {
    // 出队列
    DirectoryEntry current = queue[queue_front];
    queue_front = (queue_front + 1) % MAX_DIRECTORY_QUEUE;

    // 如果深度过大，跳过
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
        // 计算目录大小
        long long dir_size =
            calculate_directory_size_iterative(full_path, total_scanned_size);

        if (dir_size <= MAX_GROUP_SIZE) {
          // 检查是否已经处理过这个目录
          char *char_path = wchar_to_char(full_path);
          if (char_path) {
            normalize_path(char_path);

            int already_processed = 0;
            for (int i = 0; i < processed_count; i++) {
              if (strcmp(processed_dirs[i], char_path) == 0) {
                already_processed = 1;
                break;
              }
            }

            if (!already_processed && strlen(char_path) < MAX_PATH_LENGTH - 1) {
              // 添加到已处理列表
              processed_dirs[processed_count] =
                  (char *)safe_malloc(strlen(char_path) + 1);
              strcpy(processed_dirs[processed_count], char_path);
              processed_count++;

              // 添加到项目列表
              strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
              items[*item_count].size = dir_size;
              items[*item_count].type = TYPE_DIRECTORY;
              (*item_count)++;
            }
            free(char_path);
          }
        } else {
          // 目录太大，入队列继续处理子项
          if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
            wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
            queue[queue_rear].depth = current.depth + 1;
            queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;
          }
        }
      } else {
        // 处理文件
        ULARGE_INTEGER file_size;
        file_size.LowPart = find_data.nFileSizeLow;
        file_size.HighPart = find_data.nFileSizeHigh;
        *total_scanned_size += file_size.QuadPart;

        if (file_size.QuadPart > MAX_GROUP_SIZE) {
          char *char_path = wchar_to_char(full_path);
          if (char_path) {
            printf("跳过大文件: %s (%.2f MB)\n", char_path,
                   file_size.QuadPart / (1024.0 * 1024.0));
            free(char_path);
          }
          *skipped_files_size += file_size.QuadPart;
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

  // 清理已处理目录列表
  for (int i = 0; i < processed_count; i++) {
    free(processed_dirs[i]);
  }
  free(processed_dirs);
  free(queue);
}

// 处理输入路径
void process_input_path(const char *path, FileItem *items, int *item_count,
                        long long *total_input_size,
                        long long *total_scanned_size,
                        long long *skipped_files_size) {
  wchar_t *wpath = char_to_wchar(path);
  if (!wpath) {
    printf("警告: 无法转换路径编码: %s\n", path);
    return;
  }

  DWORD attr = GetFileAttributesW(wpath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    printf("警告: 无法访问路径 %s\n", path);
    free(wpath);
    return;
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("扫描文件夹: %s\n", path);
    long long dir_size =
        calculate_directory_size_iterative(wpath, total_scanned_size);
    *total_input_size += dir_size;

    printf("文件夹 %s 总大小: %.2f MB\n", path, dir_size / (1024.0 * 1024.0));

    if (dir_size > MAX_GROUP_SIZE) {
      printf("文件夹太大，递归处理子项...\n");
    } else {
      printf("文件夹大小合适，直接添加...\n");

      // 添加根目录本身
      if (*item_count < MAX_ITEMS) {
        char *char_path = wchar_to_char(wpath);
        if (char_path) {
          normalize_path(char_path);
          if (strlen(char_path) < MAX_PATH_LENGTH - 1) {
            strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
            items[*item_count].size = dir_size;
            items[*item_count].type = TYPE_DIRECTORY;
            (*item_count)++;
          }
          free(char_path);
        }
      }
    }

    // 使用迭代方式收集项目
    collect_items_iterative(wpath, items, item_count, total_scanned_size,
                            skipped_files_size);
  } else {
    // 处理文件
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

      if (file_size.QuadPart > MAX_GROUP_SIZE) {
        printf("跳过大文件: %s (%.2f MB)\n", path,
               file_size.QuadPart / (1024.0 * 1024.0));
        *skipped_files_size += file_size.QuadPart;
      } else {
        if (*item_count < MAX_ITEMS) {
          if (strlen(path) < MAX_PATH_LENGTH - 1) {
            char normalized_path[MAX_PATH_LENGTH];
            strcpy_s(normalized_path, MAX_PATH_LENGTH, path);
            normalize_path(normalized_path);

            strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, normalized_path);
            items[*item_count].size = file_size.QuadPart;
            items[*item_count].type = TYPE_FILE;
            (*item_count)++;
          } else {
            printf("警告: 路径过长被跳过: %s\n", path);
          }
        }
      }
    }
  }

  free(wpath);
}

// 比较函数：文件在前，文件夹在后，按大小降序
int compare_items(const void *a, const void *b) {
  const FileItem *item1 = (const FileItem *)a;
  const FileItem *item2 = (const FileItem *)b;

  if (item1->type != item2->type) {
    return item1->type - item2->type; // 文件在前
  }

  // 按大小降序排列
  if (item1->size > item2->size)
    return -1;
  if (item1->size < item2->size)
    return 1;

  return strcmp(item1->path, item2->path);
}

// 改进的分组算法 - 动态内存分配
GroupResult group_files(FileItem *items, int item_count) {
  GroupResult result = {0};

  // 初始分配小容量
  int initial_group_count = (item_count > 100) ? (item_count / 100) + 1 : 10;
  result.groups =
      (FileGroup *)safe_malloc(sizeof(FileGroup) * initial_group_count);
  result.group_count = 0;
  result.groups_capacity = initial_group_count;

  // 按类型和大小排序
  printf("正在排序 %d 个项...\n", item_count);
  qsort(items, item_count, sizeof(FileItem), compare_items);

  // 初始化分组 - 使用小初始容量
  for (int i = 0; i < initial_group_count; i++) {
    result.groups[i].items =
        (FileItem *)safe_malloc(sizeof(FileItem) * 10); // 小初始容量
    result.groups[i].count = 0;
    result.groups[i].capacity = 10;
    result.groups[i].total_size = 0;
  }

  printf("开始分组处理...\n");

  // 改进的最佳适应算法
  for (int i = 0; i < item_count; i++) {
    if (i % 1000 == 0) {
      printf("处理进度: %d/%d (%.1f%%)\n", i, item_count,
             (double)i / item_count * 100);
    }

    int best_group = -1;
    long long best_remaining = MAX_GROUP_SIZE;

    // 寻找最适合的分组
    for (int j = 0; j < result.group_count; j++) {
      long long remaining = MAX_GROUP_SIZE - result.groups[j].total_size;
      if (remaining >= items[i].size && remaining < best_remaining) {
        best_remaining = remaining;
        best_group = j;
      }
    }

    if (best_group != -1) {
      // 放入现有分组
      FileGroup *group = &result.groups[best_group];

      // 动态扩展容量
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
      // 创建新分组
      if (result.group_count >= result.groups_capacity) {
        // 扩展分组数组
        int new_capacity = result.groups_capacity * 2;
        FileGroup *new_groups = (FileGroup *)safe_realloc(
            result.groups, sizeof(FileGroup) * new_capacity);

        // 初始化新分配的分组
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
      new_group->items[new_group->count++] = items[i];
      new_group->total_size += items[i].size;
      result.group_count++;
    }
  }

  printf("分组完成，共 %d 个分组\n", result.group_count);

  // 优化内存使用：收缩分组数组到实际大小
  if (result.group_count < result.groups_capacity) {
    result.groups = (FileGroup *)safe_realloc(
        result.groups, sizeof(FileGroup) * result.group_count);
    result.groups_capacity = result.group_count;
  }

  return result;
}

// 打印分组结果
void print_groups(const GroupResult *result) {
  printf("=== 分组结果 ===\n\n");

  int max_groups_to_show = 10; // 只显示前10组详情
  int groups_to_show = result->group_count < max_groups_to_show
                           ? result->group_count
                           : max_groups_to_show;

  for (int i = 0; i < groups_to_show; i++) {
    printf("分组 %d:\n", i + 1);
    printf("  总大小: %.2f MB\n",
           result->groups[i].total_size / (1024.0 * 1024.0));

    int file_count = 0, dir_count = 0;
    for (int j = 0; j < result->groups[i].count; j++) {
      if (result->groups[i].items[j].type == TYPE_FILE) {
        file_count++;
      } else {
        dir_count++;
      }
    }
    printf("  文件数: %d, 文件夹数: %d\n", file_count, dir_count);

    // 只显示前5个项
    printf("  前5个项:\n");
    int items_to_show =
        result->groups[i].count < 5 ? result->groups[i].count : 5;
    for (int j = 0; j < items_to_show; j++) {
      const char *type_str =
          result->groups[i].items[j].type == TYPE_FILE ? "文件" : "文件夹";
      printf("    %s: %s (%.2f MB)\n", type_str,
             result->groups[i].items[j].path,
             result->groups[i].items[j].size / (1024.0 * 1024.0));
    }
    if (result->groups[i].count > 5) {
      printf("    ... 还有 %d 个项\n", result->groups[i].count - 5);
    }
    printf("\n");
  }

  if (result->group_count > max_groups_to_show) {
    printf("... 还有 %d 个分组未显示\n\n",
           result->group_count - max_groups_to_show);
  }
}

// 打印统计信息
void print_statistics(const GroupResult *result, long long total_scanned_size,
                      long long skipped_files_size) {
  printf("=== 统计信息 ===\n\n");

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

  printf("总分组数: %d\n", result->group_count);
  printf("平均每组大小: %.2f MB\n", avg_group_size / (1024.0 * 1024.0));
  printf("\n");
  printf("分组文件数: %d\n", total_grouped_files);
  printf("分组文件夹数: %d\n", total_grouped_dirs);
  printf("分组总大小: %.2f MB\n", total_grouped_size / (1024.0 * 1024.0));
  printf("扫描总大小: %.2f MB\n", total_scanned_size / (1024.0 * 1024.0));
  printf("跳过大文件总大小: %.2f MB\n", skipped_files_size / (1024.0 * 1024.0));
}

// 验证结果
void validate_result(const GroupResult *result, long long input_total_size,
                     long long total_scanned_size,
                     long long skipped_files_size) {
  printf("=== 验证结果 ===\n\n");

  long long total_grouped_size = 0;
  for (int i = 0; i < result->group_count; i++) {
    total_grouped_size += result->groups[i].total_size;
  }

  long long calculated_total = total_grouped_size + skipped_files_size;

  printf("输入总大小: %.2f MB\n", input_total_size / (1024.0 * 1024.0));
  printf("分组总大小: %.2f MB\n", total_grouped_size / (1024.0 * 1024.0));
  printf("跳过大文件总大小: %.2f MB\n", skipped_files_size / (1024.0 * 1024.0));
  printf("扫描总大小: %.2f MB\n", total_scanned_size / (1024.0 * 1024.0));
  printf("计算总大小: %.2f MB\n", calculated_total / (1024.0 * 1024.0));

  if (calculated_total == input_total_size) {
    printf("✓ 验证成功: 没有文件遗漏\n");
  } else if (calculated_total < input_total_size) {
    printf("⚠ 警告: 可能有文件遗漏 (相差 %.2f MB)\n",
           (input_total_size - calculated_total) / (1024.0 * 1024.0));
    printf("可能原因:\n");
    printf("  1. 隐藏文件或系统文件未被统计\n");
    printf("  2. 权限问题导致某些文件无法访问\n");
    printf("  3. 符号链接或特殊文件类型\n");
  } else {
    printf("✗ 错误: 数据不一致 (计算值大于输入值)\n");
    printf("calculated_total - input_total_size\n%.2f MB - %.2f MB = %.2f MB "
           "(%lld)\n",
           calculated_total / (1024.0 * 1024.0),
           input_total_size / (1024.0 * 1024.0),
           (calculated_total - input_total_size) / (1024.0 * 1024.0),
           calculated_total - input_total_size);
  }
}

// 释放内存
void free_group_result(GroupResult *result) {
  if (result && result->groups) {
    for (int i = 0; i < result->groups_capacity; i++) {
      if (result->groups[i].items) {
        free(result->groups[i].items);
        result->groups[i].items = NULL;
      }
    }
    free(result->groups);
    result->groups = NULL;
  }
  result->group_count = 0;
  result->groups_capacity = 0;
}

// 新封装的函数：处理输入路径并生成分组结果
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size) {
  // 收集所有文件和文件夹信息
  FileItem *items = (FileItem *)safe_malloc(sizeof(FileItem) * MAX_ITEMS);
  int item_count = 0;
  long long total_input_size = 0;

  // 初始化统计变量
  *total_scanned_size = 0;
  *skipped_files_size = 0;

  printf("正在扫描文件和文件夹...\n\n");

  for (int i = 0; i < path_count; i++) {
    process_input_path(paths[i], items, &item_count, &total_input_size,
                       total_scanned_size, skipped_files_size);
  }

  printf("\n扫描完成:\n");
  printf("  共收集到 %d 个有效项\n", item_count);
  printf("  输入总大小: %.2f MB\n", total_input_size / (1024.0 * 1024.0));
  printf("  扫描总大小: %.2f MB\n", *total_scanned_size / (1024.0 * 1024.0));
  printf("  跳过大文件总大小: %.2f MB\n\n",
         *skipped_files_size / (1024.0 * 1024.0));

  // 执行分组
  printf("正在进行分组...\n");
  GroupResult result = group_files(items, item_count);
  result.total_input_size = total_input_size;
  result.skipped_size = *skipped_files_size;

  // 释放items内存
  free(items);

  return result;
}

// 新的测试函数：运行算法，打印结果，释放内存
void run_grouping_test(char *paths[], int path_count) {
  long long total_scanned_size, skipped_files_size;

  // 处理输入路径并生成分组结果
  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size);

  // 输出结果
  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  // 释放内存
  free_group_result(&result);
}

int main() {
  // 设置控制台输出为UTF-8，防止中文乱码
  SetConsoleOutputCP(CP_UTF8);

  printf("文件分组工具 - 开始处理预设路径\n\n");

  // 硬编码的路径数组 - 你可以在这里修改为你想要扫描的路径
  // char *input_paths[] = {"C:\\Users"};
  // char *input_paths[] = {"C:\\Program Files", "C:\\Program Files (x86)",
  //                        "C:\\ProgramData", "C:\\Windows"};
  char *input_paths[] = {"C:\\Windows"};

  int path_count = sizeof(input_paths) / sizeof(input_paths[0]);

  // 显示将要扫描的路径
  printf("预设扫描路径:\n");
  for (int i = 0; i < path_count; i++) {
    printf("  %d. %s\n", i + 1, input_paths[i]);
  }
  printf("\n");

  // 使用新的测试函数
  run_grouping_test(input_paths, path_count);

  return 0;
}
