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

        if (file_size.QuadPart > MAX_GROUP_SIZE) {
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

      if (file_size.QuadPart > MAX_GROUP_SIZE) {
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
      input_total_size > 0 ? (double)difference / input_total_size * 100
                           : 0;

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

// 主测试函数
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

int main() {
  // 设置控制台输出为UTF-8
  SetConsoleOutputCP(CP_UTF8);

  printf("========================================\n");
  printf("          文件分组工具\n");
  printf("========================================\n\n");

  // 硬编码的路径数组
  char *input_paths[] = {
      "C:\\Windows", // 这个会变
      //
      // "C:\\Users", // 这个会变
      //
      // "C:\\inetpub",
      // "C:\\DumpStack.log",
      // "C:\\OneDriveTemp",
      // "C:\\PerfLogs",
      // "C:\\ProgramFiles",
      // "C:\\ProgramFiles(x86)",
      // "C:\\ProgramData",
      //
      // "C:\\Recovery", // 这个会变
  };

  int path_count = sizeof(input_paths) / sizeof(input_paths[0]);

  printf("预设扫描路径:\n");
  for (int i = 0; i < path_count; i++) {
    printf("  %d. %s\n", i + 1, input_paths[i]);
  }
  printf("\n");

  run_grouping_test(input_paths, path_count);

  printf("\n[完成] 处理完成！\n");
  return 0;
}
