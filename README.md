# test-7-git-batches

优化代码：
分析上面代码，罗列隐藏问题
针对刚刚提出的3个严重问题，分别列出解决方案，并输出完整代码

增加一个测试函数，在跑完process_input_paths后，对每个分组都进行如下的操作：
每个路径前后加个双引号，用英文空格拼接到字符串"git add"后面形成新的字符串，当长度超过30000后当成命令去用系统方式执行，继续拼接和执行，直到所有路径都用完后，执行git commit -F commit-info.txt，再执行git push
commit-info.txt为exe命令的第一个参数
