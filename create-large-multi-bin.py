import os
import random
import shutil
import multiprocessing
import time
from functools import partial

dir_name = "大文件/中 空b1"
dir_prefix = "空 c"
prefix = "空 d"
# 总大小设置为GB
total_size_mb = 56
# 每个文件大小范围
min_file_size = 53 * 1024 * 1024
max_file_size = 55 * 1024 * 1024
# 每个文件夹大小范围
min_folder_size = 400 * 1024 * 1024
max_folder_size = 800 * 1024 * 1024


def generate_single_file(file_info, progress_queue):
    filename, file_size = file_info
    buffer_size = 512 * 1024
    pid = os.getpid()
    progress_queue.put(
        {
            "pid": pid,
            "filename": filename,
            "status": "started",
            "file_size": file_size,
            "message": f"进程 {pid:>5} 开始生成文件 {filename:<30} ({file_size/(1024*1024):6.2f} MB)...",
        }
    )
    start_time = time.time()
    last_report_time = start_time
    try:
        with open(filename, "wb") as f:
            bytes_written = 0
            while bytes_written < file_size:
                remaining = file_size - bytes_written
                current_buffer_size = min(buffer_size, remaining)
                try:
                    random_bytes = os.urandom(current_buffer_size)
                except:
                    random_bytes = bytes(
                        random.randint(0, 255) for _ in range(int(current_buffer_size))
                    )
                f.write(random_bytes)
                bytes_written += current_buffer_size
                current_time = time.time()
                if current_time - last_report_time >= 5.0:
                    progress = (bytes_written / file_size) * 100
                    elapsed = current_time - start_time
                    if elapsed > 0:
                        speed = bytes_written / (1024 * 1024) / elapsed
                    else:
                        speed = 0
                    progress_queue.put(
                        {
                            "pid": pid,
                            "filename": filename,
                            "status": "progress",
                            "progress": progress,
                            "bytes_written": bytes_written,
                            "file_size": file_size,
                            "speed": speed,
                            "elapsed": elapsed,
                        }
                    )
                    last_report_time = current_time
        end_time = time.time()
        elapsed = end_time - start_time
        speed = 9999
        if elapsed:
            speed = file_size / (1024 * 1024) / elapsed
        progress_queue.put(
            {
                "pid": pid,
                "filename": filename,
                "status": "completed",
                "message": f"进程 {pid:>5} 完成 {filename:<30} - 耗时: {elapsed:6.2f}秒, 速度: {speed:5.2f} MB/s",
                "elapsed": elapsed,
                "speed": speed,
            }
        )
        return filename, file_size
    except Exception as e:
        progress_queue.put(
            {
                "pid": pid,
                "filename": filename,
                "status": "error",
                "message": f"进程 {pid:>5} 生成文件 {filename:<30} 时出错: {str(e)}",
            }
        )
        raise


def progress_monitor(progress_queue, total_files, total_size_bytes):
    process_info = {}
    completed_files = 0
    completed_size = 0
    start_time = time.time()
    print("进度监控器已启动，显示所有子进程的实时进度...")
    print("=" * 100)
    while completed_files < total_files:
        try:
            info = progress_queue.get(timeout=1)
            pid = info["pid"]
            filename = info["filename"]
            if info["status"] == "started":
                print(f"[{time.strftime('%H:%M:%S')}] {info['message']}")
                file_size = info.get("file_size", 0)
                process_info[pid] = {
                    "filename": filename,
                    "progress": 0,
                    "bytes_written": 0,
                    "file_size": file_size,
                    "speed": 0,
                    "start_time": time.time(),
                }
            elif info["status"] == "progress":
                file_size = info.get(
                    "file_size", process_info.get(pid, {}).get("file_size", 0)
                )
                if pid not in process_info:
                    process_info[pid] = {
                        "filename": filename,
                        "progress": 0,
                        "bytes_written": 0,
                        "file_size": file_size,
                        "speed": 0,
                        "start_time": time.time(),
                    }
                process_info[pid].update(
                    {
                        "progress": info["progress"],
                        "bytes_written": info["bytes_written"],
                        "file_size": file_size,
                        "speed": info["speed"],
                        "last_update": time.time(),
                    }
                )
                file_info = process_info[pid]
                progress_str = f"{file_info['progress']:5.1f}%"
                written_str = f"{file_info['bytes_written']/(1024*1024):6.2f}"
                total_str = f"{file_info['file_size']/(1024*1024):6.2f}"
                speed_str = f"{file_info['speed']:5.2f}"
                print(
                    f"[{time.strftime('%H:%M:%S')}] 进程 {pid:>5}: {filename:<30} - "
                    f"进度: {progress_str} ({written_str}/{total_str} MB) - "
                    f"速度: {speed_str} MB/s"
                )
            elif info["status"] == "completed":
                print(f"[{time.strftime('%H:%M:%S')}] {info['message']}")
                if pid in process_info:
                    completed_files += 1
                    completed_size += process_info[pid]["file_size"]
                progress = (completed_size / total_size_bytes) * 100
                elapsed_time = time.time() - start_time
                if elapsed_time > 0:
                    overall_speed = (
                        completed_size / (1024 * 1024 * 1024) / (elapsed_time / 3600)
                    )
                else:
                    overall_speed = 0
                print(
                    f"[{time.strftime('%H:%M:%S')}] 总体进度: {progress:5.1f}% "
                    f"({completed_size/(1024*1024*1024):6.2f} GB / {total_size_bytes/(1024*1024*1024):6.2f} GB) - "
                    f"速度: {overall_speed:6.2f} GB/小时 - "
                    f"已完成 {completed_files:4d}/{total_files:4d} 个文件"
                )
                print("-" * 100)
            elif info["status"] == "error":
                print(f"[{time.strftime('%H:%M:%S')}] {info['message']}")
        except:
            pass
    total_time = time.time() - start_time
    print(f"所有文件生成完成! 总共生成了 {total_files:4d} 个文件")
    print(f"总大小: {completed_size/(1024*1024*1024):6.2f} GB")
    print(f"总耗时: {total_time:8.2f} 秒")
    print(
        f"平均速度: {completed_size/(1024*1024*1024) / (total_time/3600):6.2f} GB/小时"
    )


