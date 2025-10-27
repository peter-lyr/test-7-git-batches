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
    dest[dest_size - 1] = L'\0';
    return 0;
  }
  return 1;
}

// 路径规范化函数
void normalize_path(char *path) {
  if (!path || strlen(path) == 0)
    return;

  size_t len = strlen(path);
  while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
    path[len - 1] = '\0';
    len--;
  }

  for (char *p = path; *p; p++) {
    if (*p == '/')
      *p = '\\';
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

// 迭代计算文件夹大小
long long calculate_directory_size_iterative(const wchar_t *wpath,
                                             long long *total_scanned_size) {
  long long total_size = 0;
  DirectoryEntry *queue = (DirectoryEntry *)safe_malloc(sizeof(DirectoryEntry) *
                                                        MAX_DIRECTORY_QUEUE);
  int queue_front = 0, queue_rear = 0;

  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

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

  wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, wpath);
  queue[queue_rear].depth = 0;
  queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;

  char **processed_dirs = (char **)safe_malloc(sizeof(char *) * MAX_ITEMS);
  int processed_count = 0;

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
        long long dir_size =
            calculate_directory_size_iterative(full_path, total_scanned_size);

        if (dir_size <= MAX_GROUP_SIZE) {
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
              processed_dirs[processed_count] =
                  (char *)safe_malloc(strlen(char_path) + 1);
              strcpy(processed_dirs[processed_count], char_path);
              processed_count++;

              strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
              items[*item_count].size = dir_size;
              items[*item_count].type = TYPE_DIRECTORY;
              (*item_count)++;
            }
            free(char_path);
          }
        } else {
          if ((queue_rear + 1) % MAX_DIRECTORY_QUEUE != queue_front) {
            wcscpy_s(queue[queue_rear].path, MAX_PATH_LENGTH, full_path);
            queue[queue_rear].depth = current.depth + 1;
            queue_rear = (queue_rear + 1) % MAX_DIRECTORY_QUEUE;
          }
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
    printf("[警告] 无法转换路径编码: %s\n", path);
    return;
  }

  DWORD attr = GetFileAttributesW(wpath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    printf("[警告] 无法访问路径 %s\n", path);
    free(wpath);
    return;
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("[扫描] 文件夹: %s\n", path);
    long long dir_size =
        calculate_directory_size_iterative(wpath, total_scanned_size);
    *total_input_size += dir_size;

    char size_str[32];
    format_size(dir_size, size_str, sizeof(size_str));
    printf("       文件夹大小: %s\n", size_str);

    if (dir_size > MAX_GROUP_SIZE) {
      printf("       文件夹太大，递归处理子项...\n");
    } else {
      printf("       文件夹大小合适，直接添加...\n");

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

    collect_items_iterative(wpath, items, item_count, total_scanned_size,
                            skipped_files_size);
  } else {
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
        printf("[跳过] 大文件: %s", path);
        char size_str[32];
        format_size(file_size.QuadPart, size_str, sizeof(size_str));
        printf(" (%s)\n", size_str);
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
            printf("[警告] 路径过长被跳过: %s\n", path);
          }
        }
      }
    } else {
      printf("[警告] 无法打开文件 %s (错误: %lu)\n", path, GetLastError());
    }
  }

  free(wpath);
}

// 比较函数：文件在前，文件夹在后，按大小降序
int compare_items(const void *a, const void *b) {
  const FileItem *item1 = (const FileItem *)a;
  const FileItem *item2 = (const FileItem *)b;

  if (item1->type != item2->type) {
    return item1->type - item2->type;
  }

  if (item1->size > item2->size)
    return -1;
  if (item1->size < item2->size)
    return 1;

  return strcmp(item1->path, item2->path);
}

