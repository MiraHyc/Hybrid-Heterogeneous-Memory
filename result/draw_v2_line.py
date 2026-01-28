"""
IEEE/ACM Conference Style Line Plot Generator (多组支持版)
Author: Your Name
Description: 生成符合学术会议论文要求的折线图，支持自定义组数和文件数据读取
"""

import matplotlib.pyplot as plt
import numpy as np
import csv

#### 配置参数 ####
# 横坐标名称
x_label = "Local Cache Size (MB)"
# 纵坐标名称
y_label = "Percent (%)"
# Y轴最大值
y_max = 100
# Y轴步长
y_step = 10
# 是否使用整数步长标签
y_step_int = True
# 线型配置（可选：'-', '--', '-.', ':'）
line_style = '-'
# 标记样式（可选：'o', 's', 'D', '^', 'v'等）
marker_style = 'o'
# 标记大小
marker_size = 3
# 线宽
line_width = 1
# 各组配置（名称、颜色、线型、标记）
group_config = [
    {"name": "Cache Hit", "color": "#2F7FC1", "linestyle": "-", "marker": "o"},
    {"name": "Hash Bucket Usage", "color": "#009E73", "linestyle": "-", "marker": "s"},
    # 添加更多组...
]
# 数据文件名
data_file = 'data_cache_size.csv'
# 输出文件名
target_file = "test_cache_size_line.pdf"

def read_data_from_file(filename, group_count=2):
    """（与原始代码相同，保持不变）"""
    data = {"labels": []}
    try:
        with open(filename, 'r') as f:
            reader = csv.reader(f)
            headers = [h.strip() for h in next(reader)]  # 读取列头
            
            # 初始化数据存储结构
            for i in range(1, group_count+1):
                data[f'value{i}'] = []
            
            for row in reader:
                if not row:  # 跳过空行
                    continue
                # 处理labels列
                data["labels"].append(row[0].strip())
                
                # 处理数值列
                for i in range(1, min(group_count+1, len(row))):
                    try:
                        val = float(row[i].strip())
                        data[f'value{i}'].append(val)
                    except (IndexError, ValueError):
                        data[f'value{i}'].append(0.0)
    except Exception as e:
        print(f"Error reading file: {str(e)}")
        exit(1)
    
    # 验证数据完整性
    required_fields = ["labels"] + [f"value{i+1}" for i in range(group_count)]
    for field in required_fields:
        if field not in data:
            raise ValueError(f"Missing required field: {field}")
    
    return data

def create_line_plot(labels, group_data,
                   figsize=(6, 4),
                   xlabel='Categories',
                   ylabel='Values',
                   ylim=(0, y_max),
                   y_ticks_step=y_step,
                   grid=True,
                   dpi=300,
                   font_size=10,
                   output_file='plot.pdf'):
    """
    创建支持多组的折线图
    group_data: 包含各组数据的列表 [
        {"values": [], "name": "", "color": "", "linestyle": "", "marker": ""},
        ...
    ]
    """
    # 设置学术样式参数
    plt.style.use('seaborn-v0_8-white')
    plt.rcParams.update({
        'font.family': 'sans-serif',
        'font.size': font_size,
        'axes.labelsize': font_size + 1,
        'axes.titlesize': font_size + 2,
        'xtick.labelsize': font_size - 1,
        'ytick.labelsize': font_size - 1,
        'axes.linewidth': 0.8,
        'grid.linewidth': 0.4,
        'legend.fontsize': font_size - 1,
        'figure.dpi': dpi,
        'savefig.dpi': dpi,
        'savefig.bbox': 'tight',
        'savefig.pad_inches': 0.15,
    })

    fig, ax = plt.subplots(figsize=figsize)
    x = np.arange(len(labels))
    
    # 绘制各组折线图
    for idx, config in enumerate(group_data):
        ax.plot(x,
               config["values"],
               color=config["color"],
               linestyle=config.get("linestyle", "-"),
               linewidth=line_width,
               marker=config.get("marker", "o"),
               markersize=marker_size,
               markeredgecolor='black',
               markeredgewidth=0.5,
               label=config["name"])

    # 设置坐标轴
    ax.set_xlabel(xlabel, fontweight='bold')
    ax.set_ylabel(ylabel, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=0, ha='center', fontweight='bold')
    
    # 设置Y轴
    if ylim is not None:
        ax.set_ylim(ylim)
        y_ticks = np.arange(ylim[0], ylim[1] + y_ticks_step, y_ticks_step)
        ax.set_yticks(y_ticks)
        if y_step_int or y_ticks_step.is_integer():
            ax.set_yticklabels([f"{int(tick)}" for tick in y_ticks])
        else:
            ax.set_yticklabels([f"{tick:.1f}" for tick in y_ticks])

    # 添加网格
    if grid:
        ax.yaxis.grid(True, linestyle='--', alpha=0.6)

    # 调整图例位置
    ax.legend(loc='upper center', 
             bbox_to_anchor=(0.5, 1.15),
             ncol=min(3, len(group_data)),
             frameon=True)

    plt.tight_layout()
    plt.savefig(output_file)
    plt.close()

if __name__ == "__main__":
    # 示例配置（与CSV文件列对应）
    group_count = len(group_config)
    
    # 从文件读取数据
    file_data = read_data_from_file(data_file, group_count=group_count)
    
    # 准备绘图数据
    plot_groups = []
    for i, config in enumerate(group_config):
        plot_groups.append({
            "values": file_data[f'value{i+1}'],
            "name": config["name"],
            "color": config["color"],
            "linestyle": config.get("linestyle", "-"),
            "marker": config.get("marker", "o")
        })
    
    create_line_plot(
        labels=file_data['labels'],
        group_data=plot_groups,
        xlabel=x_label,
        ylabel=y_label,
        output_file=target_file
    )