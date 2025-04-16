#!/bin/bash  # 强制使用 bash 解释器

# 脚本功能：监控 ycsb_test 进程的 NUMA 内存状态
# 触发停止方式：按 Ctrl+C 或 kill 脚本进程

# 捕获退出信号（Ctrl+C/SIGTERM），清理后退出
cleanup() {
  echo -e "\n[$(date +'%F %T')] 用户终止脚本。"
  exit 0
}
trap cleanup INT TERM 

# 检测目标进程是否存在
find_target_pid() {
  pgrep -f './ycsb_test' 2>/dev/null
}

# 主监控逻辑
main() {
  echo "[$(date +'%F %T')] 启动监控，等待 ycsb_test 进程..."

  while true; do
    pid=$(find_target_pid)
    
    if [ -n "$pid" ]; then
      echo "[$(date +'%F %T')] 发现进程 PID=$pid, 开始监控 NUMA 状态..."
      
      while kill -0 "$pid" 2>/dev/null; do
        echo "====== [$(date +'%F %T')] NUMA 状态快照 ======"
        numastat -p "$pid"
        sleep 1
      done
      
      echo "[$(date +'%F %T')] 进程 $pid 已退出，重新检测中..."
    else
      sleep 1
    fi
  done
}

main

