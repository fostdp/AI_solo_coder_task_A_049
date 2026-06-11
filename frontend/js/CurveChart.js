class CurveChart {
    constructor() {
        this.charts = {};
        this.maxRenderPoints = 500;
        this.initLazySeries = 5;
        this._chartUpdateScheduled = false;
        this._init();
    }

    _init() {
        const axisStyle = {
            axisLine: { lineStyle: { color: '#3d4766' } },
            axisLabel: { color: '#8a93ac', fontSize: 10 },
            splitLine: { lineStyle: { color: '#252c42' } }
        };

        const dataZoomConfig = [
            { type: 'inside', start: 70, end: 100, zoomOnMouseWheel: 'ctrl', moveOnMouseMove: true, moveOnMouseWheel: false },
            { type: 'slider', show: true, height: 14, bottom: 4, borderColor: '#3d4766', backgroundColor: '#1a1f30',
              fillerColor: 'rgba(212,175,55,0.15)', handleStyle: { color: '#d4af37' }, textStyle: { color: '#8a93ac', fontSize: 9 }, start: 70, end: 100 }
        ];

        this.charts.conductance = echarts.init(document.getElementById('chart-conductance'));
        this.charts.conductance.setOption({
            title: { text: '皮肤电导 (μS)', left: 10, top: 4, textStyle: { color: '#c9d1e4', fontSize: 12, fontWeight: 'normal' } },
            grid: { left: 45, right: 15, top: 35, bottom: 50 },
            tooltip: { trigger: 'axis', backgroundColor: 'rgba(20,24,38,0.95)', borderColor: '#d4af37', textStyle: { color: '#e4e8f0' } },
            legend: { data: [], top: 2, right: 10, textStyle: { color: '#8a93ac', fontSize: 10 }, itemWidth: 12, itemHeight: 8 },
            dataZoom: dataZoomConfig,
            xAxis: { type: 'category', data: [], ...axisStyle },
            yAxis: { type: 'value', ...axisStyle, scale: true },
            series: []
        });

        this.charts.temperature = echarts.init(document.getElementById('chart-temperature'));
        this.charts.temperature.setOption({
            title: { text: '红外温度 (℃)', left: 10, top: 4, textStyle: { color: '#c9d1e4', fontSize: 12, fontWeight: 'normal' } },
            grid: { left: 40, right: 15, top: 30, bottom: 50 },
            tooltip: { trigger: 'axis', backgroundColor: 'rgba(20,24,38,0.95)', borderColor: '#e74c3c', textStyle: { color: '#e4e8f0' } },
            dataZoom: dataZoomConfig,
            xAxis: { type: 'category', data: [], ...axisStyle },
            yAxis: { type: 'value', min: 35, max: 39, ...axisStyle },
            series: [{
                name: '温度', type: 'line', data: [], smooth: true, showSymbol: false,
                sampling: 'lttb',
                lineStyle: { color: '#e74c3c', width: 2 },
                areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                    colorStops: [{ offset: 0, color: 'rgba(231,76,60,0.25)' }, { offset: 1, color: 'rgba(231,76,60,0)' }] } },
                markLine: { silent: true, symbol: 'none', lineStyle: { color: '#f5a623', type: 'dashed' },
                    data: [{ yAxis: 38, label: { formatter: '38℃ 阈值', color: '#f5a623', fontSize: 9 } }] }
            }]
        });

        this.charts.emg = echarts.init(document.getElementById('chart-emg'));
        this.charts.emg.setOption({
            title: { text: '肌电幅值 (μV)', left: 10, top: 4, textStyle: { color: '#c9d1e4', fontSize: 12, fontWeight: 'normal' } },
            grid: { left: 40, right: 15, top: 30, bottom: 50 },
            tooltip: { trigger: 'axis', backgroundColor: 'rgba(20,24,38,0.95)', borderColor: '#3498db', textStyle: { color: '#e4e8f0' } },
            dataZoom: dataZoomConfig,
            xAxis: { type: 'category', data: [], ...axisStyle },
            yAxis: { type: 'value', ...axisStyle, scale: true },
            series: [{
                name: '肌电', type: 'line', data: [], smooth: true, showSymbol: false,
                sampling: 'lttb',
                lineStyle: { color: '#3498db', width: 2 },
                areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                    colorStops: [{ offset: 0, color: 'rgba(52,152,219,0.3)' }, { offset: 1, color: 'rgba(52,152,219,0)' }] } }
            }]
        });

        this.charts.features = echarts.init(document.getElementById('chart-features'));
        this.charts.features.setOption({
            title: { text: '随机森林特征重要性', left: 10, top: 4, textStyle: { color: '#c9d1e4', fontSize: 12, fontWeight: 'normal' } },
            grid: { left: 100, right: 15, top: 28, bottom: 8 },
            tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' }, backgroundColor: 'rgba(20,24,38,0.95)', borderColor: '#9b59b6', textStyle: { color: '#e4e8f0' } },
            xAxis: { type: 'value', ...axisStyle },
            yAxis: { type: 'category', data: [], ...axisStyle, axisLabel: { fontSize: 10, color: '#c9d1e4' } },
            series: [{
                type: 'bar', data: [],
                itemStyle: { color: { type: 'linear', x: 0, y: 0, x2: 1, y2: 0,
                    colorStops: [{ offset: 0, color: '#9b59b6' }, { offset: 1, color: '#3498db' }] }, borderRadius: [0, 3, 3, 0] },
                label: { show: true, position: 'right', color: '#c9d1e4', fontSize: 10, formatter: '{c}%' }
            }]
        });

        window.addEventListener('resize', () => {
            for (const k in this.charts) this.charts[k].resize();
        });
    }

    lttbDownsample(data, threshold) {
        if (data.length <= threshold || threshold < 3) return data;
        const sampled = [data[0]];
        const bucketSize = (data.length - 2) / (threshold - 2);
        let a = 0;
        for (let i = 0; i < threshold - 2; i++) {
            const avgRangeStart = Math.floor((i + 1) * bucketSize) + 1;
            const avgRangeEnd = Math.floor((i + 2) * bucketSize) + 1;
            const avgRange = Math.min(avgRangeEnd, data.length) - avgRangeStart;
            let avgX = 0, avgY = 0;
            for (let j = avgRangeStart; j < avgRangeEnd; j++) { avgX += j; avgY += data[j][1]; }
            avgX /= avgRange; avgY /= avgRange;
            const rangeOffs = Math.floor(i * bucketSize) + 1;
            const rangeTo = Math.floor((i + 1) * bucketSize) + 1;
            const pointA = [a, data[a][1]];
            let maxArea = -1, maxAreaPoint = 0;
            for (let j = rangeOffs; j < rangeTo; j++) {
                const area = Math.abs(
                    (pointA[0] - avgX) * (data[j][1] - pointA[1]) -
                    (pointA[0] - j) * (avgY - pointA[1])
                ) * 0.5;
                if (area > maxArea) { maxArea = area; maxAreaPoint = j; }
            }
            sampled.push(data[maxAreaPoint]);
            a = maxAreaPoint;
        }
        sampled.push(data[data.length - 1]);
        return sampled;
    }

    downsampleArray(arr, threshold) {
        if (arr.length <= threshold) return arr;
        const tuples = arr.map((v, i) => [i, v]);
        const ds = this.lttbDownsample(tuples, threshold);
        return ds.map(t => t[1]);
    }

    getVolunteerColor(idx) {
        const palette = [
            '#d4af37', '#e74c3c', '#3498db', '#27ae60', '#9b59b6',
            '#f39c12', '#1abc9c', '#e67e22', '#34495e', '#e91e63',
            '#00bcd4', '#8bc34a', '#ff9800', '#673ab7', '#2196f3',
            '#ff5722', '#009688', '#795548', '#607d8b', '#ff4081',
            '#536dfe', '#448aff', '#69f0ae', '#ffd740', '#e040fb',
            '#18ffff', '#ff6e40', '#eeff41', '#b388ff', '#8c9eff'
        ];
        return palette[idx % palette.length];
    }

    scheduleUpdate() {
        if (this._chartUpdateScheduled) return;
        this._chartUpdateScheduled = true;
        requestAnimationFrame(() => {
            this._chartUpdateScheduled = false;
        });
    }

    update(primaryVid, selectedAcupoint, history, volunteerHistories, compareVolunteers) {
        const apId = selectedAcupoint;
        const volIds = new Set([primaryVid]);
        if (compareVolunteers) compareVolunteers.forEach(v => volIds.add(v));
        const allVolunteerIds = Object.keys(volunteerHistories)
            .filter(v => volunteerHistories[v] && volunteerHistories[v][apId]);
        allVolunteerIds.forEach(v => volIds.add(v));

        const vidList = Array.from(volIds).slice(0, 30);
        const displayCount = Math.min(vidList.length, this.initLazySeries);

        const primaryH = (volunteerHistories[primaryVid] && volunteerHistories[primaryVid][apId])
            ? volunteerHistories[primaryVid][apId]
            : (history[apId] || []);

        let times = primaryH.map(d => d.time);
        const needTimes = times.length;
        const tempVals = primaryH.map(d => Number(d.infrared_temperature.toFixed(2)));
        const emgVals = primaryH.map(d => Number(d.emg_amplitude.toFixed(1)));

        const dsTimes = this.downsampleArray(times, this.maxRenderPoints);
        const dsTemp = this.downsampleArray(tempVals, this.maxRenderPoints);
        const dsEmg = this.downsampleArray(emgVals, this.maxRenderPoints);

        const condSeries = [];
        const condLegend = [];

        for (let i = 0; i < displayCount; i++) {
            const vid = vidList[i];
            const source = (volunteerHistories[vid] && volunteerHistories[vid][apId])
                ? volunteerHistories[vid][apId] : primaryH;
            let data = source.map(d => Number(d.skin_conductance.toFixed(2)));
            if (data.length !== needTimes) {
                const pad = needTimes - data.length;
                if (pad > 0) data = new Array(pad).fill(null).concat(data);
                else data = data.slice(-needTimes);
            }
            data = this.downsampleArray(data, this.maxRenderPoints);
            const isPrimary = vid === primaryVid;
            condSeries.push({
                name: `${vid}`, type: 'line', data, smooth: true, showSymbol: false,
                sampling: 'lttb', large: true, largeThreshold: 500,
                lineStyle: { color: this.getVolunteerColor(i), width: isPrimary ? 2.5 : 1.2, opacity: isPrimary ? 1.0 : 0.65 },
                areaStyle: isPrimary ? { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                    colorStops: [{ offset: 0, color: this.getVolunteerColor(i) + '55' }, { offset: 1, color: this.getVolunteerColor(i) + '00' }] } } : null,
                emphasis: { focus: 'series', lineStyle: { width: 3 } },
                z: isPrimary ? 100 : (100 - i)
            });
            condLegend.push(`${vid}`);
        }

        if (vidList.length > displayCount) {
            condSeries.push({ name: `+${vidList.length - displayCount} 志愿者(点击加载)`, type: 'line', data: [], lineStyle: { color: 'transparent' }, symbol: 'none', silent: true });
        }

        this.charts.conductance.setOption({
            title: { text: `皮肤电导 - ${apId} · 显示${displayCount}/${vidList.length}志愿者 (μS)`, left: 10, top: 4, textStyle: { color: '#c9d1e4', fontSize: 12, fontWeight: 'normal' } },
            xAxis: { data: dsTimes },
            legend: { data: condLegend },
            series: condSeries
        }, { lazyUpdate: true });

        this.charts.temperature.setOption({ xAxis: { data: dsTimes }, series: [{ data: dsTemp }] }, { lazyUpdate: true });
        this.charts.emg.setOption({ xAxis: { data: dsTimes }, series: [{ data: dsEmg }] }, { lazyUpdate: true });
    }

    updateFeatures(features) {
        this.charts.features.setOption({
            yAxis: { data: features.map(f => f.name).reverse() },
            series: [{ data: features.map(f => f.val).reverse() }]
        });
    }

    updateEfficacy(pred) {
        this._setMetric('deqi', pred.predicted_deqi, (pred.predicted_deqi * 100).toFixed(0) + '%');
        this._setMetric('pain', pred.predicted_pain_relief, (pred.predicted_pain_relief * 100).toFixed(0) + '%');
        this._setMetric('confidence', pred.confidence, (pred.confidence * 100).toFixed(0) + '%');
    }

    _setMetric(key, ratio, text) {
        const el = document.getElementById(key + '-value');
        const bar = document.getElementById(key + '-bar');
        if (el) el.textContent = text;
        if (bar) bar.style.width = (ratio * 100).toFixed(0) + '%';
    }
}
