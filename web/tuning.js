// Tuning Page - Servo endpoints, steering geometry, ESC settings

export class TuningPage {
    constructor() {
        this.elements = {};
        this.config = null;
        this.toastTimer = null;
    }

    render() {
        return `
            <div class="tuning">
                <!-- Toast notification -->
                <div class="toast" id="toast"></div>

                <!-- Axle Servo Cards -->
                <div class="card">
                    <h2>Axle Servos</h2>
                    <div class="servo-grid">
                        ${this.renderServoCard(0, 'Axle 1 (Front)')}
                        ${this.renderServoCard(1, 'Axle 2')}
                        ${this.renderServoCard(2, 'Axle 3')}
                        ${this.renderServoCard(3, 'Axle 4 (Rear)')}
                    </div>
                </div>

                <!-- Steering Geometry Card -->
                <div class="card">
                    <h2>Steering Geometry</h2>
                    <div class="tuning-group">
                        ${this.renderSliderRow('ratio0', 'Axle 1 Ratio', 0, 100, 100, '%')}
                        ${this.renderSliderRow('ratio1', 'Axle 2 Ratio', 0, 100, 70, '%')}
                        ${this.renderSliderRow('ratio2', 'Axle 3 Ratio', 0, 100, 70, '%')}
                        ${this.renderSliderRow('ratio3', 'Axle 4 Ratio', 0, 100, 100, '%')}
                        ${this.renderSliderRow('all-axle-rear', 'All-Axle Rear', 0, 100, 80, '%')}
                        ${this.renderSliderRow('expo', 'Steering Expo', 0, 100, 0, '%')}
                        ${this.renderSliderRow('speed-steering', 'Speed Steering', 0, 100, 0, '%')}
                        <div class="hint">Axle ratios control how much each axle steers relative to input. Inner axles typically steer less. Expo adds non-linear response for finer control around center. Speed Steering reduces steering sensitivity at higher throttle (0%=disabled, 100%=max reduction at full speed).</div>
                    </div>
                </div>

                <!-- ESC Settings Card -->
                <div class="card">
                    <h2>ESC Settings</h2>
                    <div class="tuning-group">
                        ${this.renderSliderRow('esc-fwd', 'Forward Limit', 0, 100, 100, '%')}
                        ${this.renderSliderRow('esc-rev', 'Reverse Limit', 0, 100, 100, '%')}
                        ${this.renderSliderRow('esc-subtrim', 'Subtrim', -100, 100, 0, '')}
                        ${this.renderSliderRow('esc-deadzone', 'Deadzone', 0, 100, 30, '')}
                        <div class="tuning-row">
                            <label>Reversed</label>
                            <label class="toggle">
                                <input type="checkbox" id="esc-reversed"/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                    </div>
                </div>

                <!-- Realistic Throttle Card -->
                <div class="card">
                    <h2>Realistic Throttle</h2>
                    <div class="tuning-group">
                        <div class="tuning-row">
                            <label>Enable Realistic Mode</label>
                            <label class="toggle">
                                <input type="checkbox" id="esc-realistic"/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                        <div class="hint">When enabled, the crawler will coast when you release throttle instead of braking instantly. This feels more like a real vehicle.</div>
                        ${this.renderSliderRow('esc-coast', 'Coast Rate', 0, 100, 50, '%')}
                        <div class="hint">How long the crawler coasts. 0% = quick stop, 100% = long coast.</div>
                        ${this.renderSliderRow('esc-brake', 'Brake Force', 0, 100, 50, '%')}
                        <div class="hint">How hard active braking stops you. 0% = weak, 100% = instant stop.</div>
                    </div>
                </div>

                <!-- Servo Test Mode Card -->
                <div class="card">
                    <h2>Servo Test</h2>
                    <div class="tuning-group">
                        <div class="tuning-row">
                            <label>Test Mode</label>
                            <label class="toggle">
                                <input type="checkbox" id="servo-test-active"/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                        <div class="hint" id="servo-test-hint">Enable to manually control servos. Auto-disables after 30 seconds of inactivity.</div>
                        <div id="servo-test-controls" style="display:none">
                            ${this.renderSliderRow('servo-test-0', 'Axle 1 (Front)', -1000, 1000, 0, '')}
                            ${this.renderSliderRow('servo-test-1', 'Axle 2', -1000, 1000, 0, '')}
                            ${this.renderSliderRow('servo-test-2', 'Axle 3', -1000, 1000, 0, '')}
                            ${this.renderSliderRow('servo-test-3', 'Axle 4 (Rear)', -1000, 1000, 0, '')}
                            <div class="tuning-actions">
                                <button id="servo-test-center" class="btn btn-secondary">Center All</button>
                            </div>
                        </div>
                    </div>
                </div>

                <!-- Action Buttons -->
                <div class="card">
                    <div class="tuning-actions">
                        <button id="tuning-reset" class="btn btn-secondary">Reset to Defaults</button>
                    </div>
                </div>
            </div>
        `;
    }

