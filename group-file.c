#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define MAX_GROUP_SIZE (100 * 1024 * 1024LL) // 100MB
#define MAX_ITEMS 100000

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
  long long total_input_size;
  long long skipped_size;
  int total_files;
  int total_directories;
} GroupResult;

// 字符编码转换函数
wchar_t *char_to_wchar(const char *str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t *wstr = (wchar_t *)malloc(len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr;
}

char *wchar_to_char(const wchar_t *wstr) {
  int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  char *str = (char *)malloc(len);
  WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
  return str;
}

// 递归计算文件夹大小
long long calculate_directory_size(const wchar_t *wpath,
                                   long long *total_scanned_size) {
  long long total_size = 0;
  wchar_t search_path[MAX_PATH_LENGTH];
  _snwprintf_s(search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*", wpath);

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t full_path[MAX_PATH_LENGTH];
    _snwprintf_s(full_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s", wpath,
                 find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      total_size += calculate_directory_size(full_path, total_scanned_size);
    } else {
      long long file_size =
          ((ULONGLONG)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
      total_size += file_size;
      *total_scanned_size += file_size;
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);
  return total_size;
}

// 递归收集文件和文件夹信息
void collect_items_recursive(const wchar_t *wpath, FileItem *items,
                             int *item_count, int is_root,
                             long long *total_scanned_size,
                             long long *skipped_files_size) {
  wchar_t search_path[MAX_PATH_LENGTH];
  _snwprintf_s(search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*", wpath);

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
    _snwprintf_s(full_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s", wpath,
                 find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // 计算文件夹大小
      long long dir_size =
          calculate_directory_size(full_path, total_scanned_size);

      if (dir_size <= MAX_GROUP_SIZE) {
        // 文件夹大小合适，添加到列表
        if (*item_count < MAX_ITEMS) {
          char *char_path = wchar_to_char(full_path);
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
          items[*item_count].size = dir_size;
          items[*item_count].type = TYPE_DIRECTORY;
          (*item_count)++;
          free(char_path);
        }
      } else {
        // 文件夹太大，递归处理子项
        collect_items_recursive(full_path, items, item_count, 0,
                                total_scanned_size, skipped_files_size);
      }
    } else {
      // 处理文件
      long long file_size =
          ((ULONGLONG)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
      *total_scanned_size += file_size;

      if (file_size > MAX_GROUP_SIZE) {
        char *char_path = wchar_to_char(full_path);
        printf("跳过大文件: %s (%.2f MB)\n", char_path,
               file_size / (1024.0 * 1024.0));
        *skipped_files_size += file_size;
        free(char_path);
      } else {
        // 文件大小合适，添加到列表
        if (*item_count < MAX_ITEMS) {
          char *char_path = wchar_to_char(full_path);
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
          items[*item_count].size = file_size;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;
          free(char_path);
        }
      }
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);

  // 如果是根目录且文件夹大小合适，添加根文件夹本身
  if (is_root) {
    long long root_size = calculate_directory_size(wpath, total_scanned_size);
    if (root_size <= MAX_GROUP_SIZE && *item_count < MAX_ITEMS) {
      // 检查是否已经添加（可能在递归过程中添加了）
      int already_added = 0;
      char *root_char_path = wchar_to_char(wpath);

      for (int i = 0; i < *item_count; i++) {
        if (strcmp(items[i].path, root_char_path) == 0) {
          already_added = 1;
          break;
        }
      }

      if (!already_added) {
        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, root_char_path);
        items[*item_count].size = root_size;
        items[*item_count].type = TYPE_DIRECTORY;
        (*item_count)++;
      }
      free(root_char_path);
    }
  }
}

// 处理输入路径
void process_input_path(const char *path, FileItem *items, int *item_count,
                        long long *total_input_size,
                        long long *total_scanned_size,
                        long long *skipped_files_size) {
  wchar_t *wpath = char_to_wchar(path);

  DWORD attr = GetFileAttributesW(wpath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    printf("警告: 无法访问路径 %s\n", path);
    free(wpath);
    return;
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    printf("扫描文件夹: %s\n", path);
    long long dir_size = calculate_directory_size(wpath, total_scanned_size);
    *total_input_size += dir_size;

    printf("文件夹 %s 总大小: %.2f MB\n", path, dir_size / (1024.0 * 1024.0));

    if (dir_size > MAX_GROUP_SIZE) {
      printf("文件夹太大，递归处理子项...\n");
    } else {
      printf("文件夹大小合适，直接添加...\n");
    }

    // 总是递归收集，但如果是小文件夹会在collect_items_recursive中添加整个文件夹
    collect_items_recursive(wpath, items, item_count, 1, total_scanned_size,
                            skipped_files_size);
  } else {
    // 处理文件
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
      DWORD sizeLow, sizeHigh;
      sizeLow = GetFileSize(hFile, &sizeHigh);
      long long file_size = ((ULONGLONG)sizeHigh << 32) | sizeLow;
      CloseHandle(hFile);

      *total_input_size += file_size;
      *total_scanned_size += file_size;

      if (file_size > MAX_GROUP_SIZE) {
        printf("跳过大文件: %s (%.2f MB)\n", path,
               file_size / (1024.0 * 1024.0));
        *skipped_files_size += file_size;
      } else {
        if (*item_count < MAX_ITEMS) {
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, path);
          items[*item_count].size = file_size;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;
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

// 改进的分组算法
GroupResult group_files(FileItem *items, int item_count) {
  GroupResult result = {0};

  // 为分组分配合理的内存
  int initial_group_count = (item_count / 100) + 10; // 预估分组数
  result.groups = malloc(sizeof(FileGroup) * initial_group_count);
  result.group_count = 0;

  // 按类型和大小排序
  printf("正在排序 %d 个项...\n", item_count);
  qsort(items, item_count, sizeof(FileItem), compare_items);

  // 初始化分组
  for (int i = 0; i < initial_group_count; i++) {
    result.groups[i].items = malloc(sizeof(FileItem) * 1000); // 合理的初始容量
    result.groups[i].count = 0;
    result.groups[i].capacity = 1000;
    result.groups[i].total_size = 0;
  }

  printf("开始分组处理...\n");

  // 改进的最佳适应算法 - 添加进度显示
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

      // 检查是否需要扩展容量
      if (group->count >= group->capacity) {
        group->capacity *= 2;
        group->items =
            realloc(group->items, sizeof(FileItem) * group->capacity);
      }

      group->items[group->count++] = items[i];
      group->total_size += items[i].size;
    } else {
      // 创建新分组
      if (result.group_count >= initial_group_count) {
        // 需要扩展分组数组
        initial_group_count *= 2;
        result.groups =
            realloc(result.groups, sizeof(FileGroup) * initial_group_count);

        // 初始化新分配的分组
        for (int k = result.group_count; k < initial_group_count; k++) {
          result.groups[k].items = malloc(sizeof(FileItem) * 1000);
          result.groups[k].count = 0;
          result.groups[k].capacity = 1000;
          result.groups[k].total_size = 0;
        }
      }

      FileGroup *new_group = &result.groups[result.group_count];
      new_group->items[new_group->count++] = items[i];
      new_group->total_size += items[i].size;
      result.group_count++;
    }
  }

  printf("分组完成，共 %d 个分组\n", result.group_count);
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
    printf("calculated_total:%lld - input_total_size:%lld = %lld\n",
           calculated_total, input_total_size,
           calculated_total - input_total_size);
  }
}

// 释放内存
void free_group_result(GroupResult *result) {
  for (int i = 0; i < result->group_count; i++) {
    free(result->groups[i].items);
  }
  free(result->groups);
}

// 新封装的函数：处理输入路径并生成分组结果
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size) {
  // 收集所有文件和文件夹信息
  FileItem *items = malloc(sizeof(FileItem) * MAX_ITEMS);
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
  char *input_paths[] = {"C:\\Users\\depei_liu"};

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
