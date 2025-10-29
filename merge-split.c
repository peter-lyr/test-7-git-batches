#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define BUFFER_SIZE (1024 * 1024) // 1MB缓冲区

// 安全内存分配函数
void *safe_malloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    fprintf(stderr, "错误: 内存分配失败 (申请大小: %zu 字节)\n", size);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

// 路径规范化
void normalize_path(char *path) {
  if (!path || strlen(path) == 0)
    return;

  for (char *p = path; *p; p++) {
    if (*p == '/')
      *p = '\\';
  }

  size_t len = strlen(path);
  while (len > 1 && path[len - 1] == '\\') {
    if (len == 3 && path[1] == ':' && path[2] == '\\')
      break;
    if (len == 2 && path[0] == '\\' && path[1] == '\\')
      break;
    path[len - 1] = '\0';
    len--;
  }
}

// 检查文件是否存在
int file_exists(const char *path) {
  DWORD attr = GetFileAttributesA(path);
  return (attr != INVALID_FILE_ATTRIBUTES &&
          !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// 检查目录是否存在
int directory_exists(const char *path) {
  DWORD attr = GetFileAttributesA(path);
  return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

// 获取文件大小
long long get_file_size(const char *filename) {
  WIN32_FILE_ATTRIBUTE_DATA fileInfo;
  if (GetFileAttributesExA(filename, GetFileExInfoStandard, &fileInfo)) {
    ULARGE_INTEGER size;
    size.LowPart = fileInfo.nFileSizeLow;
    size.HighPart = fileInfo.nFileSizeHigh;
    return size.QuadPart;
  }
  return -1;
}

// 创建目录（递归）
int create_directory_recursive(const char *path) {
  char temp_path[MAX_PATH_LENGTH];
  char *p = NULL;
  size_t len = strlen(path);

  strcpy_s(temp_path, MAX_PATH_LENGTH, path);

  for (p = temp_path + 1; *p; p++) {
    if (*p == '\\') {
      *p = '\0';
      if (!directory_exists(temp_path)) {
        if (!CreateDirectoryA(temp_path, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_ALREADY_EXISTS) {
            printf("[错误] 无法创建目录: %s (错误: %lu)\n", temp_path, error);
            return 0;
          }
        }
      }
      *p = '\\';
    }
  }

  if (!directory_exists(temp_path)) {
    if (!CreateDirectoryA(temp_path, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        printf("[错误] 无法创建目录: %s (错误: %lu)\n", temp_path, error);
        return 0;
      }
    }
  }
  return 1;
}

// 合并拆分文件
int merge_split_files(const char *split_dir, const char *output_file) {
  printf("\n========================================\n");
  printf("          开始合并拆分文件\n");
  printf("========================================\n\n");

  printf("拆分目录: %s\n", split_dir);
  printf("输出文件: %s\n", output_file);

  // 检查拆分目录是否存在
  if (!directory_exists(split_dir)) {
    printf("[错误] 拆分目录不存在: %s\n", split_dir);
    return 0;
  }

  // 查找所有拆分文件
  char search_pattern[MAX_PATH_LENGTH];
  snprintf(search_pattern, MAX_PATH_LENGTH, "%s\\*", split_dir);

  WIN32_FIND_DATAA find_data;
  HANDLE hFind = FindFirstFileA(search_pattern, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    printf("[错误] 无法访问拆分目录: %s\n", split_dir);
    return 0;
  }

  // 统计拆分文件
  typedef struct {
    char path[MAX_PATH_LENGTH];
    int part_num;
    long long size;
  } SplitFile;

  SplitFile *split_files = (SplitFile *)safe_malloc(sizeof(SplitFile) * 1000);
  int split_count = 0;
  long long total_size = 0;

  printf("\n正在扫描拆分文件...\n");

  do {
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue; // 跳过目录
    }

    char *filename = find_data.cFileName;

    // 检查文件名是否符合拆分文件命名模式 (原文件名-partXXXX.原扩展名)
    char *part_str = strstr(filename, "-part");
    if (!part_str) {
      continue; // 不是拆分文件
    }

    // 提取部分序号
    char num_str[5] = {0};
    strncpy_s(num_str, 5, part_str + 5, 4);

    // 验证是否是数字
    int is_number = 1;
    for (int i = 0; i < 4; i++) {
      if (num_str[i] < '0' || num_str[i] > '9') {
        is_number = 0;
        break;
      }
    }

    if (!is_number) {
      continue;
    }

    int part_num = atoi(num_str);
    if (part_num <= 0) {
      continue;
    }

    // 构建完整路径
    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, MAX_PATH_LENGTH, "%s\\%s", split_dir, filename);

    // 获取文件大小
    long long file_size = get_file_size(full_path);
    if (file_size <= 0) {
      printf("[警告] 无法获取文件大小: %s\n", full_path);
      continue;
    }

    // 保存拆分文件信息
    if (split_count < 1000) {
      strcpy_s(split_files[split_count].path, MAX_PATH_LENGTH, full_path);
      split_files[split_count].part_num = part_num;
      split_files[split_count].size = file_size;
      split_count++;
      total_size += file_size;

      printf("  找到拆分文件: %s (部分 %d, 大小: %lld bytes)\n", filename,
             part_num, file_size);
    }

  } while (FindNextFileA(hFind, &find_data) && split_count < 1000);

  FindClose(hFind);

  if (split_count == 0) {
    printf("[错误] 在目录中没有找到有效的拆分文件: %s\n", split_dir);
    free(split_files);
    return 0;
  }

  printf("\n共找到 %d 个拆分文件，总大小: %lld bytes\n", split_count,
         total_size);

  // 按部分序号排序
  printf("正在对拆分文件排序...\n");
  for (int i = 0; i < split_count - 1; i++) {
    for (int j = i + 1; j < split_count; j++) {
      if (split_files[i].part_num > split_files[j].part_num) {
        SplitFile temp = split_files[i];
        split_files[i] = split_files[j];
        split_files[j] = temp;
      }
    }
  }

  // 验证序号连续性
  int missing_parts = 0;
  for (int i = 0; i < split_count; i++) {
    if (split_files[i].part_num != i + 1) {
      printf("[警告] 序号不连续: 期望 %d, 实际 %d\n", i + 1,
             split_files[i].part_num);
      missing_parts = 1;
    }
  }

  if (missing_parts) {
    printf("[警告] 发现不连续的序号，但将继续合并\n");
  }

  // 创建输出目录（如果需要）
  char output_dir[MAX_PATH_LENGTH];
  strcpy_s(output_dir, MAX_PATH_LENGTH, output_file);
  char *last_slash = strrchr(output_dir, '\\');
  if (last_slash) {
    *last_slash = '\0';
    if (!create_directory_recursive(output_dir)) {
      printf("[错误] 无法创建输出目录: %s\n", output_dir);
      free(split_files);
      return 0;
    }
  }

  // 合并文件
  printf("\n开始合并文件...\n");
  FILE *output_fp = fopen(output_file, "wb");
  if (!output_fp) {
    printf("[错误] 无法创建输出文件: %s\n", output_file);
    free(split_files);
    return 0;
  }

  unsigned char *buffer = (unsigned char *)safe_malloc(BUFFER_SIZE);
  long long merged_size = 0;
  int success_count = 0;

  for (int i = 0; i < split_count; i++) {
    printf("  正在合并部分 %d/%d: %s\n", i + 1, split_count,
           split_files[i].path);

    FILE *input_fp = fopen(split_files[i].path, "rb");
    if (!input_fp) {
      printf("    [错误] 无法打开拆分文件: %s\n", split_files[i].path);
      continue;
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, input_fp)) > 0) {
      size_t bytes_written = fwrite(buffer, 1, bytes_read, output_fp);
      if (bytes_written != bytes_read) {
        printf("    [错误] 写入输出文件失败\n");
        fclose(input_fp);
        fclose(output_fp);
        free(buffer);
        free(split_files);
        return 0;
      }
      merged_size += bytes_written;
    }

    fclose(input_fp);
    success_count++;

    // 显示进度
    double progress = (double)(i + 1) / split_count * 100;
    printf("    进度: %d/%d (%.1f%%) - 已合并: %lld bytes\n", i + 1,
           split_count, progress, merged_size);
  }

  fclose(output_fp);
  free(buffer);
  free(split_files);

  // 验证合并结果
  long long final_size = get_file_size(output_file);
  printf("\n合并完成:\n");
  printf("  成功合并: %d/%d 个部分\n", success_count, split_count);
  printf("  预期大小: %lld bytes\n", total_size);
  printf("  实际大小: %lld bytes\n", final_size);

  if (final_size == total_size) {
    printf("  [成功] 文件大小验证通过！\n");
  } else {
    printf("  [警告] 文件大小不匹配！差异: %lld bytes\n",
           llabs(final_size - total_size));
  }

  printf("  输出文件: %s\n", output_file);

  return (success_count == split_count) ? 1 : 0;
}

