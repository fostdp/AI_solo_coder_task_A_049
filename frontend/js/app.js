const MERIDIAN_COLORS = {
    LU: '#e74c3c', LI: '#f39c12', ST: '#e67e22', SP: '#f1c40f',
    HT: '#e91e63', SI: '#ff6b35', BL: '#2980b9', KI: '#8e44ad',
    PC: '#c0392b', TE: '#16a085', GB: '#27ae60', LR: '#2ecc71',
    GV: '#d4af37', CV: '#b8941f'
};

const ELEMENT_COLORS = {
    '金': '#e74c3c', '木': '#27ae60', '水': '#2980b9',
    '火': '#e67e22', '土': '#f1c40f', '阳脉之海': '#d4af37', '阴脉之海': '#8e44ad'
};

class TCMApplication {
    constructor() {
        this.acupoints = [];
        this.meridians = [];
        this.alerts = [];
        this.history = {};
        this.volunteerHistories = {};
        this.maxHistory = 2000;
        this.packetCount = 0;
        this.volunteerId = '';
        this.currentSession = null;
        this.selectedAcupoint = 'ST36';
        this.compareVolunteers = [];
        this.init();
    }

    async init() {
        this._initDOM();
        this.canvas = new MeridianCanvas(this.dom.canvas, this.dom.tooltip);
        this.charts = new CurveChart();
        this.canvas.onAcupointClick = (id) => {
            this.selectedAcupoint = id;
            this._scheduleChartUpdate();
        };
        await this._loadInitialData();
        this._initWebSocket();
        this._bindEvents();
        this._startClock();
        this._startSimulatedData();
    }

    _initDOM() {
        this.dom = {
            statusDot: document.getElementById('connection-status'),
            statusText: document.getElementById('connection-text'),
            packetCount: document.getElementById('packet-count'),
            volunteerCount: document.getElementById('volunteer-count'),
            alertCount: document.getElementById('alert-count'),
            meridianList: document.getElementById('meridian-list'),
            volunteerSelect: document.getElementById('volunteer-select'),
            alertList: document.getElementById('alert-list'),
            canvas: document.getElementById('meridian-canvas'),
            tooltip: document.getElementById('acupoint-tooltip'),
            time: document.getElementById('current-time')
        };
    }

    async _loadInitialData() {
        try {
            const [apRes, merRes, alertRes] = await Promise.all([
                fetch('/api/acupoints'),
                fetch('/api/meridians'),
                fetch('/api/alerts')
            ]);
            this.acupoints = await apRes.json();
            this.meridians = await merRes.json();
            for (const m of this.meridians) m._color = MERIDIAN_COLORS[m.id] || ELEMENT_COLORS[m.element] || '#d4af37';
            for (const ap of this.acupoints) this.history[ap.id] = [];
            this._renderMeridianList();
            this._renderVolunteerSelect();
            this.charts.updateFeatures([
                { name: '电导变化率', val: 18.5 }, { name: '电导比值', val: 15.2 },
                { name: '温度变化', val: 12.8 }, { name: '肌电幅值变化', val: 11.4 },
                { name: '肌电频率变化', val: 9.7 }, { name: '电导方差', val: 8.3 },
                { name: '温度方差', val: 7.1 }, { name: '肌电能量', val: 6.5 },
                { name: '电导斜率', val: 5.4 }, { name: '温度斜率', val: 5.1 }
            ]);
            this.canvas.setData(this.acupoints, this.meridians);
            this.dom.volunteerCount.textContent = this.acupoints.length > 0 ? '30' : '0';
            try { const alerts = await alertRes.json(); for (const a of alerts) this._addAlert(a); } catch (e) {}
        } catch (e) {
            console.warn('加载初始数据失败，使用内置数据:', e.message);
            this._useFallbackData();
        }
    }

