#include "ble_simulator.h"
#include <iostream>
#include <csignal>
#include <string>

tcm::BLESimulator* g_sim = nullptr;

void signal_handler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在停止模拟器..." << std::endl;
    if (g_sim) g_sim->stop();
}

int main(int argc, char* argv[]) {
    tcm::BLESimulator::Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) config.server_host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) config.udp_port = std::atoi(argv[++i]);
        else if (arg == "--http" && i + 1 < argc) config.http_url = argv[++i];
        else if (arg == "--no-http") config.http_url = "";
        else if (arg == "--volunteers" && i + 1 < argc) config.num_volunteers = std::atoi(argv[++i]);
        else if (arg == "--interval" && i + 1 < argc) config.interval_ms = std::atoi(argv[++i]);
        else if (arg == "--no-anomaly") config.inject_anomalies = false;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "BLE 数据模拟器\n"
                      << "用法: ble_simulator [选项]\n\n"
                      << "选项:\n"
                      << "  --host <主机>         服务器地址 (默认: 127.0.0.1)\n"
                      << "  --port <端口>         UDP端口 (默认: 8081)\n"
                      << "  --http <URL>          HTTP上报URL (默认: http://127.0.0.1:8080)\n"
                      << "  --no-http             使用UDP而非HTTP上报\n"
                      << "  --volunteers <数量>   模拟志愿者数量 (默认: 30)\n"
                      << "  --interval <ms>       上报间隔毫秒 (默认: 100)\n"
                      << "  --no-anomaly          不注入异常数据\n"
                      << "  -h, --help            显示帮助信息\n";
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    tcm::BLESimulator sim;
    g_sim = &sim;

    std::cout << "========================================" << std::endl;
    std::cout << "BLE 传感器数据模拟器" << std::endl;
    std::cout << "========================================" << std::endl;

    if (!sim.start(config)) {
        std::cerr << "模拟器启动失败！" << std::endl;
        return 1;
    }

    std::cout << "按 Ctrl+C 停止模拟器" << std::endl;

    int tick = 0;
    while (sim.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        tick++;
        if (tick % 3 == 0) {
            auto volunteers = {std::string("V001"), std::string("V015"), std::string("V030")};
            auto points = {"LI4","ST36","PC6","LR3","BL23"};
            for (const auto& v : volunteers) {
                std::string ap = points[tick % 5];
                sim.trigger_acupuncture(v, ap, 90 + (tick * 7) % 60);
            }
        }
    }

    g_sim = nullptr;
    std::cout << "模拟器已停止。共发送 " << sim.get_total_packets() << " 数据包" << std::endl;
    return 0;
}
