#!/bin/bash
# 树莓派启动脚本：chmod +x start.sh 后运行 ./start.sh
cd "$(dirname "$0")" || exit 1
echo "============================================"
echo "  智能叫号系统 患者端服务"
echo "  手机访问: http://$(hostname -I | awk '{print $1}'):5000"
echo "============================================"
python3 app.py