// 分组算法
GroupResult group_files(FileItem *items, int item_count) {
  GroupResult result = {0};

  int initial_group_count = (item_count > 100) ? (item_count / 100) + 1 : 10;
  result.groups =
      (FileGroup *)safe_malloc(sizeof(FileGroup) * initial_group_count);
  result.group_count = 0;
  result.groups_capacity = initial_group_count;

  printf("[处理] 正在排序 %d 个项...\n", item_count);
  qsort(items, item_count, sizeof(FileItem), compare_items);

  for (int i = 0; i < initial_group_count; i++) {
    result.groups[i].items = (FileItem *)safe_malloc(sizeof(FileItem) * 10);
    result.groups[i].count = 0;
    result.groups[i].capacity = 10;
    result.groups[i].total_size = 0;
  }

  printf("[处理] 开始分组处理...\n");

  for (int i = 0; i < item_count; i++) {
    if (i % 1000 == 0 || i == item_count - 1) {
      draw_progress_bar(i + 1, item_count, "分组进度");
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
      new_group->items[new_group->count++] = items[i];
      new_group->total_size += items[i].size;
      result.group_count++;
    }
  }

  printf("\n[完成] 分组完成，共 %d 个分组\n", result.group_count);

  if (result.group_count < result.groups_capacity) {
    result.groups = (FileGroup *)safe_realloc(
        result.groups, sizeof(FileGroup) * result.group_count);
    result.groups_capacity = result.group_count;
  }

  return result;
}

// 打印分组结果
void print_groups(const GroupResult *result) {
  printf("\n========================================\n");
  printf("              分组结果\n");
  printf("========================================\n\n");

  int max_groups_to_show = 10;
  int groups_to_show = result->group_count < max_groups_to_show
                           ? result->group_count
                           : max_groups_to_show;

  for (int i = 0; i < groups_to_show; i++) {
    printf("分组 %d:\n", i + 1);

    char size_str[32];
    format_size(result->groups[i].total_size, size_str, sizeof(size_str));
    printf("  总大小: %s\n", size_str);

    // 计算使用率
    double usage_rate =
        (double)result->groups[i].total_size / MAX_GROUP_SIZE * 100;

    printf("  使用率: %.1f%% (%.2f/%.2f MB)\n", usage_rate,
           result->groups[i].total_size / (1024.0 * 1024.0),
           MAX_GROUP_SIZE / (1024.0 * 1024.0));

    int file_count = 0, dir_count = 0;
    for (int j = 0; j < result->groups[i].count; j++) {
      if (result->groups[i].items[j].type == TYPE_FILE) {
        file_count++;
      } else {
        dir_count++;
      }
    }
    printf("  包含: %d 个文件, %d 个文件夹\n", file_count, dir_count);

    printf("  前5个项:\n");
    int items_to_show =
        result->groups[i].count < 5 ? result->groups[i].count : 5;
    for (int j = 0; j < items_to_show; j++) {
      const char *type_str =
          result->groups[i].items[j].type == TYPE_FILE ? "文件" : "文件夹";
      char item_size_str[32];
      format_size(result->groups[i].items[j].size, item_size_str,
                  sizeof(item_size_str));

      printf("    [%s] %s (%s)\n", type_str, result->groups[i].items[j].path,
             item_size_str);
    }
    if (result->groups[i].count > 5) {
      printf("    ... 还有 %d 个项\n", result->groups[i].count - 5);
    }
    printf("\n");
  }

  if (result->group_count > max_groups_to_show) {
    printf("[信息] 还有 %d 个分组未显示\n\n",
           result->group_count - max_groups_to_show);
  }
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
  printf("  分组文件数: %d 个\n", total_grouped_files);
  printf("  分组文件夹数: %d 个\n", total_grouped_dirs);
  printf("  分组总项数: %d 个\n", total_grouped_files + total_grouped_dirs);
  printf("\n");

  printf("大小统计:\n");
  char grouped_size_str[32], scanned_size_str[32], skipped_size_str[32];
  format_size(total_grouped_size, grouped_size_str, sizeof(grouped_size_str));
  format_size(total_scanned_size, scanned_size_str, sizeof(scanned_size_str));
  format_size(skipped_files_size, skipped_size_str, sizeof(skipped_size_str));

  printf("  分组总大小: %s (%.1f%%)\n", grouped_size_str, grouped_percent);
  printf("  扫描总大小: %s\n", scanned_size_str);
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
      calc_str[32], diff_str[32];
  format_size(input_total_size, input_str, sizeof(input_str));
  format_size(total_grouped_size, grouped_str, sizeof(grouped_str));
  format_size(skipped_files_size, skipped_str, sizeof(skipped_str));
  format_size(total_scanned_size, scanned_str, sizeof(scanned_str));
  format_size(calculated_total, calc_str, sizeof(calc_str));
  format_size(difference, diff_str, sizeof(diff_str));

  printf("  输入总大小: %s vs\n", input_str);
  printf("  分组总大小:(%s +\n", grouped_str);
  printf("  跳过大文件: %s =\n", skipped_str);
  printf("  计算总大小: %s)\n", calc_str);
  printf("（扫描总大小: %s）\n", scanned_str);
  printf("\n");

  if (calculated_total == input_total_size) {
    printf("  [成功] 验证成功: 没有文件遗漏\n");
  } else if (calculated_total < input_total_size) {
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

// 处理输入路径并生成分组结果
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size) {
  FileItem *items = (FileItem *)safe_malloc(sizeof(FileItem) * MAX_ITEMS);
  int item_count = 0;
  long long total_input_size = 0;

  *total_scanned_size = 0;
  *skipped_files_size = 0;

  printf("[开始] 正在扫描文件和文件夹...\n\n");

  for (int i = 0; i < path_count; i++) {
    process_input_path(paths[i], items, &item_count, &total_input_size,
                       total_scanned_size, skipped_files_size);
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
  GroupResult result = group_files(items, item_count);
  result.total_input_size = total_input_size;
  result.skipped_size = *skipped_files_size;

  free(items);
  return result;
}

// 修改命令执行部分，使用宽字符版本的命令
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

    // 使用宽字符构建命令
    wchar_t command_buffer[32768] = L"git add";
    size_t buffer_len = wcslen(command_buffer);
    int current_command_path_count = 0;

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
        printf("  执行命令: git add [%d个路径]\n", current_command_path_count);

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
      }

      // 添加路径到命令
      if (buffer_len + quoted_path_len < _countof(command_buffer)) {
        wcscat_s(command_buffer, _countof(command_buffer), quoted_path);
        buffer_len += quoted_path_len;
        current_command_path_count++;
      }

      free(wpath);
    }

    // 执行分组最后一个命令（如果有内容）
    if (current_command_path_count > 0) {
      printf("  执行命令: git add [%d个路径]\n", current_command_path_count);

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

// 修改后的run_grouping_test函数，增加Git操作
void run_grouping_test_with_git(char *paths[], int path_count,
                                const char *commit_info_file) {
  long long total_scanned_size, skipped_files_size;

  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size);

  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  // 执行Git操作
  execute_git_commands(&result, commit_info_file);

  free_group_result(&result);
}

