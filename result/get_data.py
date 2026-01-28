import re

# 输入文件路径和输出文件路径
input_file = "4-18.txt"
output_file = "values.txt"

# 定义正则表达式匹配 "epoch = 9" 以及吞吐量值
pattern = r'epoch = 9.*throughput (\d+\.\d+)'

# 打开输入文件并读取内容
with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
    for line in infile:
        # 使用正则表达式查找匹配的行
        match = re.search(pattern, line)
        if match:
            # 提取吞吐量值并写入输出文件
            throughput = match.group(1)
            outfile.write(throughput + '\n')

print(f"提取的吞吐量值已保存到 {output_file}")