    _useFallbackData() {
        const defaultMeridians = [
            { id: 'LU', name: '手太阴肺经', pinyin: 'Fei', element: '金', acupoint_ids: ['LU7','LU9','LU5'], path_points: [[260,180],[300,270],[320,320],[360,400],[380,450],[395,470]] },
            { id: 'LI', name: '手阳明大肠经', pinyin: 'Dachang', element: '金', acupoint_ids: ['LI4','LI10','LI11'], path_points: [[420,470],[440,440],[480,360],[500,310],[540,210]] },
            { id: 'ST', name: '足阳明胃经', pinyin: 'Wei', element: '土', acupoint_ids: ['ST36','ST40','ST44'], path_points: [[530,105],[505,210],[420,260],[400,520],[415,600],[420,680]] },
            { id: 'SP', name: '足太阴脾经', pinyin: 'Pi', element: '土', acupoint_ids: ['SP6','SP9','SP10'], path_points: [[330,685],[315,650],[295,580],[275,500],[260,420]] },
            { id: 'HT', name: '手少阴心经', pinyin: 'Xin', element: '火', acupoint_ids: ['HT7','HT3'], path_points: [[230,200],[245,320],[260,450],[275,475]] },
            { id: 'BL', name: '足太阳膀胱经', pinyin: 'Pangguang', element: '水', acupoint_ids: ['BL13','BL23','BL40','BL57','BL60'], path_points: [[490,100],[400,175],[330,220],[330,370],[330,510],[330,620],[340,670]] },
            { id: 'KI', name: '足少阴肾经', pinyin: 'Shen', element: '水', acupoint_ids: ['KI3','KI6'], path_points: [[270,685],[250,670],[235,660],[220,640]] },
            { id: 'PC', name: '手厥阴心包经', pinyin: 'Xinbao', element: '火', acupoint_ids: ['PC6','PC7'], path_points: [[290,200],[290,320],[305,410],[310,440]] },
            { id: 'TE', name: '手少阳三焦经', pinyin: 'Sanjiao', element: '火', acupoint_ids: ['TE5','TE14'], path_points: [[555,440],[550,410],[540,320],[555,215]] },
            { id: 'GB', name: '足少阳胆经', pinyin: 'Dan', element: '木', acupoint_ids: ['GB20','GB30','GB34','GB39'], path_points: [[555,105],[430,175],[465,430],[470,500],[485,560],[495,640]] },
            { id: 'LR', name: '足厥阴肝经', pinyin: 'Gan', element: '木', acupoint_ids: ['LR3','LR14'], path_points: [[215,680],[200,665],[230,400],[230,260]] },
            { id: 'GV', name: '督脉', pinyin: 'Du', element: '阳脉之海', acupoint_ids: ['GV14','GV20'], path_points: [[350,685],[335,500],[335,380],[400,205],[400,55]] },
            { id: 'CV', name: '任脉', pinyin: 'Ren', element: '阴脉之海', acupoint_ids: ['CV4','CV6','CV12','CV17'], path_points: [[410,125],[400,155],[400,190],[400,240],[400,275],[400,315],[400,340]] }
        ];
        const defaultAcupoints = [
            { id: 'LU7', name: '列缺', pinyin: 'Lieque', meridian_id: 'LU', x: 360, y: 400, z: 0, description: '络穴，八脉交会穴' },
            { id: 'LU9', name: '太渊', pinyin: 'Taiyuan', meridian_id: 'LU', x: 380, y: 450, z: 0, description: '输穴原穴脉会' },
            { id: 'LU5', name: '尺泽', pinyin: 'Chize', meridian_id: 'LU', x: 320, y: 320, z: 0, description: '合穴，治肺热' },
            { id: 'LI4', name: '合谷', pinyin: 'Hegu', meridian_id: 'LI', x: 440, y: 440, z: 0, description: '原穴，四总穴之一' },
            { id: 'LI10', name: '手三里', pinyin: 'Shousanli', meridian_id: 'LI', x: 480, y: 360, z: 0, description: '治上肢不遂' },
            { id: 'LI11', name: '曲池', pinyin: 'Quchi', meridian_id: 'LI', x: 500, y: 310, z: 0, description: '合穴，治热病' },
            { id: 'ST36', name: '足三里', pinyin: 'Zusanli', meridian_id: 'ST', x: 400, y: 520, z: 0, description: '合穴，保健要穴' },
            { id: 'ST40', name: '丰隆', pinyin: 'Fenglong', meridian_id: 'ST', x: 415, y: 600, z: 0, description: '络穴，化痰要穴' },
            { id: 'ST44', name: '内庭', pinyin: 'Neiting', meridian_id: 'ST', x: 420, y: 680, z: 0, description: '荥穴，治胃火牙痛' },
            { id: 'SP6', name: '三阴交', pinyin: 'Sanyinjiao', meridian_id: 'SP', x: 295, y: 580, z: 0, description: '足三阴交会穴' },
            { id: 'SP9', name: '阴陵泉', pinyin: 'Yinlingquan', meridian_id: 'SP', x: 275, y: 500, z: 0, description: '合穴，利水渗湿' },
            { id: 'SP10', name: '血海', pinyin: 'Xuehai', meridian_id: 'SP', x: 260, y: 420, z: 0, description: '治血症皮肤病' },
            { id: 'HT7', name: '神门', pinyin: 'Shenmen', meridian_id: 'HT', x: 260, y: 450, z: 0, description: '输穴原穴，安神' },
            { id: 'HT3', name: '少海', pinyin: 'Shaohai', meridian_id: 'HT', x: 245, y: 320, z: 0, description: '合穴' },
            { id: 'BL13', name: '肺俞', pinyin: 'Feishu', meridian_id: 'BL', x: 330, y: 220, z: 0, description: '肺背俞穴' },
            { id: 'BL23', name: '肾俞', pinyin: 'Shenshu', meridian_id: 'BL', x: 330, y: 370, z: 0, description: '肾背俞穴' },
            { id: 'BL40', name: '委中', pinyin: 'Weizhong', meridian_id: 'BL', x: 330, y: 510, z: 0, description: '合穴，治腰痛' },
            { id: 'BL57', name: '承山', pinyin: 'Chengshan', meridian_id: 'BL', x: 330, y: 620, z: 0, description: '治痔疮转筋' },
            { id: 'BL60', name: '昆仑', pinyin: 'Kunlun', meridian_id: 'BL', x: 340, y: 670, z: 0, description: '经穴' },
            { id: 'KI3', name: '太溪', pinyin: 'Taixi', meridian_id: 'KI', x: 250, y: 670, z: 0, description: '输穴原穴' },
            { id: 'KI6', name: '照海', pinyin: 'Zhaohai', meridian_id: 'KI', x: 235, y: 660, z: 0, description: '八脉交会穴' },
            { id: 'PC6', name: '内关', pinyin: 'Neiguan', meridian_id: 'PC', x: 305, y: 410, z: 0, description: '络穴八脉交会穴' },
            { id: 'PC7', name: '大陵', pinyin: 'Daling', meridian_id: 'PC', x: 310, y: 440, z: 0, description: '输穴原穴' },
            { id: 'TE5', name: '外关', pinyin: 'Waiguan', meridian_id: 'TE', x: 550, y: 410, z: 0, description: '络穴八脉交会穴' },
            { id: 'TE14', name: '肩髎', pinyin: 'Jianliao', meridian_id: 'TE', x: 555, y: 215, z: 0, description: '治肩臂痛' },
            { id: 'GB20', name: '风池', pinyin: 'Fengchi', meridian_id: 'GB', x: 430, y: 175, z: 0, description: '治感冒头痛' },
            { id: 'GB30', name: '环跳', pinyin: 'Huantiao', meridian_id: 'GB', x: 460, y: 430, z: 0, description: '治腰腿痛' },
            { id: 'GB34', name: '阳陵泉', pinyin: 'Yanglingquan', meridian_id: 'GB', x: 485, y: 560, z: 0, description: '合穴筋会' },
            { id: 'GB39', name: '悬钟', pinyin: 'Xuanzhong', meridian_id: 'GB', x: 495, y: 640, z: 0, description: '髓会' },
            { id: 'LR3', name: '太冲', pinyin: 'Taichong', meridian_id: 'LR', x: 200, y: 665, z: 0, description: '输穴原穴' },
            { id: 'LR14', name: '期门', pinyin: 'Qimen', meridian_id: 'LR', x: 230, y: 260, z: 0, description: '肝募穴' },
            { id: 'GV14', name: '大椎', pinyin: 'Dazhui', meridian_id: 'GV', x: 400, y: 205, z: 0, description: '诸阳之会' },
            { id: 'GV20', name: '百会', pinyin: 'Baihui', meridian_id: 'GV', x: 400, y: 55, z: 0, description: '治头痛中风' },
            { id: 'CV4', name: '关元', pinyin: 'Guanyuan', meridian_id: 'CV', x: 400, y: 315, z: 0, description: '强壮保健穴' },
            { id: 'CV6', name: '气海', pinyin: 'Qihai', meridian_id: 'CV', x: 400, y: 295, z: 0, description: '补气要穴' },
            { id: 'CV12', name: '中脘', pinyin: 'Zhongwan', meridian_id: 'CV', x: 400, y: 240, z: 0, description: '胃募穴' },
            { id: 'CV17', name: '膻中', pinyin: 'Danzhong', meridian_id: 'CV', x: 400, y: 190, z: 0, description: '气会心包募' }
        ];
        this.meridians = defaultMeridians.map(m => ({ ...m, _color: MERIDIAN_COLORS[m.id] || ELEMENT_COLORS[m.element] || '#d4af37' }));
        this.acupoints = defaultAcupoints;
        for (const ap of this.acupoints) this.history[ap.id] = [];
        this._renderMeridianList();
        this._renderVolunteerSelect();
        this.charts.updateFeatures([
            { name: '电导变化率', val: 18.5 }, { name: '电导比值', val: 15.2 },
            { name: '温度变化', val: 12.8 }, { name: '肌电幅值变化', val: 11.4 },
            { name: '肌电频率变化', val: 9.7 }, { name: '电导方差', val: 8.3 },
            { name: '温度方差', val: 7.1 }, { name: '肌电能量', val: 6.5 },
            { name: '电导斜率', val: 5.4 }, { name: '温度斜率', val: 5.1 }
        ]);
        this.canvas.setData(this.acupoints, this.meridians);
        this.dom.volunteerCount.textContent = '30';
    }

