@echo off
chcp 65001 >nul
echo ========================================
echo 中医经络数字化系统 - Windows 编译脚本
echo ========================================
echo.

where cl >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 MSVC 编译器，请先安装 Visual Studio 或运行 "Developer Command Prompt for VS"
    echo.
    echo 尝试使用 g++ 编译...
    where g++ >nul 2>nul
    if %ERRORLEVEL% neq 0 (
        echo [错误] 也未找到 g++ 编译器
        pause
        exit /b 1
    )
    g++ -std=c++17 -O2 -static -o tcm_backend.exe backend_single.cpp -lws2_32
    if %ERRORLEVEL% equ 0 (
        echo.
        echo [成功] 编译完成: tcm_backend.exe
    )
    goto :end
)

echo 使用 MSVC 编译...
cl /EHsc /std:c++17 /O2 /Fe:tcm_backend.exe backend_single.cpp ws2_32.lib
if %ERRORLEVEL% equ 0 (
    echo.
    echo [成功] 编译完成: tcm_backend.exe
    del *.obj 2>nul
)

:end
echo.
pause
