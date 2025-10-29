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
    fprintf(stderr,
            "Error: Memory allocation failed (requested size: %zu bytes)\n",
            size);
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
    printf("Error: Could not find part files in directory\n");
    return 0;
  }
  if (file_count == 0) {
    printf("Error: No part files found in directory\n");
    free(part_files);
    return 0;
  }
  printf("Found %d part files to merge\n", file_count);
  wchar_t output_dir[MAX_PATH_LENGTH];
  wcscpy_s(output_dir, MAX_PATH_LENGTH, output_file);
  wchar_t *last_slash = wcsrchr(output_dir, L'\\');
  if (last_slash) {
    *last_slash = L'\0';
    if (!create_directory_recursive(output_dir)) {
      printf("Warning: Could not create output directory structure\n");
    }
  }
  HANDLE hOutput = CreateFileW(output_file, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hOutput == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    printf("Error: Could not create output file (error: %lu)\n", error);
    free(part_files);
    return 0;
  }
  BYTE *buffer = (BYTE *)safe_malloc(BUFFER_SIZE);
  int success = 1;
  long long total_written = 0;
  for (int i = 0; i < file_count && success; i++) {
    char *part_path_char = wchar_to_char(part_files[i].path);
    printf("Merging part %d/%d: %s\n", i + 1, file_count,
           part_path_char ? part_path_char : "[Unable to display path]");
    if (part_path_char)
      free(part_path_char);
    HANDLE hPart =
        CreateFileW(part_files[i].path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPart == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      printf("Error: Could not open part file (error: %lu)\n", error);
      success = 0;
      continue;
    }
    DWORD bytes_read, bytes_written;
    while (ReadFile(hPart, buffer, BUFFER_SIZE, &bytes_read, NULL) &&
           bytes_read > 0) {
      if (!WriteFile(hOutput, buffer, bytes_read, &bytes_written, NULL) ||
          bytes_written != bytes_read) {
        printf("Error: Write failed for part file\n");
        success = 0;
        break;
      }
      total_written += bytes_written;
    }
    CloseHandle(hPart);
    if (success) {
      printf("  Successfully merged part %d\n", part_files[i].part_number);
    }
  }
  free(buffer);
  CloseHandle(hOutput);
  free(part_files);
  if (success) {
    printf("Merge completed successfully\n");
    printf("Total bytes written: %lld\n", total_written);
    HANDLE hVerify =
        CreateFileW(output_file, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hVerify != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER file_size;
      if (GetFileSizeEx(hVerify, &file_size)) {
        printf("Output file size: %lld bytes\n", file_size.QuadPart);
      }
      CloseHandle(hVerify);
    }
  } else {
    printf("Merge failed\n");
    DeleteFileW(output_file);
  }
  return success;
}

int validate_split_directory(const wchar_t *split_dir) {
  DWORD attr = GetFileAttributesW(split_dir);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    char *path_char = wchar_to_char(split_dir);
    printf("Directory does not exist or cannot be accessed: %s\n",
           path_char ? path_char : "[Unknown path]");
    if (path_char)
      free(path_char);
    return 0;
  }
  if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    char *path_char = wchar_to_char(split_dir);
    printf("Path exists but is not a directory: %s\n",
           path_char ? path_char : "[Unknown path]");
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

int main(int argc, char *argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  printf("========================================\n");
  printf("          File Merge Tool\n");
  printf("========================================\n\n");
  if (argc != 2) {
    printf("Usage: %s <split_directory>\n", argv[0]);
    printf("\nExamples:\n");
    printf("  %s \"C:\\path\\to\\largefile.zip-split\"\n", argv[0]);
    printf("  %s \"relative\\path\\file-split\"\n", argv[0]);
    printf("  %s \".\\folder with spaces-split\"\n", argv[0]);
    printf("\nThis will create: \"C:\\path\\to\\largefile-merged.zip\"\n");
    return 1;
  }
  char *utf8_path = ansi_to_utf8(argv[1]);
  if (!utf8_path) {
    printf("Error: Failed to convert argument encoding\n");
    return 1;
  }
  char input_path[MAX_PATH_LENGTH];
  strcpy_s(input_path, MAX_PATH_LENGTH, utf8_path);
  normalize_path(input_path);
  printf("Input path: %s\n", input_path);
  wchar_t *split_dir = char_to_wchar(input_path);
  if (!split_dir) {
    printf("Error: Failed to convert path to wide string\n");
    free(utf8_path);
    return 1;
  }
  wchar_t absolute_path[MAX_PATH_LENGTH];
  if (GetFullPathNameW(split_dir, MAX_PATH_LENGTH, absolute_path, NULL) == 0) {
    wcscpy_s(absolute_path, MAX_PATH_LENGTH, split_dir);
  }
  char *absolute_path_char = wchar_to_char(absolute_path);
  printf("Absolute path: %s\n",
         absolute_path_char ? absolute_path_char : "[Unable to display]");
  if (absolute_path_char)
    free(absolute_path_char);
  if (!validate_split_directory(absolute_path)) {
    printf("Error: Split directory does not exist or is not accessible\n");
    wchar_t current_dir[MAX_PATH_LENGTH];
    if (get_current_directory(current_dir, MAX_PATH_LENGTH)) {
      char *current_dir_char = wchar_to_char(current_dir);
      printf("Current directory: %s\n",
             current_dir_char ? current_dir_char : "[Unable to display]");
      if (current_dir_char)
        free(current_dir_char);
    }
    free(split_dir);
    free(utf8_path);
    return 1;
  }
  wchar_t merged_file_path[MAX_PATH_LENGTH];
  if (!get_merged_file_path(absolute_path, merged_file_path, MAX_PATH_LENGTH)) {
    printf(
        "Error: Could not determine output file path from split directory\n");
    printf("Make sure the split directory name ends with '-split'\n");
    free(split_dir);
    free(utf8_path);
    return 1;
  }
  char *merged_file_path_char = wchar_to_char(merged_file_path);
  if (merged_file_path_char) {
    printf("Output file: %s\n", merged_file_path_char);
    free(merged_file_path_char);
  } else {
    printf("Output file: [Unable to display path]\n");
  }
  printf("\nStarting merge process...\n");
  if (merge_part_files(absolute_path, merged_file_path)) {
    printf("\nMerge completed successfully!\n");
    char *merged_path_display = wchar_to_char(merged_file_path);
    if (merged_path_display) {
      printf("Output file: %s\n", merged_path_display);
      free(merged_path_display);
    }
    free(split_dir);
    free(utf8_path);
    return 0;
  } else {
    printf("\nMerge failed!\n");
    free(split_dir);
    free(utf8_path);
    return 1;
  }
}
