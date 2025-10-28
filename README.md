# test-7-git-batches

优化代码：
分析上面代码，罗列隐藏问题
针对刚刚提出的3个严重问题，分别列出解决方案，并输出完整代码

增加一个测试函数，在跑完process_input_paths后，对每个分组都进行如下的操作：
每个路径前后加个双引号，用英文空格拼接到字符串"git add"后面形成新的字符串，当长度超过30000后当成命令去用系统方式执行，继续拼接和执行，直到所有路径都用完后，执行git commit -F commit-info.txt，再执行git push
commit-info.txt为exe命令的第一个参数

看看readme.md修改能不能识别到

对所有分组的所有大文件进行拆分，并把大文件备份一下：
大文件文件名如a-name.bin
在大文件所在目录下创建.gitignore文件，把大文件不带拓展名的文件名+"-merged."+大文件拓展名字符串（如a-name-merged.bin）加到.gitignore文件里去，并把.gitignore文件加入到文件分组中去。
在大文件所在目录下创建文件夹，命名为大文件文件名+"-split"后缀（如a-name.bin-split），把这个文件夹加入到文件分组中去，里面放所有子文件，
每个子文件命名为大文件文件名+"-part0001"后缀（如a-name.bin-part0001），大小最大为MAX_FILE_SIZE，
把大文件备份到新的地址，新地址在大文件所在绝对路径的基础上，只在git仓库所在目录文件夹名的基础上+"-backup"（如大文件绝对路径为E:\test-7-git-batches-large-repo\a\b4\c0000\d0000.bin的git仓库目录路径是E:\test-7-git-batches-large-repo，则把它复制到E:\test-7-git-batches-large-repo-backup\a\b4\c0000\d0000.bin）
输出完整代码后检查整个代码是否完全满足上面的需求，如不满足则自行修改，直到全都满足为止后停止修改，并输出完整代码

<!-- # 增加拆分大文件并备份的需求 -->
<!--  -->
<!-- process_input_paths时不跳过大文件，返回的结构体里增加一个数组用来放它们。 -->
<!--  -->
<!-- 如果存在大文件，则判断是否需要拆分成每个50M大小的文件，并备份原文件，把原文件名增加到当前目录的git忽略列表里。 -->
<!--  -->
<!-- 拆分规则如下： -->
<!--  -->
<!-- 在同目录下创建目录，命名为原文件名+"-split" -->
<!-- 把拆分的小文件放到该目录下 -->
<!--  -->
<!-- 需要拆分的条件如下： -->
<!-- 大文件所在目录存在原文件名+"-split"的文件夹 -->
<!-- 且文件夹下所有文件的总大小和大文件的大小不相同 -->
<!--  -->
<!-- 备份规则如下： -->
<!--  -->
<!-- 把大文件备份到新的地址，新地址在大文件所在绝对路径的基础上， -->
<!-- 只在git仓库所在目录文件夹名的基础上+"-backup"（如大文件绝对路径为E:\test-7-git-batches-large-repo\a\b4\c0000\d0000.bin的git仓库目录路径是E:\test-7-git-batches-large-repo，则把它复制到E:\test-7-git-batches-large-repo-backup\a\b4\c0000\d0000.bin） -->
<!--  -->
<!-- 忽略规则如下： -->
<!--  -->
<!-- 在大文件所在目录下创建.gitignore文件（不存在的情况下） -->
<!-- 把大文件不带拓展名的文件名+"-merged."+大文件拓展名字符串（如file.bin->file-merged.bin）加到.gitignore文件里去 -->
<!--  -->
<!-- 如果存在大文件，而且存在判断需要拆分的，则重新调用一下process_input_paths，再去往下跑 -->
<!-- 如果不存在大文件，或者大文件都不需要拆分，则直接往下跑 -->
<!--  -->
<!-- 输出修改后的完整代码 -->

# 增加拆分大文件并备份的需求

process_input_paths返回的结构体里，增加跳过的大文件数组，返回后打印大文件信息
需要修改的函数完整输出出来

print_skipped_files时，每打印一个大文件路径，就把这个大文件备份一下：
复制到新的路径，复制规则如下：
如果大文件路径为E:\test-6-git-batches\中a\空 尾b1\大 文c20000\d0000.bin，
它所在git仓库路径为E:\test-6-git-batches
则新路径为E:\test-6-git-batches-backup\中a\空 尾b1\大 文c20000\d0000.bin
需要修改的函数完整输出出来