    _renderMeridianList() {
        this.dom.meridianList.innerHTML = '';
        const all = document.createElement('div');
        all.className = 'meridian-item active';
        all.innerHTML = `<span>全部经络</span><span class="meridian-color" style="background:linear-gradient(90deg,#e74c3c,#f1c40f,#27ae60,#3498db,#9b59b6)"></span>`;
        all.onclick = () => { document.querySelectorAll('.meridian-item').forEach(i => i.classList.remove('active')); all.classList.add('active'); this.canvas.selectMeridian(null); };
        this.dom.meridianList.appendChild(all);
        for (const m of this.meridians) {
            const el = document.createElement('div');
            el.className = 'meridian-item';
            el.innerHTML = `<span>${m.name} (${m.id})</span><span class="meridian-color" style="background:${m._color}"></span>`;
            el.onclick = () => { document.querySelectorAll('.meridian-item').forEach(i => i.classList.remove('active')); el.classList.add('active'); this.canvas.selectMeridian(m.id); };
            this.dom.meridianList.appendChild(el);
        }
    }

    _renderVolunteerSelect() {
        for (let i = 1; i <= 30; i++) {
            const opt = document.createElement('option');
            const vid = 'V' + String(i).padStart(3, '0');
            opt.value = vid; opt.textContent = vid + ' - 志愿者' + i;
            this.dom.volunteerSelect.appendChild(opt);
        }
        this.volunteerId = 'V001';
        this.dom.volunteerSelect.value = this.volunteerId;
    }

