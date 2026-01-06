// Calibration Page - Manual per-channel RC calibration

const CHANNEL_NAMES = ['Throttle', 'Steering', 'Aux1', 'Aux2', 'Aux3', 'Aux4'];

export class CalibrationPage {
    constructor() {
        this.elements = {};
        this.pollTimer = null;
        this.data = null;
        this._destroyed = false;
    }

    render() {
        return `
            <div class="calibration">
                <!-- Calibration Wizard Card -->
                <div class="card" id="cal-wizard-card">
                    <h2>Calibration Wizard</h2>
                    <div id="cal-wizard">
                        <div class="cal-idle" id="cal-idle">
                            <p>Select a channel below to calibrate it.</p>
                        </div>
                        <div class="cal-active" id="cal-active" style="display:none">
                            <div class="cal-channel-name" id="cal-channel-name">-</div>
                            <div class="cal-step-msg" id="cal-step-msg">-</div>
                            <div class="cal-pulse-display">
                                <span class="label">Current:</span>
                                <span class="cal-pulse" id="cal-pulse">1500</span>
                                <span class="unit">us</span>
                            </div>
                            <div class="cal-recorded" id="cal-recorded"></div>
                            <div class="cal-wizard-buttons">
                                <button id="cal-next-btn" class="btn btn-primary">Next</button>
                                <button id="cal-cancel-btn" class="btn btn-secondary">Cancel</button>
                            </div>
                        </div>
                    </div>
                </div>

                <!-- Channels Card -->
                <div class="card">
                    <h2>RC Channels</h2>
                    <div class="cal-channels-list">
                        ${CHANNEL_NAMES.map((name, i) => `
                            <div class="cal-channel-row" id="cal-row-${i}">
                                <div class="cal-ch-info">
                                    <span class="cal-ch-name">${name}</span>
                                    <span class="cal-ch-values" id="cal-values-${i}">1000 / 1500 / 2000</span>
                                </div>
                                <div class="cal-ch-live">
                                    <span class="cal-ch-raw" id="cal-raw-${i}">-</span>
                                </div>
                                <div class="cal-ch-actions">
                                    <label class="toggle small" title="Reverse">
                                        <input type="checkbox" id="cal-rev-${i}"/>
                                        <span class="toggle-slider"></span>
                                    </label>
                                    <button class="btn btn-sm" id="cal-btn-${i}">Calibrate</button>
                                </div>
                            </div>
                        `).join('')}
                    </div>
                </div>

                <!-- Actions Card -->
                <div class="card">
                    <div class="cal-actions">
                        <button id="cal-clear-all" class="btn btn-secondary">Reset All to Defaults</button>
                    </div>
                </div>
            </div>
        `;
    }

    init() {
        this.elements = {
            wizardCard: document.getElementById('cal-wizard-card'),
            idleView: document.getElementById('cal-idle'),
            activeView: document.getElementById('cal-active'),
            channelName: document.getElementById('cal-channel-name'),
            stepMsg: document.getElementById('cal-step-msg'),
            pulse: document.getElementById('cal-pulse'),
            recorded: document.getElementById('cal-recorded'),
            nextBtn: document.getElementById('cal-next-btn'),
            cancelBtn: document.getElementById('cal-cancel-btn'),
            clearAllBtn: document.getElementById('cal-clear-all'),
            channels: [],
            rawValues: [],
            revToggles: [],
            calButtons: []
        };

        // Collect per-channel elements
        for (let i = 0; i < 6; i++) {
            this.elements.channels[i] = document.getElementById(`cal-values-${i}`);
            this.elements.rawValues[i] = document.getElementById(`cal-raw-${i}`);
            this.elements.revToggles[i] = document.getElementById(`cal-rev-${i}`);
            this.elements.calButtons[i] = document.getElementById(`cal-btn-${i}`);

            // Calibrate button
            this.elements.calButtons[i].addEventListener('click', () => this.startChannel(i));

            // Reverse toggle
            this.elements.revToggles[i].addEventListener('change', () => {
                this.setReversed(i, this.elements.revToggles[i].checked);
            });
        }

        // Wizard buttons
        this.elements.nextBtn.addEventListener('click', () => this.nextStep());
        this.elements.cancelBtn.addEventListener('click', () => this.cancel());
        this.elements.clearAllBtn.addEventListener('click', () => this.clearAll());

        // Load initial state
        this.loadState();
    }