// 从拆分目录路径推断原文件名
int infer_original_filename(const char *split_dir, char *original_name,
                            char *extension) {
  // 拆分目录格式: 原文件路径-split
  char temp_path[MAX_PATH_LENGTH];
  strcpy_s(temp_path, MAX_PATH_LENGTH, split_dir);

  // 移除末尾的 "-split"
  char *split_suffix = strstr(temp_path, "-split");
  if (!split_suffix) {
    return 0;
  }

  *split_suffix = '\0'; // 截断 "-split" 部分

  // 现在 temp_path 包含原文件路径
  // 提取文件名和扩展名
  char *last_slash = strrchr(temp_path, '\\');
  char *filename = last_slash ? last_slash + 1 : temp_path;

  char *dot = strrchr(filename, '.');
  if (dot) {
    // 有扩展名
    strcpy_s(extension, MAX_PATH_LENGTH, dot); // 包含点号
    *dot = '\0';                               // 截断扩展名
  } else {
    extension[0] = '\0';
  }

  strcpy_s(original_name, MAX_PATH_LENGTH, filename);
  return 1;
}

// 处理单个拆分目录
int process_split_directory(const char *split_dir) {
  printf("\n处理拆分目录: %s\n", split_dir);

  char original_name[MAX_PATH_LENGTH];
  char extension[MAX_PATH_LENGTH];

  if (!infer_original_filename(split_dir, original_name, extension)) {
    printf("[错误] 无法从拆分目录推断原文件名: %s\n", split_dir);
    return 0;
  }

  printf("  推断的原文件名: %s\n", original_name);
  printf("  推断的扩展名: %s\n", extension);

  // 构建输出文件路径
  char output_file[MAX_PATH_LENGTH];
  char *last_slash = strrchr(split_dir, '\\');
  if (last_slash) {
    // 在原文件所在目录创建合并文件
    size_t dir_len = last_slash - split_dir + 1;
    strncpy_s(output_file, MAX_PATH_LENGTH, split_dir, dir_len);
    output_file[dir_len] = '\0';
  } else {
    output_file[0] = '\0';
  }

  // 构建合并文件名: 原文件名-merged.扩展名
  snprintf(output_file + strlen(output_file),
           MAX_PATH_LENGTH - strlen(output_file), "%s-merged%s", original_name,
           extension);

  normalize_path(output_file);

  printf("  输出文件路径: %s\n", output_file);

  // 检查输出文件是否已存在
  if (file_exists(output_file)) {
    printf("[警告] 输出文件已存在: %s\n", output_file);
    printf("是否覆盖？(y/n): ");
    char response[10];
    if (fgets(response, sizeof(response), stdin)) {
      if (response[0] != 'y' && response[0] != 'Y') {
        printf("  跳过合并\n");
        return 0;
      }
    }
  }

  return merge_split_files(split_dir, output_file);
}