    renderSliderRow(id, label, min, max, value, suffix) {
        return `
            <div class="tuning-row">
                <label>${label}</label>
                <input type="range" id="${id}" min="${min}" max="${max}" value="${value}"/>
                <input type="number" id="${id}-num" min="${min}" max="${max}" value="${value}" class="tuning-num"/>
                <span class="tuning-suffix">${suffix}</span>
            </div>
        `;
    }

    renderServoCard(idx, name) {
        return `
            <div class="servo-card">
                <h3>${name}</h3>
                <div class="tuning-row">
                    <label>Min (µs)</label>
                    <input type="number" id="s${idx}-min" min="500" max="1500" value="1000"/>
                </div>
                <div class="tuning-row">
                    <label>Max (µs)</label>
                    <input type="number" id="s${idx}-max" min="1500" max="2500" value="2000"/>
                </div>
                <div class="tuning-row">
                    <label>Subtrim</label>
                    <input type="range" id="s${idx}-subtrim" min="-200" max="200" value="0"/>
                    <input type="number" id="s${idx}-subtrim-num" min="-200" max="200" value="0" class="tuning-num"/>
                    <span class="tuning-suffix">µs</span>
                </div>
                <div class="tuning-row">
                    <label>Trim</label>
                    <input type="range" id="s${idx}-trim" min="-200" max="200" value="0"/>
                    <input type="number" id="s${idx}-trim-num" min="-200" max="200" value="0" class="tuning-num"/>
                    <span class="tuning-suffix">µs</span>
                </div>
                <div class="tuning-row">
                    <label>Reversed</label>
                    <label class="toggle">
                        <input type="checkbox" id="s${idx}-rev"/>
                        <span class="toggle-slider"></span>
                    </label>
                </div>
            </div>
        `;
    }