    loadState() {
        fetch('/api/calibration')
            .then(r => r.json())
            .then(data => this.updateUI(data))
            .catch(err => console.error('Failed to load calibration:', err));
    }

    updateUI(data) {
        this.data = data;

        // Update channel list
        if (data.channels) {
            for (let i = 0; i < 6 && i < data.channels.length; i++) {
                const ch = data.channels[i];
                this.elements.channels[i].textContent = `${ch.min} / ${ch.center} / ${ch.max}`;
                this.elements.revToggles[i].checked = ch.rev;
            }
        }

        // Update wizard view
        if (data.inProgress && data.channel >= 0) {
            this.elements.idleView.style.display = 'none';
            this.elements.activeView.style.display = 'block';
            this.elements.channelName.textContent = CHANNEL_NAMES[data.channel];
            this.elements.stepMsg.textContent = data.message;
            this.elements.pulse.textContent = data.pulse;

            // Show recorded values
            let recorded = '';
            if (data.step >= 2) recorded += `Center: ${data.recCenter}us `;
            if (data.step >= 3) recorded += `Min: ${data.recMin}us `;
            this.elements.recorded.textContent = recorded;

            // Update button text based on step
            if (data.step === 4) {
                this.elements.nextBtn.textContent = 'Done';
            } else {
                this.elements.nextBtn.textContent = 'Next';
            }

            // Disable calibrate buttons during calibration
            for (let i = 0; i < 6; i++) {
                this.elements.calButtons[i].disabled = true;
            }
        } else {
            this.elements.idleView.style.display = 'block';
            this.elements.activeView.style.display = 'none';

            // Enable calibrate buttons
            for (let i = 0; i < 6; i++) {
                this.elements.calButtons[i].disabled = false;
            }

            // Stop polling when idle
            this.stopPolling();
        }
    }

    startChannel(channel) {
        fetch('/api/calibration', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'start', channel: channel })
        })
        .then(r => r.json())
        .then(data => {
            this.updateUI(data);
            this.startPolling();
        })
        .catch(err => console.error('Failed to start calibration:', err));
    }

    nextStep() {
        fetch('/api/calibration', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'next' })
        })
        .then(r => r.json())
        .then(data => this.updateUI(data))
        .catch(err => console.error('Failed to advance step:', err));
    }

    cancel() {
        fetch('/api/calibration', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'cancel' })
        })
        .then(r => r.json())
        .then(data => {
            this.updateUI(data);
            this.stopPolling();
        })
        .catch(err => console.error('Failed to cancel:', err));
    }

    setReversed(channel, reversed) {
        fetch('/api/calibration', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'reverse', channel: channel, value: reversed })
        })
        .then(r => r.json())
        .then(data => this.updateUI(data))
        .catch(err => console.error('Failed to set reversed:', err));
    }

    clearAll() {
        if (!confirm('Reset all channels to default calibration?')) return;

        fetch('/api/calibration', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'clearAll' })
        })
        .then(r => r.json())
        .then(data => this.updateUI(data))
        .catch(err => console.error('Failed to clear:', err));
    }

    startPolling() {
        this.stopPolling();
        this.pollTimer = setInterval(() => {
            fetch('/api/calibration')
                .then(r => r.json())
                .then(data => {
                    if (!this._destroyed) this.updateUI(data);
                })
                .catch(err => console.error('Poll error:', err));
        }, 100);  // Poll at 10Hz for responsive pulse display
    }

    stopPolling() {
        if (this.pollTimer) {
            clearInterval(this.pollTimer);
            this.pollTimer = null;
        }
    }

    onData(data) {
        // Update live raw values from WebSocket data
        if (data.rc) {
            for (let i = 0; i < 6 && i < data.rc.length; i++) {
                this.elements.rawValues[i].textContent = data.rc[i] + ' us';
            }
        }
    }

    destroy() {
        this._destroyed = true;
        this.stopPolling();
    }
}