// 递归查找所有拆分目录
void find_split_directories(const char *search_path, char **split_dirs,
                            int *count) {
  char search_pattern[MAX_PATH_LENGTH];
  snprintf(search_pattern, MAX_PATH_LENGTH, "%s\\*", search_path);

  WIN32_FIND_DATAA find_data;
  HANDLE hFind = FindFirstFileA(search_pattern, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (strcmp(find_data.cFileName, ".") == 0 ||
        strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, MAX_PATH_LENGTH, "%s\\%s", search_path,
             find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // 检查是否是拆分目录（以"-split"结尾）
      if (strstr(find_data.cFileName, "-split") != NULL) {
        if (*count < 1000) {
          strcpy_s(split_dirs[*count], MAX_PATH_LENGTH, full_path);
          (*count)++;
          printf("找到拆分目录: %s\n", full_path);
        }
      }

      // 递归搜索子目录
      find_split_directories(full_path, split_dirs, count);
    }

  } while (FindNextFileA(hFind, &find_data) && *count < 1000);

  FindClose(hFind);
}

// 主函数
int main(int argc, char *argv[]) {
  SetConsoleOutputCP(CP_UTF8);

  printf("========================================\n");
  printf("          拆分文件合并工具\n");
  printf("========================================\n\n");

  if (argc < 2) {
    printf("用法: %s [拆分目录路径] 或 %s -auto\n", argv[0], argv[0]);
    printf("  拆分目录路径: 包含拆分文件的目录路径\n");
    printf("  -auto: 自动搜索当前目录下的所有拆分目录\n\n");
    printf("示例:\n");
    printf("  %s C:\\path\\to\\bigfile-split\n", argv[0]);
    printf("  %s -auto\n", argv[0]);
    return 1;
  }

  int success_count = 0;
  int total_count = 0;

  if (strcmp(argv[1], "-auto") == 0) {
    printf("[模式] 自动搜索模式\n");
    printf("正在搜索当前目录下的所有拆分目录...\n\n");

    // 分配内存存储拆分目录路径
    char **split_dirs = (char **)safe_malloc(sizeof(char *) * 1000);
    for (int i = 0; i < 1000; i++) {
      split_dirs[i] = (char *)safe_malloc(MAX_PATH_LENGTH);
    }

    int dir_count = 0;
    find_split_directories(".", split_dirs, &dir_count);

    if (dir_count == 0) {
      printf("未找到任何拆分目录\n");
    } else {
      printf("\n共找到 %d 个拆分目录，开始合并...\n", dir_count);

      for (int i = 0; i < dir_count; i++) {
        printf("\n[%d/%d] ", i + 1, dir_count);
        if (process_split_directory(split_dirs[i])) {
          success_count++;
        }
        total_count++;
        printf("\n");
      }
    }

    // 释放内存
    for (int i = 0; i < 1000; i++) {
      free(split_dirs[i]);
    }
    free(split_dirs);

  } else {
    // 处理指定的拆分目录
    char split_dir[MAX_PATH_LENGTH];
    strcpy_s(split_dir, MAX_PATH_LENGTH, argv[1]);
    normalize_path(split_dir);

    if (process_split_directory(split_dir)) {
      success_count = 1;
    }
    total_count = 1;
  }

  printf("\n========================================\n");
  printf("              合并完成\n");
  printf("========================================\n\n");
  printf("统计:\n");
  printf("  总处理: %d 个拆分目录\n", total_count);
  printf("  成功: %d 个\n", success_count);
  printf("  失败: %d 个\n", total_count - success_count);

  if (success_count > 0) {
    printf("\n[成功] 文件合并完成！\n");
    printf("合并后的文件命名为: 原文件名-merged.原扩展名\n");
  } else {
    printf("\n[失败] 没有成功合并任何文件\n");
  }

  return (success_count > 0) ? 0 : 1;
}
