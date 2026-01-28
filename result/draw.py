"""
IEEE/ACM Conference Style Bar Plot Generator
Author: Your Name
Description: 生成符合学术会议论文要求的柱状图，支持自定义参数和文件数据读取
"""

import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np
import json
import csv

#### 配置参数 ####
# 组别数
group_count = 2
# 横坐标名称
x_label = "Test Name"
# 纵坐标名称
y_label = "Memory Used (GB)"
# 数据1名称
data_name1 = "Without Local Cache"
# 数据2名称
data_name2 = "With Local Cache"
# Y轴最大值
y_max = 40
# Y轴步长
y_step = 5
# 柱子宽度
bar_config_width = 0.25
# 柱子间距
bar_config_spacing = 0.15

# 输出文件名
target_file = "MemoryUse.pdf"




def read_data_from_file(filename, format='csv'):
    """
    从文件读取数据 支持CSV/JSON格式
    参数：
        filename: 文件名
        format: 文件格式 ('csv' 或 'json')
    返回：
        dict: 包含labels, values, colors等键的字典
    """
    data = {}
    try:
        if format == 'csv':
            with open(filename, 'r') as f:
                reader = csv.reader(f)
                headers = [h.strip() for h in next(reader)]  # 去除列名空格
                data = {header: [] for header in headers}
                
                for row in reader:
                    # 跳过空行
                    if not row:  
                        continue
                    # 去除每列值的空格
                    cleaned_row = [col.strip() for col in row]
                    for i, (header, value) in enumerate(zip(headers, cleaned_row)):
                        if header in ['value1', 'value2']:
                            try:
                                data[header].append(float(value))
                            except ValueError:
                                print(f"数值转换错误：行{reader.line_num}，值'{value}'")
                                data[header].append(0.0)
                        else:
                            data[header].append(value)
    except Exception as e:
        print(f"Error reading file: {str(e)}")
        exit(1)    
    # 验证必要字段
    required_fields = ['labels', 'value1', 'value2']
    for field in required_fields:
        if field not in data:
            raise ValueError(f"Missing required field: {field}")
    
    return data

def create_bar_plot(labels, values1, values2,
                   figsize=(6, 4),         # 图表尺寸（英寸）
                   bar_width=bar_config_width,          # 柱宽（0-1）
                   spacing=bar_config_spacing,           # 柱子间距
                   #  colors='#2F7FC1',      # 颜色（支持单个或列表）
                   colors1='#2F7FC1',      # 颜色（支持单个或列表）
                   colors2='#E66100',      # 颜色（支持单个或列表）
                   xlabel='Categories',   # X轴标签
                   ylabel='Values',       # Y轴标签
                   ylim=(0, y_max),             # Y轴范围
                   y_ticks_step=y_step,  # 新增参数：Y轴刻度间隔
                   grid=True,             # 显示网格
                   dpi=300,               # 输出分辨率
                   font_size=10,          # 基础字体大小
                   output_file='plot.pdf' # 输出文件名
                   ):
    """
    创建符合学术会议要求的柱状图
    """
    # 设置学术论文样式参数
    plt.style.use('seaborn-v0_8-white')
    plt.rcParams.update({
        'font.family': 'sans-serif',        
        'font.serif': 'Arial', 
        'font.size': font_size,
        'axes.labelsize': font_size + 1,
        'axes.titlesize': font_size + 2,
        'xtick.labelsize': font_size - 1,
        'ytick.labelsize': font_size - 1,
        'axes.linewidth': 0.8,          # 坐标轴线宽
        'grid.linewidth': 0.4,
        'legend.fontsize': font_size - 1,
        'figure.dpi': dpi,
        'savefig.dpi': dpi,
        'savefig.format': output_file.split('.')[-1].lower(),
        'savefig.bbox': 'tight',
        'savefig.pad_inches': 0.15,
    })

    # 创建图表和坐标轴
    fig, ax = plt.subplots(figsize=figsize)

    # 生成x轴位置
    x = np.arange(len(labels))

    # # 绘制柱状图
        # bars = ax.bar(x, values1, width=bar_width, 
        #             color=colors1, edgecolor='black',
        #             linewidth=0.5, align='center')
    # 绘制两组数据的柱状图
    bars1 = ax.bar(x - bar_width/2, values1, width=bar_width, color=colors1, edgecolor='black', label=data_name1)
    bars2 = ax.bar(x + bar_width/2, values2, width=bar_width, color=colors2, edgecolor='black', label=data_name2)

    # 设置坐标轴标签
    ax.set_xlabel(xlabel, fontweight='bold')
    ax.set_ylabel(ylabel, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=0, ha='center', fontweight='bold')


    # 设置Y轴范围
    if ylim is not None:
        ax.set_ylim(ylim)
        # 自动生成刻度
        y_ticks = np.arange(ylim[0], ylim[1] + y_ticks_step, y_ticks_step)
        ax.set_yticks(y_ticks)
        ax.set_yticklabels([f"{tick:.1f}" for tick in y_ticks])

    # 添加网格
    if grid:
        ax.yaxis.grid(True, linestyle='--', alpha=0.6)
    
    # # 添加柱状图上的数据标签
    # for bar in bars1:
    #     height = bar.get_height()
    #     ax.annotate(f'{height:.2f}',
    #                 xy=(bar.get_x() + bar.get_width() / 2, height),
    #                 xytext=(0, 3),
    #                 textcoords="offset points",
    #                 ha='center', va='bottom',
    #                 fontsize=font_size-3)

    # for bar in bars2:
    #     height = bar.get_height()
    #     ax.annotate(f'{height:.2f}',
    #                 xy=(bar.get_x() + bar.get_width() / 2, height),
    #                 xytext=(0, 3),
    #                 textcoords="offset points",
    #                 ha='center', va='bottom',
    #                 fontsize=font_size-3)

    # 调整布局
    plt.tight_layout()

    # 添加图例
    ax.legend(loc='upper center', bbox_to_anchor=(0.5, 1.10), ncol=2)

    # plt.subplots_adjust(right=0.8)  # 默认是0.9，值越小右边距越大

    # 保存图表
    plt.savefig(output_file)
    plt.close()

if __name__ == "__main__":
    # 示例用法1：直接使用数据
    # example_labels = ['Method A', 'Method B', 'Method C']
    # example_values = [78.3, 85.6, 82.1]
    
    # create_bar_plot(
    #     labels=example_labels,
    #     values=example_values,
    #     colors=['#2F7FC1', '#E66100', '#5D3A9B'],  # 不同颜色
    #     xlabel='Methods',
    #     ylabel='Accuracy (%)',
    #     ylim=(70, 90),
    #     output_file='example_plot.pdf'
    # )

    # 示例用法2：从文件读取数据
    # 示例数据文件格式（CSV）：
    # labels,values,colors
    # Method A,78.3,#2F7FC1
    # Method B,85.6,#E66100
    # Method C,82.1,#5D3A9B
    
    file_data = read_data_from_file('data.csv', format='csv')
    create_bar_plot(
        labels=file_data['labels'],
        values1=file_data['value1'],
        values2=file_data['value2'],
        # values=file_data['values'],
        # colors=file_data.get('colors', '#2F7FC1'),  # 可选颜色列
        xlabel= x_label,
        ylabel= y_label,
        # title='Comparison of EXT4, F2FS, and BTRFS',
        output_file=target_file,
    )
        