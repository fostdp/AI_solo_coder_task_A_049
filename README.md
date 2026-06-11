# 古代中医经络穴位数字化与针刺疗效关联分析系统

> 某中医药大学 · 经络数字化研究实验室  
> **TCM Meridian Digitalization & Acupuncture Efficacy Analysis Platform**

---

## 📋 系统概述

本系统在30名志愿者身上布设皮肤电导、红外温度、肌电（EMG）三类传感器，通过BLE网关每100ms实时上报生理信号，结合**随机森林机器学习算法**预测针刺疗效（得气感、疼痛缓解率），进行**经络穴位拓扑网络分析**，并在异常时通过钉钉推送告警。

### 核心功能
- 🧬 **经络可视化**：Canvas 绘制十四经 + 80+ 穴位，实时热图显示电导/温度/肌电
- 📊 **实时监测**：ECharts 多维度时间序列曲线
- 🤖 **疗效预测**：随机森林回归模型（15维特征，50棵树）
- 🔗 **网络分析**：经络拓扑网络、最短路径、中心性分析、社区检测
- ⚠️ **异常告警**：电导突降≥30%、体温>38℃、肌电Z-score>3，钉钉机器人推送
- 📡 **BLE模拟**：30名志愿者 × 多穴位传感器数据模拟，支持针刺仿真

---

## 📁 项目结构

```
AI_solo_coder_task_A_049/
├── backend/                    # C++ 后端（完整版，使用 crow + mongocxx）
│   ├── CMakeLists.txt
│   ├── include/                # 头文件（10个模块）
│   │   ├── data_types.h            # 统一数据结构
│   │   ├── mongodb_manager.h       # MongoDB 数据层
│   │   ├── ble_data_receiver.h     # BLE UDP 接收器
│   │   ├── random_forest_model.h   # 随机森林预测模型
│   │   ├── meridian_network_analyzer.h  # 经络拓扑分析
│   │   ├── anomaly_detector.h      # 异常检测告警
│   │   ├── dingtalk_notifier.h     # 钉钉通知推送
│   │   ├── websocket_manager.h     # WebSocket 广播
│   │   ├── data_processor.h        # 数据处理管道
│   │   └── http_server.h           # HTTP 服务
│   └── src/                    # 实现文件
├── ble_simulator/              # BLE 传感器数据模拟器
│   ├── CMakeLists.txt
│   ├── include/ble_simulator.h
│   └── src/
├── frontend/                   # 前端页面
│   ├── index.html
│   ├── css/style.css
│   └── js/
│       ├── meridian_renderer.js    # Canvas 经络图渲染器
│       └── app.js                   # 主应用逻辑
├── mongodb/
│   └── init_db.js              # MongoDB 初始化脚本（经络/穴位/志愿者/索引）
├── backend_single.cpp          # ⭐ 零依赖单文件后端（推荐快速体验）
├── build_windows.bat           # Windows 编译脚本
├── build_linux.sh              # Linux/macOS 编译脚本
└── run_windows.bat             # 一键编译+运行脚本
```

---

## 🚀 快速开始（零依赖，推荐）

无需安装 Crow、MongoDB、OpenSSL 等任何第三方库，直接编译运行。

### Windows
```bat
:: 一键编译并启动
run_windows.bat

:: 或手动编译
build_windows.bat
tcm_backend.exe --port 8080
```

### Linux / macOS
```bash
chmod +x build_linux.sh
./build_linux.sh
./tcm_backend --port 8080
```

### 访问系统
启动后打开浏览器访问：
> **http://localhost:8080/static/index.html**

---

## 🏗️ 完整版本构建（Crow + MongoDB）

