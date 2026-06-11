#include "http_server.h"
#include <iostream>
#include <csignal>
#include <string>

tcm::HttpServer* g_server = nullptr;

void signal_handler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在关闭服务..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    int http_port = 8080;
    std::string mongodb_uri = "mongodb://localhost:27017";
    std::string db_name = "tcm_acupuncture";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            http_port = std::atoi(argv[++i]);
        } else if (arg == "--mongodb" && i + 1 < argc) {
            mongodb_uri = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            db_name = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "古代中医经络穴位数字化与针刺疗效关联分析系统\n"
                      << "用法: tcm_backend [选项]\n\n"
                      << "选项:\n"
                      << "  --port <端口>        HTTP服务端口 (默认: 8080)\n"
                      << "  --mongodb <URI>      MongoDB连接URI (默认: mongodb://localhost:27017)\n"
                      << "  --db <数据库名>      数据库名称 (默认: tcm_acupuncture)\n"
                      << "  -h, --help           显示帮助信息\n";
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    tcm::HttpServer server;
    g_server = &server;

    std::cout << "========================================" << std::endl;
    std::cout << "古代中医经络穴位数字化与针刺疗效关联分析系统" << std::endl;
    std::cout << "========================================" << std::endl;

    if (!server.start(http_port, mongodb_uri, db_name)) {
        std::cerr << "服务启动失败！" << std::endl;
        return 1;
    }

    std::cout << "服务已启动，HTTP端口: " << http_port << std::endl;
    std::cout << "按 Ctrl+C 停止服务" << std::endl;

    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    g_server = nullptr;
    std::cout << "服务已安全关闭" << std::endl;
    return 0;
}
