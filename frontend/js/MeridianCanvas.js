class MeridianCanvas {
    constructor(canvas, tooltip) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.tooltip = tooltip;
        this.acupoints = [];
        this.meridians = [];
        this.sensorData = {};
        this.selectedMeridian = null;
        this.hoveredAcupoint = null;
        this.onAcupointClick = null;
        this.options = {
            showMeridians: true,
            showLabels: true,
            showHeatmap: true,
            dataType: 'conductance'
        };
        this.animTime = 0;
        this._dirty = true;
        this._staticCanvas = document.createElement('canvas');
        this._staticCanvas.width = canvas.width;
        this._staticCanvas.height = canvas.height;
        this._staticDirty = true;
        this._initEvents();
        this._startAnimation();
    }

    _initEvents() {
        this.canvas.addEventListener('mousemove', (e) => this._onMouseMove(e));
        this.canvas.addEventListener('mouseleave', () => {
            this.hoveredAcupoint = null;
            this.tooltip.style.display = 'none';
        });
        this.canvas.addEventListener('click', (e) => this._onClick(e));
    }

    _startAnimation() {
        const animate = () => {
            this.animTime += 0.02;
            this.render();
            requestAnimationFrame(animate);
        };
        animate();
    }

    setData(acupoints, meridians) {
        this.acupoints = acupoints;
        this.meridians = meridians;
        this._staticDirty = true;
    }

    setOptions(opts) {
        const prev = { ...this.options };
        Object.assign(this.options, opts);
        if (opts.showMeridians !== prev.showMeridians ||
            opts.dataType !== prev.dataType) {
            this._staticDirty = true;
        }
        this._dirty = true;
    }

    selectMeridian(meridianId) {
        this.selectedMeridian = meridianId;
        this._staticDirty = true;
    }

    updateSensorData(acupointId, data) {
        this.sensorData[acupointId] = data;
        this._dirty = true;
    }

    markDirty() {
        this._dirty = true;
        this._staticDirty = true;
    }

    getValueColor(value, min, max) {
        const ratio = Math.max(0, Math.min(1, (value - min) / (max - min)));
        const hue = (1 - ratio) * 240;
        return `hsla(${hue}, 85%, 55%, 0.9)`;
    }

    _getSensorValue(ap) {
        const d = this.sensorData[ap.id];
        if (!d) return null;
        switch (this.options.dataType) {
            case 'conductance': return d.skin_conductance;
            case 'temperature': return d.infrared_temperature;
            case 'emg': return d.emg_amplitude;
            default: return d.skin_conductance;
        }
    }

    _getValueRange() {
        switch (this.options.dataType) {
            case 'conductance': return [2, 25];
            case 'temperature': return [35, 39];
            case 'emg': return [5, 80];
            default: return [0, 100];
        }
    }

    _drawBodyOutline(ctx) {
        const cx = 400, cy = 380;
        ctx.save();
        ctx.strokeStyle = 'rgba(212,175,55,0.15)';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 6]);
        ctx.beginPath();
        ctx.arc(cx, 80, 45, 0, Math.PI * 2);
        ctx.stroke();

        ctx.setLineDash([]);
        ctx.strokeStyle = 'rgba(212,175,55,0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(cx - 25, 125);
        ctx.lineTo(cx - 45, 180);
        ctx.lineTo(cx - 60, 260);
        ctx.lineTo(cx - 30, 280);
        ctx.lineTo(cx - 50, 380);
        ctx.lineTo(cx - 55, 500);
        ctx.lineTo(cx - 40, 600);
        ctx.lineTo(cx - 45, 680);
        ctx.lineTo(cx - 10, 680);
        ctx.lineTo(cx, 600);
        ctx.lineTo(cx + 10, 680);
        ctx.lineTo(cx + 45, 680);
        ctx.lineTo(cx + 40, 600);
        ctx.lineTo(cx + 55, 500);
        ctx.lineTo(cx + 50, 380);
        ctx.lineTo(cx + 30, 280);
        ctx.lineTo(cx + 60, 260);
        ctx.lineTo(cx + 45, 180);
        ctx.lineTo(cx + 25, 125);
        ctx.closePath();
        ctx.stroke();

        ctx.strokeStyle = 'rgba(212,175,55,0.1)';
        ctx.beginPath();
        ctx.moveTo(cx, 125);
        ctx.lineTo(cx, 680);
        ctx.stroke();
        ctx.restore();
    }

    _drawMeridian(ctx, meridian, active) {
        const color = active ? meridian._color || '#d4af37' : 'rgba(212,175,55,0.25)';
        const width = active ? 2.5 : 1;
        const alpha = active ? 0.85 : 0.35;
        if (!meridian.path_points || meridian.path_points.length < 2) return;

        ctx.save();
        ctx.strokeStyle = color;
        ctx.globalAlpha = alpha;
        ctx.lineWidth = width;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        if (active) { ctx.shadowColor = color; ctx.shadowBlur = 8; }

        ctx.beginPath();
        const pts = meridian.path_points;
        ctx.moveTo(pts[0][0], pts[0][1]);
        for (let i = 1; i < pts.length; i++) {
            const x0 = pts[i-1][0], y0 = pts[i-1][1];
            const x1 = pts[i][0], y1 = pts[i][1];
            const mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            ctx.quadraticCurveTo(x0, y0, mx, my);
        }
        ctx.lineTo(pts[pts.length-1][0], pts[pts.length-1][1]);
        ctx.stroke();

        if (active) {
            const flowOffset = (this.animTime * 80) % 100;
            ctx.setLineDash([15, 30]);
            ctx.lineDashOffset = -flowOffset;
            ctx.strokeStyle = '#ffffff';
            ctx.globalAlpha = 0.5;
            ctx.lineWidth = 1.5;
            ctx.shadowBlur = 4;
            ctx.stroke();
        }
        ctx.restore();
    }

    _renderStatic() {
        const ctx = this._staticCanvas.getContext('2d');
        ctx.clearRect(0, 0, this._staticCanvas.width, this._staticCanvas.height);
        this._drawBodyOutline(ctx);
        if (this.options.showMeridians) {
            for (const m of this.meridians) {
                const active = !this.selectedMeridian || m.id === this.selectedMeridian;
                this._drawMeridian(ctx, m, active);
            }
        }
        this._staticDirty = false;
    }

    _drawAcupoint(ctx, ap) {
        const isHovered = this.hoveredAcupoint === ap.id;
        const val = this._getSensorValue(ap);
        const [vmin, vmax] = this._getValueRange();

        let baseRadius = 5;
        let fillColor = 'rgba(150,150,170,0.5)';
        let strokeColor = 'rgba(212,175,55,0.6)';

        if (val !== null && this.options.showHeatmap) {
            fillColor = this.getValueColor(val, vmin, vmax);
            const ratio = (val - vmin) / (vmax - vmin);
            baseRadius = 5 + ratio * 6;
        }

        const inSelected = !this.selectedMeridian || (ap.meridian_id === this.selectedMeridian);
        if (!inSelected) ctx.globalAlpha = 0.25;

        if (isHovered) {
            ctx.save();
            ctx.beginPath();
            ctx.arc(ap.x, ap.y, baseRadius + 10 + Math.sin(this.animTime * 3) * 2, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(212,175,55,0.15)';
            ctx.fill();
            ctx.restore();
        }

        if (val !== null && this.options.showHeatmap) {
            const gradient = ctx.createRadialGradient(ap.x, ap.y, 0, ap.x, ap.y, baseRadius + 4);
            gradient.addColorStop(0, fillColor);
            gradient.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = gradient;
            ctx.beginPath();
            ctx.arc(ap.x, ap.y, baseRadius + 4, 0, Math.PI * 2);
            ctx.fill();
        }

        ctx.beginPath();
        ctx.arc(ap.x, ap.y, baseRadius, 0, Math.PI * 2);
        ctx.fillStyle = fillColor;
        ctx.fill();
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = isHovered ? 2.5 : 1.2;
        ctx.stroke();

        ctx.beginPath();
        ctx.arc(ap.x, ap.y, baseRadius * 0.4, 0, Math.PI * 2);
        ctx.fillStyle = '#ffffff';
        ctx.fill();

        if (this.options.showLabels && (inSelected || isHovered)) {
            ctx.font = isHovered ? 'bold 12px Microsoft YaHei' : '10px Microsoft YaHei';
            ctx.fillStyle = isHovered ? '#f4d06f' : '#c9d1e4';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'middle';
            ctx.fillText(ap.name, ap.x + baseRadius + 5, ap.y - 6);
            ctx.font = '9px Consolas';
            ctx.fillStyle = '#8a93ac';
            ctx.fillText(ap.id, ap.x + baseRadius + 5, ap.y + 6);
        }
        ctx.globalAlpha = 1;
    }

    _drawLegend(ctx) {
        const [vmin, vmax] = this._getValueRange();
        const labels = { conductance: '皮肤电导 (μS)', temperature: '温度 (℃)', emg: '肌电幅值 (μV)' };
        const label = labels[this.options.dataType] || '';
        const x = 630, y = 640, w = 140, h = 14;
        const grad = ctx.createLinearGradient(x, y, x + w, y);
        grad.addColorStop(0, 'hsla(240,85%,55%,0.9)');
        grad.addColorStop(0.5, 'hsla(120,85%,55%,0.9)');
        grad.addColorStop(1, 'hsla(0,85%,55%,0.9)');
        ctx.fillStyle = grad;
        ctx.fillRect(x, y, w, h);
        ctx.strokeStyle = 'rgba(212,175,55,0.4)';
        ctx.lineWidth = 1;
        ctx.strokeRect(x, y, w, h);
        ctx.font = '10px Microsoft YaHei';
        ctx.fillStyle = '#8a93ac';
        ctx.textAlign = 'left';
        ctx.fillText(vmin, x, y + h + 12);
        ctx.textAlign = 'center';
        ctx.fillText(label, x + w / 2, y - 6);
        ctx.textAlign = 'right';
        ctx.fillText(vmax, x + w, y + h + 12);
    }

    render() {
        if (this._staticDirty) this._renderStatic();
        const ctx = this.ctx;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        ctx.drawImage(this._staticCanvas, 0, 0);
        for (const ap of this.acupoints) this._drawAcupoint(ctx, ap);
        this._drawLegend(ctx);
    }

    _onMouseMove(e) {
        const rect = this.canvas.getBoundingClientRect();
        const scaleX = this.canvas.width / rect.width;
        const scaleY = this.canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;

        let found = null;
        for (const ap of this.acupoints) {
            const dx = x - ap.x, dy = y - ap.y;
            if (dx * dx + dy * dy < 144) { found = ap; break; }
        }

        this.hoveredAcupoint = found ? found.id : null;
        if (found) {
            const d = this.sensorData[found.id];
            let html = `<div class="tooltip-title">${found.name} (${found.id})</div>`;
            html += `<div class="tooltip-row"><span class="tooltip-label">经络:</span><span class="tooltip-value">${found.meridian_id}</span></div>`;
            html += `<div class="tooltip-row"><span class="tooltip-label">拼音:</span><span class="tooltip-value">${found.pinyin}</span></div>`;
            if (d) {
                html += `<div class="tooltip-row"><span class="tooltip-label">电导:</span><span class="tooltip-value">${d.skin_conductance.toFixed(2)} μS</span></div>`;
                html += `<div class="tooltip-row"><span class="tooltip-label">温度:</span><span class="tooltip-value">${d.infrared_temperature.toFixed(2)} ℃</span></div>`;
                html += `<div class="tooltip-row"><span class="tooltip-label">肌电:</span><span class="tooltip-value">${d.emg_amplitude.toFixed(1)} μV</span></div>`;
            } else {
                html += `<div class="tooltip-row"><span class="tooltip-label">状态:</span><span class="tooltip-value">等待数据...</span></div>`;
            }
            html += `<div class="tooltip-row"><span class="tooltip-label">主治:</span><span class="tooltip-value">${found.description}</span></div>`;
            this.tooltip.innerHTML = html;
            this.tooltip.style.display = 'block';
            const tipX = e.clientX - rect.left + 15;
            const tipY = e.clientY - rect.top + 15;
            this.tooltip.style.left = Math.min(tipX, rect.width - 200) + 'px';
            this.tooltip.style.top = tipY + 'px';
        } else {
            this.tooltip.style.display = 'none';
        }
    }

    _onClick(e) {
        if (this.hoveredAcupoint && this.onAcupointClick) {
            this.onAcupointClick(this.hoveredAcupoint);
        }
    }
}