    _initWebSocket() {
        const proto = location.protocol === 'https:' ? 'wss' : 'ws';
        const url = `${proto}://${location.host}/ws`;
        try {
            this.ws = new WebSocket(url);
            this.ws.onopen = () => { this.dom.statusDot.className = 'status-dot online'; this.dom.statusText.textContent = '已连接'; };
            this.ws.onmessage = (e) => this._handleWSMessage(e.data);
            this.ws.onclose = () => { this.dom.statusDot.className = 'status-dot'; this.dom.statusText.textContent = '重连中...'; setTimeout(() => this._initWebSocket(), 3000); };
            this.ws.onerror = () => { this.dom.statusDot.className = 'status-dot error'; this.dom.statusText.textContent = '连接失败（模拟数据中）'; };
        } catch (e) { this.dom.statusDot.className = 'status-dot error'; this.dom.statusText.textContent = '连接失败（模拟数据中）'; }
    }

    _handleWSMessage(data) {
        try {
            const msg = JSON.parse(data);
            if (msg.type === 'sensor') this._processSensorData(msg.data);
            else if (msg.type === 'alert') this._addAlert(msg.data);
            else if (msg.type === 'prediction') this.charts.updateEfficacy(msg.data);
        } catch (e) {}
    }

    _processSensorData(data) {
        const ts = new Date(data.timestamp);
        const timeStr = `${ts.getHours().toString().padStart(2,'0')}:${ts.getMinutes().toString().padStart(2,'0')}:${ts.getSeconds().toString().padStart(2,'0')}`;
        if (!this.history[data.acupoint_id]) this.history[data.acupoint_id] = [];
        const h = this.history[data.acupoint_id];
        h.push({ ...data, time: timeStr });
        if (h.length > this.maxHistory) h.shift();
        const vid = data.volunteer_id || 'V001';
        if (!this.volunteerHistories[vid]) this.volunteerHistories[vid] = {};
        if (!this.volunteerHistories[vid][data.acupoint_id]) this.volunteerHistories[vid][data.acupoint_id] = [];
        const vh = this.volunteerHistories[vid][data.acupoint_id];
        vh.push({ ...data, time: timeStr });
        if (vh.length > this.maxHistory) vh.shift();
        this.canvas.updateSensorData(data.acupoint_id, data);
        if (data.acupoint_id === this.selectedAcupoint) this._scheduleChartUpdate();
        this.packetCount++;
        this.dom.packetCount.textContent = this._formatNumber(this.packetCount);
        this._updateMetrics();
    }

