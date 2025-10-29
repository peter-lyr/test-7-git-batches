#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define BUFFER_SIZE (1024 * 1024)

void *safe_malloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    fprintf(stderr, "错误：内存分配失败（请求大小：%zu 字节）\n", size);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

char *ansi_to_utf8(const char *ansi_str) {
  if (!ansi_str)
    return NULL;
  int wide_len = MultiByteToWideChar(CP_ACP, 0, ansi_str, -1, NULL, 0);
  if (wide_len == 0)
    return NULL;
  wchar_t *wide_str = (wchar_t *)safe_malloc(wide_len * sizeof(wchar_t));
  if (MultiByteToWideChar(CP_ACP, 0, ansi_str, -1, wide_str, wide_len) == 0) {
    free(wide_str);
    return NULL;
  }
  int utf8_len =
      WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, NULL, 0, NULL, NULL);
  if (utf8_len == 0) {
    free(wide_str);
    return NULL;
  }
  char *utf8_str = (char *)safe_malloc(utf8_len);
  if (WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_str, utf8_len, NULL,
                          NULL) == 0) {
    free(wide_str);
    free(utf8_str);
    return NULL;
  }
  free(wide_str);
  return utf8_str;
}

wchar_t *char_to_wchar(const char *str) {
  if (!str)
    return NULL;
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

void remove_quotes(char *str) {
  if (!str || strlen(str) < 2)
    return;
  size_t len = strlen(str);
  if ((str[0] == '"' && str[len - 1] == '"') ||
      (str[0] == '\'' && str[len - 1] == '\'')) {
    memmove(str, str + 1, len - 2);
    str[len - 2] = '\0';
  }
}

int safe_path_join(wchar_t *dest, size_t dest_size, const wchar_t *path1,
                   const wchar_t *path2) {
  if (_snwprintf_s(dest, dest_size, _TRUNCATE, L"%s\\%s", path1, path2) < 0) {
    dest[dest_size - 1] = L'\0';
    return 0;
  }
  return 1;
}

void normalize_path(char *path) {
  if (!path || strlen(path) == 0)
    return;
  remove_quotes(path);
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

int create_directory_recursive(const wchar_t *wpath) {
  wchar_t temp_path[MAX_PATH_LENGTH];
  wchar_t *p = NULL;
  size_t len = wcslen(wpath);
  wcscpy_s(temp_path, MAX_PATH_LENGTH, wpath);
  for (p = temp_path + 1; *p; p++) {
    if (*p == L'\\') {
      *p = L'\0';
      DWORD attr = GetFileAttributesW(temp_path);
      if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(temp_path, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_ALREADY_EXISTS) {
            return 0;
          }
        }
      } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
      }
      *p = L'\\';
    }
  }
  DWORD attr = GetFileAttributesW(temp_path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryW(temp_path, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        return 0;
      }
    }
  }
  return 1;
}

typedef struct {
  wchar_t path[MAX_PATH_LENGTH];
  int part_number;
} PartFile;

int compare_part_files(const void *a, const void *b) {
  const PartFile *file1 = (const PartFile *)a;
  const PartFile *file2 = (const PartFile *)b;
  return file1->part_number - file2->part_number;
}

int get_part_files(const wchar_t *split_dir, PartFile **part_files,
                   int *file_count) {
  wchar_t search_pattern[MAX_PATH_LENGTH];
  if (!safe_path_join(search_pattern, MAX_PATH_LENGTH, split_dir, L"*")) {
    return 0;
  }
  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_pattern, &find_data);
  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }
  int capacity = 100;
  *part_files = (PartFile *)safe_malloc(sizeof(PartFile) * capacity);
  *file_count = 0;
  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    wchar_t *dot_pos = wcsrchr(find_data.cFileName, L'.');
    wchar_t *part_pos = wcsstr(find_data.cFileName, L"-part");
    if (dot_pos && part_pos && part_pos < dot_pos) {
      wchar_t number_str[5];
      wcsncpy_s(number_str, 5, part_pos + 5, 4);
      number_str[4] = L'\0';
      int part_number = _wtoi(number_str);
      if (part_number > 0) {
        if (*file_count >= capacity) {
          capacity *= 2;
          *part_files =
              (PartFile *)realloc(*part_files, sizeof(PartFile) * capacity);
          if (!*part_files) {
            FindClose(hFind);
            return 0;
          }
        }
        PartFile *current = &((*part_files)[*file_count]);
        safe_path_join(current->path, MAX_PATH_LENGTH, split_dir,
                       find_data.cFileName);
        current->part_number = part_number;
        (*file_count)++;
      }
    }
  } while (FindNextFileW(hFind, &find_data));
  FindClose(hFind);
  if (*file_count > 0) {
    qsort(*part_files, *file_count, sizeof(PartFile), compare_part_files);
  }
  return 1;
}