// 主测试函数（不带Git功能）
void run_grouping_test(char *paths[], int path_count) {
  long long total_scanned_size, skipped_files_size;

  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size);

  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  free_group_result(&result);
}

// 从git status --porcelain获取文件路径（修复版）
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
    }

    // 现在处理路径，可能被引号包围
    char *final_path = path_start;

    // 如果路径以引号开始和结束，去除引号
    if (len >= 2 && path_start[0] == '"' && path_start[len - 1] == '"') {
      path_start[len - 1] = '\0';  // 去除结尾引号
      final_path = path_start + 1; // 跳过开头引号
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
      // 检查路径是否存在（可选，用于调试）
      DWORD attr = GetFileAttributesA(trimmed_path);
      if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("  [警告] 路径不存在或无法访问: '%s'\n", trimmed_path);
        // 继续添加，让后续处理决定如何处理
      }

      paths[*path_count] = (char *)safe_malloc(strlen(trimmed_path) + 1);
      strcpy(paths[*path_count], trimmed_path);
      (*path_count)++;

      printf("  [调试] 解析到文件: '%s'\n", trimmed_path);
    }
  }

  _pclose(pipe);

  printf("[Git] 找到 %d 个变更文件\n", *path_count);

  // 打印所有找到的文件用于调试
  if (*path_count > 0) {
    printf("[Git] 文件列表:\n");
    for (int i = 0; i < *path_count; i++) {
      printf("  %d. '%s'\n", i + 1, paths[i]);
    }
  }

  return paths;
}

// 释放git状态路径内存
void free_git_status_paths(char **paths, int path_count) {
  for (int i = 0; i < path_count; i++) {
    free(paths[i]);
  }
  free(paths);
}

int main(int argc, char *argv[]) {
  // 设置控制台输出为UTF-8
  SetConsoleOutputCP(CP_UTF8);

  printf("========================================\n");
  printf("          文件分组工具 (带Git功能)\n");
  printf("========================================\n\n");

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

  // 根据参数选择是否使用Git功能
  if (use_git) {
    run_grouping_test_with_git(input_paths, path_count, commit_info_file);
  } else {
    run_grouping_test(input_paths, path_count);
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