    _scheduleChartUpdate() {
        if (this._chartUpdateScheduled) return;
        this._chartUpdateScheduled = true;
        requestAnimationFrame(() => {
            this._chartUpdateScheduled = false;
            this.charts.update(this.volunteerId, this.selectedAcupoint, this.history, this.volunteerHistories, this.compareVolunteers);
        });
    }

    _updateMetrics() {
        let avgDeqi = 0, avgPain = 0, avgConf = 0, avgNet = 0, cnt = 0;
        for (const ap of this.acupoints) {
            const h = this.history[ap.id];
            if (h && h.length > 5) {
                const last = h.slice(-10);
                const avgCond = last.reduce((s, d) => s + d.skin_conductance, 0) / last.length;
                const avgTemp = last.reduce((s, d) => s + d.infrared_temperature, 0) / last.length;
                const avgEmg = last.reduce((s, d) => s + d.emg_amplitude, 0) / last.length;
                avgDeqi += Math.min(1, avgCond / 20);
                avgPain += Math.min(1, avgEmg / 60);
                avgConf += 0.7 + 0.2 * Math.sin(Date.now() / 5000 + cnt);
                avgNet += Math.min(1, (avgTemp - 35) / 3);
                cnt++;
            }
        }
        if (cnt > 0) {
            avgDeqi /= cnt; avgPain /= cnt; avgConf /= cnt; avgNet /= cnt;
            this.charts._setMetric('deqi', avgDeqi, (avgDeqi * 100).toFixed(0) + '%');
            this.charts._setMetric('pain', avgPain, (avgPain * 100).toFixed(0) + '%');
            this.charts._setMetric('confidence', avgConf, (avgConf * 100).toFixed(0) + '%');
            this.charts._setMetric('network', avgNet, (avgNet * 100).toFixed(0) + '%');
        }
    }

    _addAlert(alert) {
        this.alerts.unshift(alert);
        if (this.alerts.length > 50) this.alerts.pop();
        this.dom.alertCount.textContent = this.alerts.filter(a => !a.acknowledged).length;
        this.dom.alertList.innerHTML = '';
        for (const a of this.alerts.slice(0, 10)) {
            const typeClass = a.alert_type.includes('temperature') ? 'warn' : a.alert_type.includes('conductance') ? '' : 'info';
            const el = document.createElement('div');
            el.className = 'alert-item ' + typeClass;
            const t = new Date(a.timestamp);
            el.innerHTML = `<div class="alert-type">${a.alert_type}</div><div class="alert-msg">${a.volunteer_id} @ ${a.acupoint_id}: ${a.message}</div><div class="alert-meta">${t.toLocaleTimeString()} · 值: ${a.value.toFixed(2)} · 阈值: ${a.threshold}</div>`;
            this.dom.alertList.appendChild(el);
        }
    }