### 依赖项
- C++17 编译器（MSVC 2019+ / GCC 8+ / Clang 8+）
- CMake ≥ 3.14
- [Crow](https://github.com/CrowCpp/Crow)（header-only HTTP/WebSocket 框架）
- [mongocxx](https://github.com/mongodb/mongo-cxx-driver)（MongoDB C++ 驱动）
- OpenSSL ≥ 1.1
- MongoDB ≥ 4.4

### 编译
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### 初始化数据库
```bash
mongosh --file mongodb/init_db.js
```

### 运行
```bash
# 启动后端（默认端口 8080）
./build/tcm_backend --port 8080 --mongodb mongodb://localhost:27017 --db tcm_acupuncture

# 另开终端启动 BLE 模拟器
./build/ble_simulator --volunteers 30 --interval 100
```

---

## 🧠 核心算法详解

### 1. 随机森林针刺疗效预测

**输入特征（15维）**：

| 特征 | 说明 |
|------|------|
| skin_conductance_change | 针刺前后皮肤电导差值 |
| skin_conductance_ratio | 电导比值（post/pre） |
| temperature_change | 温度差值 |
| emg_amplitude_change | 肌电幅值差值 |
| emg_frequency_change | 肌电频率差值 |
| pre/post_conductance_mean | 针刺前/后电导均值 |
| conductance/temperature_variance | 电导/温度方差 |
| emg_amplitude_mean | 肌电幅值均值 |
| conductance/temperature_slope | 电导/温度时序斜率 |
| post_minus_pre_peak | 针刺前后峰值差 |
| conductance_max_diff | 电导最大差值 |
| emg_spectral_energy | 肌电频谱能量 |

**模型参数**：
- 决策树数量：50 棵
- 最大深度：15
- 最小分裂样本数：5
- 特征采样：√F = 4 个特征/树（袋装 + 特征子采样）
- 输出：`predicted_deqi`（得气强度 0~1）、`predicted_pain_relief`（疼痛缓解率 0~1）、`confidence`（置信度）

### 2. 经络穴位拓扑网络分析

将穴位视为节点，相邻穴位/同经络穴位连边，边权重由皮尔逊相关系数计算：

```cpp
// 特征：多电极皮肤电导时间序列相关性
r = Σ(xi-x̄)(yi-ȳ) / √[Σ(xi-x̄)² Σ(yi-ȳ)²]
weight = 0.5 + 0.5 * r
```

**分析指标**：
- **度中心性**（Degree Centrality）：节点连接数占比
- **接近中心性**（Closeness Centrality）：到其他节点平均最短路径倒数
- **中介中心性**（Betweenness Centrality）：最短路径经过频率
- **聚类系数**（Clustering Coefficient）：邻居节点互连比例
- **最优路径**：Dijkstra 算法计算两穴位间最优刺激路径

### 3. 异常检测

| 告警类型 | 检测规则 | 阈值 |
|----------|----------|------|
| 皮肤电导突降 | (prev - curr) / prev × 100% | ≥ 30% |
| 体温过高 | 红外温度 > 阈值 | > 38℃ |
| 体温过低 | 红外温度 < 阈值 | < 35℃ |
| 肌电异常 | Z-score = \|x - μ\| / σ | > 3.0 |

告警冷却：30秒内同穴位同类型不重复触发

### 4. 钉钉告警推送

使用签名安全机制的钉钉群机器人：
```
签名 = Base64(HMAC-SHA256( timestamp + "\n" + secret, secret ))
URL = webhook_url + "&timestamp=" + timestamp + "&sign=" + url_encode(sign)
```

---

## 🔌 REST API 文档

### 基础接口
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/health` | 服务健康检查 |
| GET | `/api/acupoints` | 获取所有穴位信息 |
| GET | `/api/meridians` | 获取所有经络信息 |

### 传感器数据
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/sensor/ingest` | 上报单条传感器数据 |
| POST | `/api/sensor/query` | 查询历史传感器数据 |

`/api/sensor/ingest` 请求体：
```json
{
  "volunteer_id": "V001",
  "acupoint_id": "ST36",
  "meridian_id": "ST",
  "timestamp": 1718000000000,
  "skin_conductance": 18.5,
  "skin_conductance_prev": 12.3,
  "infrared_temperature": 36.7,
  "emg_amplitude": 42.5,
  "emg_frequency": 68.2,
  "is_post_acupuncture": true,
  "session_id": "SES-001"
}
```

### 疗效评估
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/predict` | 随机森林预测针刺疗效 |
| POST | `/api/session/start` | 开始治疗会话 |
| POST | `/api/session/end` | 结束会话并返回评估 |
| POST | `/api/efficacy/query` | 查询历史疗效记录 |

### 网络分析
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/network/metrics` | 获取所有穴位拓扑指标 |
| GET | `/api/network/adjacency` | 获取经络邻接矩阵 |

### 告警
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/alerts` | 获取最近告警 |

### WebSocket
- 连接端点：`ws://host:port/ws`
- 实时推送消息类型：`sensor`, `alert`, `prediction`, `efficacy`, `network`

---

## 🖥️ 前端功能说明

### 主界面布局
```
┌─────────────────────────────────────────────────────────────────┐
│  状态栏：连接状态 / 数据包数 / 志愿者数 / 告警数                 │
├──────────┬──────────────────────────────────────┬─────────────────┤
│ 经络选择  │                                      │ 电导实时曲线     │
│ 志愿者    │         Canvas 经络穴位图            │ 温度实时曲线     │
│ 实时告警  │  （人体轮廓+经络+穴位热图+悬浮提示）  │ 肌电实时曲线     │
│          │                                      │ 特征重要性图     │
├──────────┴──────────────────────────────────────┴─────────────────┤
│  疗效指标卡片：得气强度 / 疼痛缓解率 / 置信度 / 经络通畅度        │
└─────────────────────────────────────────────────────────────────┘
```

### 交互功能
- 点击经络列表可单独显示某一经络
- 悬浮穴位显示：穴位名/拼音/经络/实时数据/主治
- 点击穴位切换右侧时间序列曲线
- 切换数据类型：皮肤电导 / 红外温度 / 肌电幅值
- 热图模式：根据传感器值动态渲染穴位颜色与大小
- 经络流向动画：流光效果展示经气循行

---

## 📊 数据库集合设计

| 集合 | 说明 | 索引 |
|------|------|------|
| `sensor_data` | 时序传感器数据（预计亿级） | `{volunteer_id, acupoint_id, timestamp: -1}` TTL 365天 |
| `efficacy_records` | 疗效记录 + 非结构化文本 | `{volunteer_id, session_id, timestamp: -1}` |
| `predictions` | 模型预测结果 | `{session_id, timestamp: -1}` |
| `alerts` | 异常告警记录 | `{acknowledged, timestamp: -1}` |
| `volunteers` | 志愿者信息（30名） | `{volunteer_id: 1}` unique |
| `acupoints` | 穴位基础信息（80+） | `{id: 1}` unique |
| `meridians` | 十四经基础信息 | `{id: 1}` unique |

---

## 🔧 配置项

### 钉钉机器人
修改 `backend/src/dingtalk_notifier.cpp` 或运行时传入：
```cpp
notifier.initialize(
    "https://oapi.dingtalk.com/robot/send?access_token=YOUR_TOKEN",
    "YOUR_SIGN_SECRET"
);
```

### 异常检测阈值
```cpp
detector.set_conductance_drop_threshold(30.0);   // % 
detector.set_temperature_high_threshold(38.0);    // ℃
detector.set_temperature_low_threshold(35.0);     // ℃
```

### BLE 模拟器参数
```bash
ble_simulator \
  --volunteers 30 \        # 志愿者数量
  --interval 100 \         # 上报间隔 ms
  --http http://127.0.0.1:8080 \  # 后端地址（或--no-http用UDP）
  --anomaly-prob 0.005     # 异常注入概率
```

---

## 🤝 技术栈

| 层 | 技术 |
|----|------|
| 前端 | HTML5 Canvas + ECharts 5 + 原生 JavaScript |
| 后端 | C++17 / Crow (HTTP+WS) / 标准库零依赖版本 |
| 数据库 | MongoDB 5.x（时序 + 文档） |
| 通信 | BLE Gateway → UDP 8081 / HTTP POST |
| 算法 | 随机森林回归 / 皮尔逊相关 / 图论分析 |
| 告警 | 钉钉群机器人 Webhook + HMAC-SHA256 签名 |

---

## 📝 License

中医药大学内部研究使用 · 学术用途免费

---

## 📮 常见问题

**Q: 启动后前端连接不上 WebSocket？**  
A: 零依赖版本不支持 WebSocket，前端会自动降级为内置数据模拟。完整版本需要 Crow 库。

**Q: 可以不装 MongoDB 吗？**  
A: 可以。`backend_single.cpp` 内置内存存储 + 默认穴位经络数据，无需外部数据库。

**Q: 如何接入真实 BLE 网关？**  
A: 配置 BLE 网关将数据以 UDP 报文格式 `vid|apid|ts|sc|scp|temp|emg_a|emg_f|mer|post|sid` 发送到后端 8081 端口，或通过 HTTP POST `/api/sensor/ingest` 上报 JSON。
