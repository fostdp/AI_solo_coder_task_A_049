@echo off
chcp 65001 >nul
if not exist tcm_backend.exe (
    echo 正在编译后端...
    call build_windows.bat
)
if exist tcm_backend.exe (
    echo 启动后端服务...
    tcm_backend.exe --port 8080
) else (
    echo [错误] 编译失败，无法启动
    pause
)
