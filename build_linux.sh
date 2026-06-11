#!/bin/bash
echo "========================================"
echo "中医经络数字化系统 - Linux/macOS 编译脚本"
echo "========================================"
echo

CXX=g++
if command -v clang++ &> /dev/null; then
    CXX=clang++
fi

echo "使用编译器: $CXX"
$CXX -std=c++17 -O2 -o tcm_backend backend_single.cpp -lpthread
if [ $? -eq 0 ]; then
    echo
    echo "[成功] 编译完成: tcm_backend"
    chmod +x tcm_backend
else
    echo "[失败] 编译出错"
    exit 1
fi