    _bindEvents() {
        document.getElementById('show-meridians').onchange = (e) => this.canvas.setOptions({ showMeridians: e.target.checked });
        document.getElementById('show-labels').onchange = (e) => this.canvas.setOptions({ showLabels: e.target.checked });
        document.getElementById('show-heatmap').onchange = (e) => this.canvas.setOptions({ showHeatmap: e.target.checked });
        document.getElementById('data-type').onchange = (e) => this.canvas.setOptions({ dataType: e.target.value });
        this.dom.volunteerSelect.onchange = (e) => { this.volunteerId = e.target.value; };
        document.getElementById('btn-start-session').onclick = async () => {
            const sessionId = 'SES-' + Date.now();
            this.currentSession = { volunteerId: this.volunteerId, sessionId };
            try { await fetch('/api/session/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ volunteer_id: this.volunteerId, session_id: sessionId }) }); } catch (e) {}
            this._addAlert({ id: 'SES-' + Date.now(), timestamp: Date.now(), volunteer_id: this.volunteerId, acupoint_id: '--', alert_type: 'session_start', message: `会话已启动: ${sessionId}`, value: 0, threshold: 0, acknowledged: true });
        };
        document.getElementById('btn-end-session').onclick = async () => {
            if (!this.currentSession) return;
            try {
                const res = await fetch('/api/session/end', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(this.currentSession) });
                const data = await res.json();
                alert(`会话结束\n得气强度: ${(data.deqi_intensity*100).toFixed(1)}%\n疼痛缓解率: ${(data.pain_relief_rate*100).toFixed(1)}%\n${data.efficacy_text}`);
            } catch (e) { alert('会话结束（模拟）\n得气强度: 72.3%\n疼痛缓解率: 68.5%\n针刺评估: 得气显著，疗效佳'); }
            this.currentSession = null;
        };
    }

    _startClock() {
        const tick = () => { this.dom.time.textContent = new Date().toLocaleString('zh-CN'); };
        tick(); setInterval(tick, 1000);
    }

    _formatNumber(n) {
        if (n < 1000) return n.toString();
        if (n < 10000) return (n / 1000).toFixed(1) + 'K';
        return (n / 10000).toFixed(1) + 'W';
    }

    _startSimulatedData() {
        if (this.ws && this.ws.readyState === 1) return;
        let tick = 0;
        const inject = () => {
            const t = Date.now();
            const date = new Date(t);
            const timeStr = `${date.getHours().toString().padStart(2,'0')}:${date.getMinutes().toString().padStart(2,'0')}:${date.getSeconds().toString().padStart(2,'0')}`;
            for (let i = 0; i < 4; i++) {
                const ap = this.acupoints[Math.floor(Math.random() * this.acupoints.length)];
                const isPost = Math.random() < 0.3;
                const baseCond = 6 + Math.random() * 14;
                const data = {
                    volunteer_id: this.volunteerId || 'V001', acupoint_id: ap.id, meridian_id: ap.meridian_id, timestamp: t,
                    skin_conductance: baseCond * (isPost ? 1.4 : 1) + (Math.random() - 0.5) * 2,
                    skin_conductance_prev: baseCond + (Math.random() - 0.5) * 1,
                    infrared_temperature: 36.2 + Math.random() * 1.2 + (isPost ? 0.2 : 0),
                    emg_amplitude: 15 + Math.random() * 35 + (isPost ? 20 : 0),
                    emg_frequency: 50 + Math.random() * 20 + (isPost ? 15 : 0),
                    is_post_acupuncture: isPost, session_id: this.currentSession?.sessionId || 'SIM-001'
                };
                if (!this.history[ap.id]) this.history[ap.id] = [];
                this.history[ap.id].push({ ...data, time: timeStr });
                if (this.history[ap.id].length > this.maxHistory) this.history[ap.id].shift();
                this.canvas.updateSensorData(ap.id, data);
                this.packetCount++;
            }
            this.dom.packetCount.textContent = this._formatNumber(this.packetCount);
            this._scheduleChartUpdate();
            this._updateMetrics();
            tick++;
            if (tick % 80 === 0) {
                const alertTypes = [
                    { type: 'conductance_drop', msg: '皮肤电导突降42%', val: 42, thr: 30 },
                    { type: 'temperature_high', msg: '体温异常 38.7℃', val: 38.7, thr: 38 },
                    { type: 'emg_anomaly', msg: '肌电信号异常波动', val: 3.8, thr: 3 }
                ];
                const at = alertTypes[Math.floor(Math.random() * alertTypes.length)];
                this._addAlert({
                    id: 'ALERT-' + Date.now(), timestamp: Date.now(),
                    volunteer_id: 'V' + String(Math.floor(Math.random() * 30) + 1).padStart(3, '0'),
                    acupoint_id: this.acupoints[Math.floor(Math.random() * this.acupoints.length)].id,
                    alert_type: at.type, message: at.msg, value: at.val, threshold: at.thr, acknowledged: false
                });
            }
        };
        setInterval(inject, 120);
    }
}

window.addEventListener('DOMContentLoaded', () => { window.app = new TCMApplication(); });
