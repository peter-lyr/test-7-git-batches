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
在大文件所在目录下创建.gitignore文件，把大文件不带拓展名的文件名+"-merged."+大文件拓展名字符串（如a-name-merged.bin）加到.gitignore文件里去。
在大文件所在目录下创建文件夹，命名为大文件文件名+"-split"后缀（如a-name.bin-split），里面放所有子文件，
每个子文件命名为大文件文件名+"-part0001"后缀（如a-name.bin-part0001），大小最大为MAX_FILE_SIZE，
把大文件备份到新的地址，新地址在大文件所在绝对路径的基础上，只在git仓库所在目录文件夹名的基础上+"-backup"