备份错误
文件"E:\test-7-git-batches-large-repo-4\a\中 空b1\空 c0001\空 d0007.bin"
需要备份到"E:\test-7-git-batches-large-repo-4-backup\a\中 空b1\空 c0001\空 d0007.bin"
而不是"E:\test-7-git-batches-large-repo-4-backup\空 d0007.bin"
需要修改的函数完整输出出来

print_skipped_files时，每备份一个大文件，就更新一下它所在目录下的.gitignore文件（没有则创建）：
比如大文件"E:\test-7-git-batches-large-repo-4\大文件\中 空b1\空 c0001\空 d0007.bin"
则需要在"E:\test-7-git-batches-large-repo-4\大文件\中 空b1\空 c0001\.gitignore"文件中更新忽略列表
增加忽略"空 d0007-merged.bin"
注意：
1. 大文件路径存在中文和空格
2. 不要出现创建文件错误以及写入为空或者写入中文乱码等问题
3. 如果已经存在了，就不要更新.gitignore了
需要修改的函数完整输出出来

update_gitignore_for_skipped_file之后
在大文件所在目录创建文件夹，用以存放拆分文件
拆分示例如下：
大文件E:\test-7-git-batches\大文件\中 空b1\空 c0001\空 d0004.bin
创建拆分目录E:\test-7-git-batches\大文件\中 空b1\空 c0001\空 d0004.bin-split
拆分文件1：E:\test-7-git-batches\大文件\中 空b1\空 c0001\空 d0004.bin-split\空 d0004.bin-part0001
拆分文件2：E:\test-7-git-batches\大文件\中 空b1\空 c0001\空 d0004.bin-split\空 d0004.bin-part0002
...
判断需要拆分的条件如下：
拆分目录不存在
或者所有拆分文件总大小和大文件总大小不一样大
注意：
1. 大文件路径存在中文和空格，不要出现路径错误或者中文乱码等问题
2. 每个拆分文件50M
需要修改或者添加的函数完整输出出来

需要拆分时，
如果拆分目录存在，则先清空拆分目录
需要修改或者添加的函数完整输出出来

print_skipped_files的for循环跑完之后
把update_gitignore_for_skipped_file里创建或更新的.gitignore文件
和split_large_file里创建的拆分目录里的所有拆分文件
一起加入到分组中去
先调用print_skipped_files再调用print_groups
需要修改或者添加的函数完整输出出来

输入总大小和分组总大小比较

拆分目录已存在，正在清空。这里需要改一下：
拆分目录存在且里面所有拆分文件的总大小和大文件的总大小不一样，才需要清空，重新拆分
需要修改或者添加的函数完整输出出来

检查到需要拆分才需要备份，拆分完后才备份
需要修改或者添加的函数完整输出出来

拆分文件总大小和大文件差一点都需要重新拆分备份

other: 调整打印顺序

跳过大文件总大小: 2.50 GB
需要拆分的文件: 29/29 个
备份成功: 1/29 个文件
.gitignore更新: 1/29 个目录
[警告] 部分文件备份失败，请检查权限和磁盘空间
[成功] 所有大文件拆分处理完成
[警告] 部分.gitignore文件更新失败
消除警告，因为不一定全部文件都需要拆分
需要修改或者添加的函数完整输出出来

[Git] 正在执行 git status --porcelain...
  [调试] 解析到文件: 'README.md'
  [调试] 解析到文件: 'group-file.c'
  [调试] 解析到文件: 'group-file.exe'
  [警告] 路径不存在或无法访问: '大文件/'
  [调试] 解析到文件: '大文件/'
[Git] 找到 4 个变更文件
[Git] 文件列表:
  1. 'README.md'
  2. 'group-file.c'
  3. 'group-file.exe'
  4. '大文件/'
分析[警告] 路径不存在或无法访问: '大文件/'的原因，如果有解决方案
需要修改或者添加的函数完整输出出来

[Git] 正在执行 git status --porcelain...
  [信息] 找到文件: 'README.md'
  [信息] 找到文件: 'group-file.c'
  [信息] 找到文件: 'group-file.exe'
  [信息] 路径可能为新文件或重命名: '大文件/'
  [信息] 找到文件: '大文件/'
[Git] 找到 4 个变更项
[Git] 变更项列表:
  1. 'README.md'
  2. 'group-file.c'
  3. 'group-file.exe'
  4. '大文件/'
依旧是attr == INVALID_FILE_ATTRIBUTES
移除结尾的斜杠后也是如此
需要修改或者添加的函数完整输出出来