int get_merged_file_path(const wchar_t *split_dir, wchar_t *merged_path,
                         size_t merged_path_size) {
  wchar_t temp_path[MAX_PATH_LENGTH];
  wcscpy_s(temp_path, MAX_PATH_LENGTH, split_dir);
  wchar_t *split_suffix = wcsstr(temp_path, L"-split");
  if (!split_suffix) {
    return 0;
  }
  wchar_t original_path[MAX_PATH_LENGTH];
  wcsncpy_s(original_path, MAX_PATH_LENGTH, temp_path,
            split_suffix - temp_path);
  original_path[split_suffix - temp_path] = L'\0';
  wchar_t drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
  _wsplitpath_s(original_path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname,
                _MAX_FNAME, ext, _MAX_EXT);
  wchar_t merged_fname[_MAX_FNAME + 50];
  _snwprintf_s(merged_fname, _countof(merged_fname), _TRUNCATE, L"%s-merged",
               fname);
  _wmakepath_s(merged_path, merged_path_size, drive, dir, merged_fname, ext);
  return 1;
}

int merge_part_files(const wchar_t *split_dir, const wchar_t *output_file) {
  PartFile *part_files = NULL;
  int file_count = 0;
  if (!get_part_files(split_dir, &part_files, &file_count)) {
    printf("  ❌ 无法在目录中找到分块文件\n");
    return 0;
  }
  if (file_count == 0) {
    printf("  ❌ 在目录中未找到分块文件\n");
    free(part_files);
    return 0;
  }
  printf("  📁 找到 %d 个要合并的分块文件\n", file_count);
  wchar_t output_dir[MAX_PATH_LENGTH];
  wcscpy_s(output_dir, MAX_PATH_LENGTH, output_file);
  wchar_t *last_slash = wcsrchr(output_dir, L'\\');
  if (last_slash) {
    *last_slash = L'\0';
    if (!create_directory_recursive(output_dir)) {
      printf("  ⚠️  无法创建输出目录结构\n");
    }
  }
  HANDLE hOutput = CreateFileW(output_file, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hOutput == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    printf("  ❌ 无法创建输出文件（错误：%lu）\n", error);
    free(part_files);
    return 0;
  }
  BYTE *buffer = (BYTE *)safe_malloc(BUFFER_SIZE);
  int success = 1;
  long long total_written = 0;
  for (int i = 0; i < file_count && success; i++) {
    char *part_path_char = wchar_to_char(part_files[i].path);
    printf("  🔄 正在合并分块 %d/%d：%s\n", i + 1, file_count,
           part_path_char ? part_path_char : "[无法显示路径]");
    if (part_path_char)
      free(part_path_char);
    HANDLE hPart =
        CreateFileW(part_files[i].path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPart == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      printf("  ❌ 无法打开分块文件（错误：%lu）\n", error);
      success = 0;
      continue;
    }
    DWORD bytes_read, bytes_written;
    while (ReadFile(hPart, buffer, BUFFER_SIZE, &bytes_read, NULL) &&
           bytes_read > 0) {
      if (!WriteFile(hOutput, buffer, bytes_read, &bytes_written, NULL) ||
          bytes_written != bytes_read) {
        printf("  ❌ 写入分块文件失败\n");
        success = 0;
        break;
      }
      total_written += bytes_written;
    }
    CloseHandle(hPart);
    if (success) {
      printf("  ✅ 成功合并分块 %d\n", part_files[i].part_number);
    }
  }
  free(buffer);
  CloseHandle(hOutput);
  free(part_files);
  if (success) {
    printf("  ✅ 合并完成\n");
    printf("  📊 总写入字节数：%lld\n", total_written);
    HANDLE hVerify =
        CreateFileW(output_file, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hVerify != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER file_size;
      if (GetFileSizeEx(hVerify, &file_size)) {
        printf("  📁 输出文件大小：%lld 字节\n", file_size.QuadPart);
      }
      CloseHandle(hVerify);
    }
  } else {
    printf("  ❌ 合并失败\n");
    DeleteFileW(output_file);
  }
  return success;
}