    init() {
        this.elements = {
            toast: document.getElementById('toast'),
            servos: [],
            ratio: [],
            ratioNum: [],
            allAxleRear: document.getElementById('all-axle-rear'),
            allAxleRearNum: document.getElementById('all-axle-rear-num'),
            expo: document.getElementById('expo'),
            expoNum: document.getElementById('expo-num'),
            speedSteering: document.getElementById('speed-steering'),
            speedSteeringNum: document.getElementById('speed-steering-num'),
            escFwd: document.getElementById('esc-fwd'),
            escFwdNum: document.getElementById('esc-fwd-num'),
            escRev: document.getElementById('esc-rev'),
            escRevNum: document.getElementById('esc-rev-num'),
            escSubtrim: document.getElementById('esc-subtrim'),
            escSubtrimNum: document.getElementById('esc-subtrim-num'),
            escDeadzone: document.getElementById('esc-deadzone'),
            escDeadzoneNum: document.getElementById('esc-deadzone-num'),
            escReversed: document.getElementById('esc-reversed'),
            escRealistic: document.getElementById('esc-realistic'),
            escCoast: document.getElementById('esc-coast'),
            escCoastNum: document.getElementById('esc-coast-num'),
            escBrake: document.getElementById('esc-brake'),
            escBrakeNum: document.getElementById('esc-brake-num'),
            resetBtn: document.getElementById('tuning-reset'),
            // Servo test elements
            servoTestActive: document.getElementById('servo-test-active'),
            servoTestControls: document.getElementById('servo-test-controls'),
            servoTestHint: document.getElementById('servo-test-hint'),
            servoTestCenter: document.getElementById('servo-test-center'),
            servoTest: []
        };

        // Debounce timer for auto-save
        this.saveTimer = null;

        // Collect servo elements
        for (let i = 0; i < 4; i++) {
            this.elements.servos[i] = {
                min: document.getElementById(`s${i}-min`),
                max: document.getElementById(`s${i}-max`),
                subtrim: document.getElementById(`s${i}-subtrim`),
                subtrimNum: document.getElementById(`s${i}-subtrim-num`),
                trim: document.getElementById(`s${i}-trim`),
                trimNum: document.getElementById(`s${i}-trim-num`),
                rev: document.getElementById(`s${i}-rev`)
            };

            // Bidirectional sync for servo subtrim
            this.syncSliderAndInput(this.elements.servos[i].subtrim, this.elements.servos[i].subtrimNum);
            // Bidirectional sync for servo trim
            this.syncSliderAndInput(this.elements.servos[i].trim, this.elements.servos[i].trimNum);

            this.elements.servos[i].min.addEventListener('change', () => this.scheduleAutoSave());
            this.elements.servos[i].max.addEventListener('change', () => this.scheduleAutoSave());
            this.elements.servos[i].rev.addEventListener('change', () => this.scheduleAutoSave());
        }

        // Collect ratio elements
        for (let i = 0; i < 4; i++) {
            this.elements.ratio[i] = document.getElementById(`ratio${i}`);
            this.elements.ratioNum[i] = document.getElementById(`ratio${i}-num`);
            this.syncSliderAndInput(this.elements.ratio[i], this.elements.ratioNum[i]);
        }

        // Sync slider/input pairs for steering geometry
        this.syncSliderAndInput(this.elements.allAxleRear, this.elements.allAxleRearNum);
        this.syncSliderAndInput(this.elements.expo, this.elements.expoNum);
        this.syncSliderAndInput(this.elements.speedSteering, this.elements.speedSteeringNum);

        // Sync slider/input pairs for ESC
        this.syncSliderAndInput(this.elements.escFwd, this.elements.escFwdNum);
        this.syncSliderAndInput(this.elements.escRev, this.elements.escRevNum);
        this.syncSliderAndInput(this.elements.escSubtrim, this.elements.escSubtrimNum);
        this.syncSliderAndInput(this.elements.escDeadzone, this.elements.escDeadzoneNum);

        this.elements.escReversed.addEventListener('change', () => this.scheduleAutoSave());

        // Sync slider/input pairs for realistic throttle
        this.syncSliderAndInput(this.elements.escCoast, this.elements.escCoastNum);
        this.syncSliderAndInput(this.elements.escBrake, this.elements.escBrakeNum);

        this.elements.escRealistic.addEventListener('change', () => this.scheduleAutoSave());

        // Servo test mode - collect elements and setup
        for (let i = 0; i < 4; i++) {
            this.elements.servoTest[i] = {
                slider: document.getElementById(`servo-test-${i}`),
                num: document.getElementById(`servo-test-${i}-num`)
            };
            // Sync slider and number (don't auto-save, send immediately)
            this.elements.servoTest[i].slider.addEventListener('input', () => {
                this.elements.servoTest[i].num.value = this.elements.servoTest[i].slider.value;
                this.sendServoTest();
            });
            this.elements.servoTest[i].num.addEventListener('input', () => {
                this.elements.servoTest[i].slider.value = this.elements.servoTest[i].num.value;
                this.sendServoTest();
            });
        }

        // Servo test mode toggle
        this.elements.servoTestActive.addEventListener('change', () => this.toggleServoTest());
        this.elements.servoTestCenter.addEventListener('click', () => this.centerAllServos());

        // Button handlers
        this.elements.resetBtn.addEventListener('click', () => this.resetConfig());

        // Load initial config
        this.loadConfig();
        this.loadServoTestState();
    }

    // Sync slider and number input bidirectionally
    syncSliderAndInput(slider, numInput) {
        slider.addEventListener('input', () => {
            numInput.value = slider.value;
            this.scheduleAutoSave();
        });
        numInput.addEventListener('input', () => {
            slider.value = numInput.value;
            this.scheduleAutoSave();
        });
    }

    loadConfig() {
        fetch('/api/tuning')
            .then(r => r.json())
            .then(data => {
                this.config = data;
                this.applyConfig(data);
            })
            .catch(err => {
                console.error('Failed to load tuning config:', err);
                this.showToast('Failed to load configuration', 'error');
            });
    }

