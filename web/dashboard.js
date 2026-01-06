// Dashboard Page - Real-time vehicle status and controls
import { sendMessage } from './app.js';

const MODE_DESCRIPTIONS = [
    'Axles 1-2 steer, 3-4 fixed',
    'Axles 3-4 steer, 1-2 fixed',
    'All axles steer in coordination',
    'All axles steer same direction'
];

export class DashboardPage {
    constructor() {
        this.elements = {};
    }

    render() {
        return `
            <div class="dashboard">
                <!-- Row 1: Steering Mode + Vehicle -->
                <div class="dash-row">
                    <div class="card">
                        <h2>STEERING MODE</h2>
                        <div class="aux-status">
                            <span class="aux-label">AUX1:</span>
                            <span id="aux1-st" class="aux-val aux-off">OFF</span>
                            <span class="aux-label">AUX2:</span>
                            <span id="aux2-st" class="aux-val aux-off">OFF</span>
                            <span class="aux-label">AUX3:</span>
                            <span id="aux3-st" class="aux-val aux-off">OFF</span>
                            <span class="aux-label">AUX4:</span>
                            <span id="aux4-st" class="aux-val aux-off">OFF</span>
                        </div>
                        <div class="modes" id="modes">
                            <button class="mode-btn" data-m="0">Front</button>
                            <button class="mode-btn" data-m="2">All Axle</button>
                            <button class="mode-btn" data-m="3">Crab</button>
                            <button class="mode-btn" data-m="1">Rear</button>
                            <button class="mode-btn aux-btn" id="aux-mode">AUX</button>
                        </div>
                        <div class="mode-desc" id="mode-desc">Axles 1-2 steer, 3-4 fixed</div>

                        <h3 class="section-title">System</h3>
                        <div class="stats-row">
                            <div class="stat-item">
                                <span class="stat-label">Heap</span>
                                <span class="stat-value" id="stat-heap">-</span>
                            </div>
                            <div class="stat-item">
                                <span class="stat-label">Min</span>
                                <span class="stat-value" id="stat-heap-min">-</span>
                            </div>
                            <div class="stat-item">
                                <span class="stat-label">Uptime</span>
                                <span class="stat-value" id="stat-uptime">-</span>
                            </div>
                            <div class="stat-item">
                                <span class="stat-label">RSSI</span>
                                <span class="stat-value" id="stat-rssi">-</span>
                            </div>
                        </div>
                    </div>

                    <div class="card">
                        <h2>VEHICLE STATUS</h2>
                        <div class="vehicle-container">
                            <svg viewBox="0 0 240 320" class="vehicle-svg">
                                <rect x="70" y="20" width="100" height="280" rx="10" class="vehicle-body"/>
                                <polygon points="120,35 110,50 130,50" class="direction-arrow"/>

                                <g class="axle" id="axle1-g">
                                    <rect x="50" y="38" width="12" height="28" rx="2" class="wheel" id="wheel1l"/>
                                    <rect x="178" y="38" width="12" height="28" rx="2" class="wheel" id="wheel1r"/>
                                    <line x1="70" y1="52" x2="62" y2="52" class="axle-line"/>
                                    <line x1="170" y1="52" x2="178" y2="52" class="axle-line"/>
                                    <text x="120" y="57" class="axle-label">A1</text>
                                    <text x="28" y="57" class="axle-value" id="a1-val">1500</text>
                                </g>

                                <g class="axle" id="axle2-g">
                                    <rect x="50" y="103" width="12" height="28" rx="2" class="wheel" id="wheel2l"/>
                                    <rect x="178" y="103" width="12" height="28" rx="2" class="wheel" id="wheel2r"/>
                                    <line x1="70" y1="117" x2="62" y2="117" class="axle-line"/>
                                    <line x1="170" y1="117" x2="178" y2="117" class="axle-line"/>
                                    <text x="120" y="122" class="axle-label">A2</text>
                                    <text x="28" y="122" class="axle-value" id="a2-val">1500</text>
                                </g>

                                <g class="axle" id="axle3-g">
                                    <rect x="50" y="183" width="12" height="28" rx="2" class="wheel" id="wheel3l"/>
                                    <rect x="178" y="183" width="12" height="28" rx="2" class="wheel" id="wheel3r"/>
                                    <line x1="70" y1="197" x2="62" y2="197" class="axle-line"/>
                                    <line x1="170" y1="197" x2="178" y2="197" class="axle-line"/>
                                    <text x="120" y="202" class="axle-label">A3</text>
                                    <text x="28" y="202" class="axle-value" id="a3-val">1500</text>
                                </g>

                                <g class="axle" id="axle4-g">
                                    <rect x="50" y="248" width="12" height="28" rx="2" class="wheel" id="wheel4l"/>
                                    <rect x="178" y="248" width="12" height="28" rx="2" class="wheel" id="wheel4r"/>
                                    <line x1="70" y1="262" x2="62" y2="262" class="axle-line"/>
                                    <line x1="170" y1="262" x2="178" y2="262" class="axle-line"/>
                                    <text x="120" y="267" class="axle-label">A4</text>
                                    <text x="28" y="267" class="axle-value" id="a4-val">1500</text>
                                </g>

                                <rect x="90" y="140" width="60" height="40" rx="5" class="esc-box"/>
                                <text x="120" y="155" class="esc-label">ESC</text>
                                <text x="120" y="172" class="esc-value" id="esc-val">1500</text>
                            </svg>
                        </div>
                    </div>
                </div>

                <!-- Row 2: RC Input + Servo Output -->
                <div class="dash-row">
                    <div class="card">
                        <h2>RC INPUT</h2>
                        <div class="io-grid">
                            <div class="io-row">
                                <span class="io-label">THR</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-thr-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-thr">0</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">STR</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-str-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-str">0</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">AUX1</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-aux1-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-aux1">0</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">AUX2</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-aux2-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-aux2">0</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">AUX3</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-aux3-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-aux3">0</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">AUX4</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="rc-aux4-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="rc-aux4">0</span>
                            </div>
                        </div>
                        <div class="rc-raw">
                            Raw: <span id="rc-ch1">-</span> <span id="rc-ch2">-</span> <span id="rc-ch3">-</span> <span id="rc-ch4">-</span> <span id="rc-ch5">-</span> <span id="rc-ch6">-</span> µs
                        </div>
                    </div>

                    <div class="card">
                        <h2>SERVO OUTPUT</h2>
                        <div class="io-grid">
                            <div class="io-row">
                                <span class="io-label">ESC</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="out-esc-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="out-esc">1500</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">A1</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="out-a1-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="out-a1">1500</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">A2</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="out-a2-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="out-a2">1500</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">A3</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="out-a3-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="out-a3">1500</span>
                            </div>
                            <div class="io-row">
                                <span class="io-label">A4</span>
                                <div class="io-bar-wrap">
                                    <div class="io-bar" id="out-a4-bar"></div>
                                    <div class="io-center"></div>
                                </div>
                                <span class="io-value" id="out-a4">1500</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;
    }

    init() {
        this.elements = {
            aux1: document.getElementById('aux1-st'),
            aux2: document.getElementById('aux2-st'),
            aux3: document.getElementById('aux3-st'),
            aux4: document.getElementById('aux4-st'),
            modeDesc: document.getElementById('mode-desc'),
            esc: document.getElementById('esc-val'),
            axles: [
                document.getElementById('a1-val'),
                document.getElementById('a2-val'),
                document.getElementById('a3-val'),
                document.getElementById('a4-val')
            ],
            wheels: [
                [document.getElementById('wheel1l'), document.getElementById('wheel1r')],
                [document.getElementById('wheel2l'), document.getElementById('wheel2r')],
                [document.getElementById('wheel3l'), document.getElementById('wheel3r')],
                [document.getElementById('wheel4l'), document.getElementById('wheel4r')]
            ],
            heap: document.getElementById('stat-heap'),
            heapMin: document.getElementById('stat-heap-min'),
            uptime: document.getElementById('stat-uptime'),
            rssi: document.getElementById('stat-rssi'),
            // RC inputs
            rcThr: document.getElementById('rc-thr'),
            rcThrBar: document.getElementById('rc-thr-bar'),
            rcStr: document.getElementById('rc-str'),
            rcStrBar: document.getElementById('rc-str-bar'),
            rcAux1: document.getElementById('rc-aux1'),
            rcAux1Bar: document.getElementById('rc-aux1-bar'),
            rcAux2: document.getElementById('rc-aux2'),
            rcAux2Bar: document.getElementById('rc-aux2-bar'),
            rcAux3: document.getElementById('rc-aux3'),
            rcAux3Bar: document.getElementById('rc-aux3-bar'),
            rcAux4: document.getElementById('rc-aux4'),
            rcAux4Bar: document.getElementById('rc-aux4-bar'),
            ch1: document.getElementById('rc-ch1'),
            ch2: document.getElementById('rc-ch2'),
            ch3: document.getElementById('rc-ch3'),
            ch4: document.getElementById('rc-ch4'),
            ch5: document.getElementById('rc-ch5'),
            ch6: document.getElementById('rc-ch6'),
            // Servo outputs
            outEsc: document.getElementById('out-esc'),
            outEscBar: document.getElementById('out-esc-bar'),
            outAxles: [
                { val: document.getElementById('out-a1'), bar: document.getElementById('out-a1-bar') },
                { val: document.getElementById('out-a2'), bar: document.getElementById('out-a2-bar') },
                { val: document.getElementById('out-a3'), bar: document.getElementById('out-a3-bar') },
                { val: document.getElementById('out-a4'), bar: document.getElementById('out-a4-bar') }
            ]
        };

        const modesContainer = document.getElementById('modes');
        modesContainer.addEventListener('click', (e) => {
            const btn = e.target.closest('.mode-btn');
            if (!btn) return;

            if (btn.id === 'aux-mode') {
                sendMessage({ cmd: 'aux' });
            } else {
                const mode = parseInt(btn.dataset.m);
                sendMessage({ cmd: 'mode', v: mode });
            }
        });
    }

    onData(data) {
        const el = this.elements;
        if (!el.aux1) return;

        // AUX channels
        if (data.x1 !== undefined) {
            const aux1On = data.x1 > 200;
            el.aux1.textContent = aux1On ? 'ON' : 'OFF';
            el.aux1.className = 'aux-val ' + (aux1On ? 'aux-on' : 'aux-off');
            if (el.rcAux1) {
                el.rcAux1.textContent = data.x1;
                this.updateRcBar(el.rcAux1Bar, data.x1);
            }
        }
        if (data.x2 !== undefined) {
            const aux2On = data.x2 > 200;
            el.aux2.textContent = aux2On ? 'ON' : 'OFF';
            el.aux2.className = 'aux-val ' + (aux2On ? 'aux-on' : 'aux-off');
            if (el.rcAux2) {
                el.rcAux2.textContent = data.x2;
                this.updateRcBar(el.rcAux2Bar, data.x2);
            }
        }
        if (data.x3 !== undefined) {
            const aux3On = data.x3 > 200;
            el.aux3.textContent = aux3On ? 'ON' : 'OFF';
            el.aux3.className = 'aux-val ' + (aux3On ? 'aux-on' : 'aux-off');
            if (el.rcAux3) {
                el.rcAux3.textContent = data.x3;
                this.updateRcBar(el.rcAux3Bar, data.x3);
            }
        }
        if (data.x4 !== undefined) {
            const aux4On = data.x4 > 200;
            el.aux4.textContent = aux4On ? 'ON' : 'OFF';
            el.aux4.className = 'aux-val ' + (aux4On ? 'aux-on' : 'aux-off');
            if (el.rcAux4) {
                el.rcAux4.textContent = data.x4;
                this.updateRcBar(el.rcAux4Bar, data.x4);
            }
        }

        // Steering mode
        if (data.m !== undefined) {
            this.updateModeButtons(data.m);
            el.modeDesc.textContent = MODE_DESCRIPTIONS[data.m] || '';
        }

        // ESC value
        if (data.e !== undefined) {
            el.esc.textContent = data.e;
            if (el.outEsc) {
                el.outEsc.textContent = data.e;
                this.updateServoBar(el.outEscBar, data.e);
            }
        }

        // Axle servo values
        const servos = [data.a1, data.a2, data.a3, data.a4];
        for (let i = 0; i < 4; i++) {
            if (servos[i] !== undefined) {
                if (el.axles[i]) el.axles[i].textContent = servos[i];
                this.updateWheelStyle(i, servos[i]);
                if (el.outAxles[i]) {
                    el.outAxles[i].val.textContent = servos[i];
                    this.updateServoBar(el.outAxles[i].bar, servos[i]);
                }
            }
        }

        // RC calibrated values
        if (data.t !== undefined && el.rcThr) {
            el.rcThr.textContent = data.t;
            this.updateRcBar(el.rcThrBar, data.t);
        }
        if (data.s !== undefined && el.rcStr) {
            el.rcStr.textContent = data.s;
            this.updateRcBar(el.rcStrBar, data.s);
        }

        // Raw RC - validate array structure before accessing
        if (data.rc && Array.isArray(data.rc) && data.rc.length >= 6 && el.ch1) {
            el.ch1.textContent = data.rc[0] ?? '-';
            el.ch2.textContent = data.rc[1] ?? '-';
            el.ch3.textContent = data.rc[2] ?? '-';
            el.ch4.textContent = data.rc[3] ?? '-';
            el.ch5.textContent = data.rc[4] ?? '-';
            el.ch6.textContent = data.rc[5] ?? '-';
        }

        // System stats
        if (data.h !== undefined && el.heap) el.heap.textContent = this.formatBytes(data.h);
        if (data.hm !== undefined && el.heapMin) el.heapMin.textContent = this.formatBytes(data.hm);
        if (data.u !== undefined && el.uptime) el.uptime.textContent = this.formatUptime(data.u);
        if (data.rs !== undefined && el.rssi) el.rssi.textContent = data.rs + ' dBm';
    }

    updateModeButtons(activeMode) {
        const buttons = document.querySelectorAll('.mode-btn[data-m]');
        buttons.forEach(btn => {
            const m = parseInt(btn.dataset.m);
            btn.classList.toggle('active', m === activeMode);
        });
    }

    updateWheelStyle(axleIndex, value) {
        const wheels = this.elements.wheels[axleIndex];
        if (!wheels) return;

        const deviation = value - 1500;
        const maxDeviation = 500;
        const maxAngle = 30;

        let angle = (deviation / maxDeviation) * maxAngle;
        angle = Math.max(-maxAngle, Math.min(maxAngle, angle));

        wheels.forEach(wheel => {
            wheel.style.transform = `rotate(${angle}deg)`;
            if (angle < -2) {
                wheel.style.fill = 'var(--accent-blue)';
            } else if (angle > 2) {
                wheel.style.fill = 'var(--accent-magenta)';
            } else {
                wheel.style.fill = '#444';
            }
        });
    }

    updateServoBar(barEl, value) {
        if (!barEl) return;
        // Map 1000-2000 to 0-100%
        const pct = Math.max(0, Math.min(100, ((value - 1000) / 1000) * 100));
        barEl.style.width = pct + '%';

        const deviation = Math.abs(value - 1500);
        if (deviation < 20) {
            barEl.style.background = 'var(--accent-green)';
        } else if (deviation < 200) {
            barEl.style.background = 'var(--accent-blue)';
        } else {
            barEl.style.background = 'var(--accent-orange)';
        }
    }

    updateRcBar(barEl, value) {
        if (!barEl) return;
        // Map -1000 to 1000 -> 0% to 100%
        const pct = Math.max(0, Math.min(100, 50 + (value / 20)));
        barEl.style.width = pct + '%';

        const deviation = Math.abs(value);
        if (deviation < 50) {
            barEl.style.background = 'var(--accent-green)';
        } else if (deviation < 500) {
            barEl.style.background = 'var(--accent-blue)';
        } else {
            barEl.style.background = 'var(--accent-orange)';
        }
    }

    formatBytes(bytes) {
        if (bytes < 1024) return bytes + ' B';
        return (bytes / 1024).toFixed(1) + ' KB';
    }

    formatUptime(ms) {
        const secs = Math.floor(ms / 1000);
        const mins = Math.floor(secs / 60);
        const hours = Math.floor(mins / 60);
        const days = Math.floor(hours / 24);

        if (days > 0) return days + 'd ' + (hours % 24) + 'h';
        if (hours > 0) return hours + 'h ' + (mins % 60) + 'm';
        if (mins > 0) return mins + 'm ' + (secs % 60) + 's';
        return secs + 's';
    }

    destroy() {}
}