int validate_split_directory(const wchar_t *split_dir) {
  DWORD attr = GetFileAttributesW(split_dir);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    char *path_char = wchar_to_char(split_dir);
    printf("  ❌ 目录不存在或无法访问：%s\n",
           path_char ? path_char : "[未知路径]");
    if (path_char)
      free(path_char);
    return 0;
  }
  if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    char *path_char = wchar_to_char(split_dir);
    printf("  ❌ 路径存在但不是目录：%s\n",
           path_char ? path_char : "[未知路径]");
    if (path_char)
      free(path_char);
    return 0;
  }
  return 1;
}

int get_current_directory(wchar_t *buffer, size_t buffer_size) {
  DWORD len = GetCurrentDirectoryW(buffer_size, buffer);
  return (len > 0 && len < buffer_size);
}

int find_split_directories_recursive(const wchar_t *search_dir,
                                     wchar_t ***dir_list, int *dir_count,
                                     int *capacity) {
  wchar_t search_pattern[MAX_PATH_LENGTH];
  if (!safe_path_join(search_pattern, MAX_PATH_LENGTH, search_dir, L"*")) {
    return 0;
  }
  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_pattern, &find_data);
  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }
  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      wchar_t full_path[MAX_PATH_LENGTH];
      safe_path_join(full_path, MAX_PATH_LENGTH, search_dir,
                     find_data.cFileName);
      wchar_t *split_pos = wcsstr(find_data.cFileName, L"-split");
      if (split_pos && wcslen(split_pos) == 6) {
        if (*dir_count >= *capacity) {
          *capacity *= 2;
          *dir_list =
              (wchar_t **)realloc(*dir_list, sizeof(wchar_t *) * (*capacity));
          if (!*dir_list) {
            FindClose(hFind);
            return 0;
          }
        }
        (*dir_list)[*dir_count] = _wcsdup(full_path);
        if (!(*dir_list)[*dir_count]) {
          FindClose(hFind);
          return 0;
        }
        (*dir_count)++;
      } else {
        find_split_directories_recursive(full_path, dir_list, dir_count,
                                         capacity);
      }
    }
  } while (FindNextFileW(hFind, &find_data));
  FindClose(hFind);
  return 1;
}

int find_all_split_directories(const wchar_t *search_dir, wchar_t ***dir_list,
                               int *dir_count) {
  int capacity = 50;
  *dir_list = (wchar_t **)safe_malloc(sizeof(wchar_t *) * capacity);
  *dir_count = 0;
  return find_split_directories_recursive(search_dir, dir_list, dir_count,
                                          &capacity);
}

void print_usage(const char *program_name) {
  printf("使用方法：\n");
  printf("  %s [分割目录1] [分割目录2] ...\n", program_name);
  printf("\n示例：\n");
  printf("  %s\n", program_name);
  printf("    - 自动查找并合并当前文件夹及其子文件夹中所有 '-split' 目录\n\n");
  printf("  %s \"C:\\路径\\到\\大文件.zip-split\"\n", program_name);
  printf("    - 合并指定的分割目录\n\n");
  printf("  %s \"文件夹1-split\" \"文件夹2-split\" \"文件夹3-split\"\n",
         program_name);
  printf("    - 合并多个指定的分割目录\n\n");
  printf("输出文件将在相同位置创建，并带有 '-merged' 后缀\n");
}