    applyConfig(data) {
        // Apply servo settings from nested structure: data.servos[i]
        if (data.servos) {
            for (let i = 0; i < 4; i++) {
                const s = this.elements.servos[i];
                const srv = data.servos[i];
                if (srv) {
                    if (srv.min !== undefined) s.min.value = srv.min;
                    if (srv.max !== undefined) s.max.value = srv.max;
                    if (srv.subtrim !== undefined) {
                        s.subtrim.value = srv.subtrim;
                        s.subtrimNum.value = srv.subtrim;
                    }
                    if (srv.trim !== undefined) {
                        s.trim.value = srv.trim;
                        s.trimNum.value = srv.trim;
                    }
                    if (srv.rev !== undefined) s.rev.checked = srv.rev;
                }
            }
        }

        // Apply steering settings from data.steering
        if (data.steering) {
            if (data.steering.ratio) {
                for (let i = 0; i < 4; i++) {
                    if (data.steering.ratio[i] !== undefined) {
                        this.elements.ratio[i].value = data.steering.ratio[i];
                        this.elements.ratioNum[i].value = data.steering.ratio[i];
                    }
                }
            }
            if (data.steering.allAxleRear !== undefined) {
                this.elements.allAxleRear.value = data.steering.allAxleRear;
                this.elements.allAxleRearNum.value = data.steering.allAxleRear;
            }
            if (data.steering.expo !== undefined) {
                this.elements.expo.value = data.steering.expo;
                this.elements.expoNum.value = data.steering.expo;
            }
            if (data.steering.speedSteering !== undefined) {
                this.elements.speedSteering.value = data.steering.speedSteering;
                this.elements.speedSteeringNum.value = data.steering.speedSteering;
            }
        }

        // Apply ESC settings from data.esc
        if (data.esc) {
            if (data.esc.fwdLimit !== undefined) {
                this.elements.escFwd.value = data.esc.fwdLimit;
                this.elements.escFwdNum.value = data.esc.fwdLimit;
            }
            if (data.esc.revLimit !== undefined) {
                this.elements.escRev.value = data.esc.revLimit;
                this.elements.escRevNum.value = data.esc.revLimit;
            }
            if (data.esc.subtrim !== undefined) {
                this.elements.escSubtrim.value = data.esc.subtrim;
                this.elements.escSubtrimNum.value = data.esc.subtrim;
            }
            if (data.esc.deadzone !== undefined) {
                this.elements.escDeadzone.value = data.esc.deadzone;
                this.elements.escDeadzoneNum.value = data.esc.deadzone;
            }
            if (data.esc.rev !== undefined) {
                this.elements.escReversed.checked = data.esc.rev;
            }
            if (data.esc.realistic !== undefined) {
                this.elements.escRealistic.checked = data.esc.realistic;
            }
            if (data.esc.coastRate !== undefined) {
                this.elements.escCoast.value = data.esc.coastRate;
                this.elements.escCoastNum.value = data.esc.coastRate;
            }
            if (data.esc.brakeForce !== undefined) {
                this.elements.escBrake.value = data.esc.brakeForce;
                this.elements.escBrakeNum.value = data.esc.brakeForce;
            }
        }
    }

    scheduleAutoSave() {
        if (this.saveTimer) {
            clearTimeout(this.saveTimer);
        }
        this.saveTimer = setTimeout(() => this.saveConfig(), 500);
    }