def generate_random_bin_files_parallel():
    total_size_bytes = total_size_mb * 1024 * 1024
    print(f"目标总大小: {total_size_mb:3f} MB")
    print(
        f"每个文件大小: {min_file_size/(1024*1024):5.1f} MB 到 {max_file_size/(1024*1024):5.1f} MB"
    )
    print(
        f"每个文件夹大小: {min_folder_size/(1024*1024):5.1f} MB 到 {max_folder_size/(1024*1024):5.1f} MB"
    )

    file_tasks = []
    bytes_written_total = 0
    file_count = 0
    folder_count = 0

    # 创建第一个文件夹
    current_folder_path = f"{dir_name}/{dir_prefix}{folder_count:04d}"
    os.makedirs(current_folder_path, exist_ok=True)
    current_folder_size = 0

    # 为每个文件夹随机设置目标大小
    current_folder_target_size = random.randint(
        int(min_folder_size), int(max_folder_size)
    )
    print(
        f"文件夹 {folder_count:04d} 目标大小: {current_folder_target_size/(1024*1024):.2f} MB"
    )

    while bytes_written_total < total_size_bytes:
        # 计算当前文件夹剩余可用空间
        folder_remaining = current_folder_target_size - current_folder_size
        # 计算总剩余空间
        remaining_bytes = total_size_bytes - bytes_written_total

        # 如果当前文件夹已满或剩余空间不足最小文件大小，创建新文件夹
        if current_folder_size >= current_folder_target_size or (
            folder_remaining < min_file_size and remaining_bytes > min_file_size
        ):
            folder_count += 1
            current_folder_path = f"{dir_name}/{dir_prefix}{folder_count:04d}"
            os.makedirs(current_folder_path, exist_ok=True)
            current_folder_size = 0
            # 为新文件夹随机设置目标大小
            current_folder_target_size = random.randint(
                int(min_folder_size), int(max_folder_size)
            )
            print(
                f"文件夹 {folder_count:04d} 目标大小: {current_folder_target_size/(1024*1024):.2f} MB"
            )
            folder_remaining = current_folder_target_size

        # 确定当前文件大小
        if remaining_bytes < min_file_size:
            # 剩余空间小于最小文件大小，使用剩余空间
            current_file_size = remaining_bytes
        else:
            # 确保最大可能文件大小不小于最小文件大小
            max_possible_file = min(max_file_size, folder_remaining, remaining_bytes)
            if max_possible_file < min_file_size:
                # 如果最大可能文件大小小于最小文件大小，使用最大可能文件大小
                current_file_size = max_possible_file
            else:
                # 正常情况：在最小和最大可能文件大小之间随机选择
                current_file_size = random.randint(
                    int(min_file_size), int(max_possible_file)
                )

        # 生成文件名
        filename = f"{current_folder_path}/{prefix}{file_count:04d}.bin"
        file_tasks.append((filename, current_file_size))

        bytes_written_total += current_file_size
        current_folder_size += current_file_size
        file_count += 1

    print(
        f"将生成 {file_count:4d} 个文件，分布在 {folder_count+1:4d} 个文件夹中，总大小: {bytes_written_total/(1024*1024*1024):6.2f} GB"
    )
    num_processes = min(multiprocessing.cpu_count(), 16, len(file_tasks))
    print(f"使用 {num_processes:2d} 个进程并行生成文件...")
    manager = multiprocessing.Manager()
    progress_queue = manager.Queue()
    from threading import Thread

    monitor_thread = Thread(
        target=progress_monitor,
        args=(progress_queue, len(file_tasks), total_size_bytes),
    )
    monitor_thread.daemon = True
    monitor_thread.start()
    time.sleep(1)
    start_time = time.time()
    try:
        with multiprocessing.Pool(processes=num_processes) as pool:
            worker_func = partial(generate_single_file, progress_queue=progress_queue)
            results = []
            for result in pool.imap_unordered(worker_func, file_tasks, chunksize=1):
                results.append(result)
    except Exception as e:
        print(f"处理过程中发生错误: {e}")
        import traceback

        traceback.print_exc()
    total_time = time.time() - start_time
    monitor_thread.join(timeout=5)
    manager.shutdown()
    return file_count, folder_count + 1


def main():
    print("开始使用多进程生成随机二进制文件集合...")
    print("使用多文件夹结构，每个文件夹有特定的大小限制")
    try:
        total, used, free = shutil.disk_usage(".")
        required_space = total_size_mb * 1024 * 1024
        if free < required_space:
            print(f"警告: 磁盘空间不足!")
            print(f"需要: {required_space/(1024*1024*1024):6.2f} GB")
            print(f"可用: {free/(1024*1024*1024):6.2f} GB")
            response = input("是否继续? (y/n): ")
            if response.lower() != "y":
                print("操作已取消")
                return
    except Exception as e:
        print(f"无法检查磁盘空间: {e}")
        print("将继续执行，但请确保有足够的磁盘空间...")
    file_count, folder_count = generate_random_bin_files_parallel()
    print(
        f"完成! 共生成 {file_count:4d} 个文件，分布在 {folder_count:4d} 个文件夹中，总大小约{total_size_mb}MB"
    )


if __name__ == "__main__":
    main()