int process_single_directory(const wchar_t *split_dir) {
  wchar_t absolute_path[MAX_PATH_LENGTH];
  if (GetFullPathNameW(split_dir, MAX_PATH_LENGTH, absolute_path, NULL) == 0) {
    wcscpy_s(absolute_path, MAX_PATH_LENGTH, split_dir);
  }
  char *absolute_path_char = wchar_to_char(absolute_path);
  printf("📁 正在处理：%s\n",
         absolute_path_char ? absolute_path_char : "[无法显示]");
  if (absolute_path_char)
    free(absolute_path_char);
  if (!validate_split_directory(absolute_path)) {
    printf("  ❌ 无效的分割目录\n\n");
    return 0;
  }
  wchar_t merged_file_path[MAX_PATH_LENGTH];
  if (!get_merged_file_path(absolute_path, merged_file_path, MAX_PATH_LENGTH)) {
    printf("  ❌ 无法确定输出文件路径\n");
    printf("  ℹ️  请确保目录名以 '-split' 结尾\n\n");
    return 0;
  }
  char *merged_file_path_char = wchar_to_char(merged_file_path);
  if (merged_file_path_char) {
    printf("  💾 输出：%s\n", merged_file_path_char);
    free(merged_file_path_char);
  }
  int result = merge_part_files(absolute_path, merged_file_path);
  printf("\n");
  return result;
}

int main(int argc, char *argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  printf("========================================\n");
  printf("          文件合并工具\n");
  printf("========================================\n\n");
  int total_processed = 0;
  int successful_merges = 0;
  if (argc == 1) {
    printf("🔄 未提供参数，正在递归搜索所有 '-split' 目录...\n\n");
    wchar_t current_dir[MAX_PATH_LENGTH];
    if (!get_current_directory(current_dir, MAX_PATH_LENGTH)) {
      printf("❌ 无法获取当前目录\n");
      return 1;
    }
    char *current_dir_char = wchar_to_char(current_dir);
    printf("📂 当前目录：%s\n\n",
           current_dir_char ? current_dir_char : "[无法显示]");
    if (current_dir_char)
      free(current_dir_char);
    wchar_t **split_dirs = NULL;
    int dir_count = 0;
    if (!find_all_split_directories(current_dir, &split_dirs, &dir_count)) {
      printf("❌ 搜索分割目录时出错\n");
      return 1;
    }
    if (dir_count == 0) {
      printf("❌ 在当前文件夹及其子文件夹中未找到 '-split' 目录\n");
      printf("ℹ️  请手动指定目录或在包含分割文件的文件夹中运行\n\n");
      print_usage(argv[0]);
      return 1;
    }
    printf("🎯 找到 %d 个分割目录：\n", dir_count);
    for (int i = 0; i < dir_count; i++) {
      char *dir_char = wchar_to_char(split_dirs[i]);
      printf("  %d. %s\n", i + 1, dir_char ? dir_char : "[无法显示]");
      if (dir_char)
        free(dir_char);
    }
    printf("\n");
    for (int i = 0; i < dir_count; i++) {
      total_processed++;
      if (process_single_directory(split_dirs[i])) {
        successful_merges++;
      }
      free(split_dirs[i]);
    }
    free(split_dirs);
  } else {
    for (int i = 1; i < argc; i++) {
      char *utf8_path = ansi_to_utf8(argv[i]);
      if (!utf8_path) {
        printf("❌ 转换参数编码失败：%s\n", argv[i]);
        continue;
      }
      char input_path[MAX_PATH_LENGTH];
      strcpy_s(input_path, MAX_PATH_LENGTH, utf8_path);
      normalize_path(input_path);
      wchar_t *split_dir = char_to_wchar(input_path);
      if (!split_dir) {
        printf("❌ 转换路径为宽字符串失败：%s\n", input_path);
        free(utf8_path);
        continue;
      }
      total_processed++;
      if (process_single_directory(split_dir)) {
        successful_merges++;
      }
      free(split_dir);
      free(utf8_path);
    }
  }
  printf("========================================\n");
  printf("             汇总\n");
  printf("========================================\n");
  printf("📊 已处理目录数：%d\n", total_processed);
  printf("✅ 成功合并数：%d\n", successful_merges);
  printf("❌ 合并失败数：%d\n", total_processed - successful_merges);
  if (successful_merges == total_processed && total_processed > 0) {
    printf("\n🎉 所有合并均成功完成！\n");
    return 0;
  } else if (successful_merges > 0) {
    printf("\n⚠️  部分合并完成但出现错误\n");
    return 1;
  } else {
    printf("\n💥 所有合并均失败！\n");
    return 1;
  }
}