    saveConfig() {
        const config = {};

        // Gather servo settings
        for (let i = 0; i < 4; i++) {
            const s = this.elements.servos[i];
            config[`s${i}_min`] = parseInt(s.min.value);
            config[`s${i}_max`] = parseInt(s.max.value);
            config[`s${i}_subtrim`] = parseInt(s.subtrim.value);
            config[`s${i}_trim`] = parseInt(s.trim.value);
            config[`s${i}_rev`] = s.rev.checked;
        }

        // Gather steering settings
        for (let i = 0; i < 4; i++) {
            config[`ratio${i}`] = parseInt(this.elements.ratio[i].value);
        }
        config.allAxleRear = parseInt(this.elements.allAxleRear.value);
        config.expo = parseInt(this.elements.expo.value);
        config.speedSteering = parseInt(this.elements.speedSteering.value);

        // Gather ESC settings
        config.fwdLimit = parseInt(this.elements.escFwd.value);
        config.revLimit = parseInt(this.elements.escRev.value);
        config.escSubtrim = parseInt(this.elements.escSubtrim.value);
        config.deadzone = parseInt(this.elements.escDeadzone.value);
        config.escRev = this.elements.escReversed.checked;

        // Gather realistic throttle settings
        config.realistic = this.elements.escRealistic.checked;
        config.coastRate = parseInt(this.elements.escCoast.value);
        config.brakeForce = parseInt(this.elements.escBrake.value);

        fetch('/api/tuning', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        })
        .then(r => r.json())
        .then(() => {
            this.showToast('Saved', 'success');
        })
        .catch(err => {
            console.error('Failed to save tuning config:', err);
            this.showToast('Failed to save', 'error');
        });
    }

    resetConfig() {
        if (!confirm('Reset all tuning settings to factory defaults?\n\nThis cannot be undone.')) {
            return;
        }

        this.elements.resetBtn.disabled = true;
        this.elements.resetBtn.textContent = 'Resetting...';

        fetch('/api/tuning/reset', { method: 'POST' })
            .then(r => r.json())
            .then(() => {
                this.showToast('Reset to defaults', 'success');
                this.elements.resetBtn.textContent = 'Reset!';
                setTimeout(() => {
                    this.elements.resetBtn.textContent = 'Reset to Defaults';
                    this.elements.resetBtn.disabled = false;
                }, 1500);
                this.loadConfig();
            })
            .catch(err => {
                console.error('Failed to reset tuning config:', err);
                this.showToast('Failed to reset', 'error');
                this.elements.resetBtn.textContent = 'Error!';
                setTimeout(() => {
                    this.elements.resetBtn.textContent = 'Reset to Defaults';
                    this.elements.resetBtn.disabled = false;
                }, 2000);
            });
    }

    showToast(message, type) {
        const toast = this.elements.toast;
        toast.textContent = message;
        toast.className = 'toast show ' + (type || '');

        if (this.toastTimer) {
            clearTimeout(this.toastTimer);
        }
        this.toastTimer = setTimeout(() => {
            toast.className = 'toast';
        }, 2000);
    }

    onData(data) {
        // Could update live preview here if needed
    }

    // =========================================================================
    // Servo Test Mode
    // =========================================================================

    loadServoTestState() {
        fetch('/api/servo')
            .then(r => r.json())
            .then(data => {
                this.elements.servoTestActive.checked = data.active;
                this.updateServoTestUI(data.active);
                if (data.values) {
                    for (let i = 0; i < 4; i++) {
                        this.elements.servoTest[i].slider.value = data.values[i] || 0;
                        this.elements.servoTest[i].num.value = data.values[i] || 0;
                    }
                }
            })
            .catch(err => console.error('Failed to load servo test state:', err));
    }

    toggleServoTest() {
        const active = this.elements.servoTestActive.checked;
        this.updateServoTestUI(active);

        // Send enable/disable command
        fetch('/api/servo', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ active: active })
        })
        .then(r => r.json())
        .catch(err => {
            console.error('Failed to toggle servo test:', err);
            // Revert UI on error
            this.elements.servoTestActive.checked = !active;
            this.updateServoTestUI(!active);
        });
    }

    updateServoTestUI(active) {
        this.elements.servoTestControls.style.display = active ? 'block' : 'none';
        this.elements.servoTestHint.textContent = active
            ? 'Test mode active. RC input is ignored. Auto-disables after 30s of inactivity.'
            : 'Enable to manually control servos. Auto-disables after 30 seconds of inactivity.';
    }

    sendServoTest() {
        const values = [];
        for (let i = 0; i < 4; i++) {
            values.push(parseInt(this.elements.servoTest[i].slider.value));
        }

        fetch('/api/servo', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ active: true, values: values })
        })
        .catch(err => console.error('Failed to send servo test:', err));
    }

    centerAllServos() {
        for (let i = 0; i < 4; i++) {
            this.elements.servoTest[i].slider.value = 0;
            this.elements.servoTest[i].num.value = 0;
        }
        this.sendServoTest();
    }

    destroy() {
        if (this.saveTimer) clearTimeout(this.saveTimer);
        if (this.toastTimer) clearTimeout(this.toastTimer);
    }
}
