// Sound Settings Page - Engine sound configuration

export class SoundPage {
    constructor() {
        this.elements = {};
        this.profiles = [];
        this.config = null;
    }

    render() {
        return `
            <div class="sound-settings">
                <!-- Sound Profile Card -->
                <div class="card">
                    <h2>Engine Sound Profile</h2>
                    <div class="profile-container">
                        <div class="row">
                            <span class="label">Sound Profile</span>
                            <select id="sound-profile" class="select">
                                <option value="0">Loading...</option>
                            </select>
                        </div>
                        <div class="profile-info" id="profile-info">
                            <span class="description" id="profile-desc">-</span>
                        </div>
                        <div class="row">
                            <span class="label">Engine Sound</span>
                            <label class="toggle">
                                <input type="checkbox" id="sound-enabled" checked/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                        <div class="row">
                            <span class="label">Current RPM</span>
                            <span class="value" id="current-rpm">-</span>
                        </div>
                    </div>
                </div>

                <!-- Volume Settings Card -->
                <div class="card">
                    <h2>Volume Settings</h2>
                    <div class="volume-container">
                        <div class="volume-presets-section">
                            <span class="label">Menu Volume Presets</span>
                            <div class="presets-grid">
                                <div class="preset-item" id="preset-low-item">
                                    <button id="vol-low-btn" class="preset-apply-btn">Low</button>
                                    <input type="number" id="vol-low" min="0" max="200" value="20" class="preset-input"/>
                                    <span class="preset-unit">%</span>
                                </div>
                                <div class="preset-item" id="preset-medium-item">
                                    <button id="vol-medium-btn" class="preset-apply-btn">Medium</button>
                                    <input type="number" id="vol-medium" min="0" max="200" value="100" class="preset-input"/>
                                    <span class="preset-unit">%</span>
                                </div>
                                <div class="preset-item" id="preset-high-item">
                                    <button id="vol-high-btn" class="preset-apply-btn">High</button>
                                    <input type="number" id="vol-high" min="0" max="200" value="170" class="preset-input"/>
                                    <span class="preset-unit">%</span>
                                </div>
                            </div>
                            <div class="hint">Click preset name to apply. Edit values to customize.</div>
                        </div>
                        <div class="slider-row">
                            <span class="label">Idle Volume</span>
                            <input type="range" id="idle-volume" min="0" max="200" value="100"/>
                            <span class="value" id="idle-volume-val">100%</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Rev Volume</span>
                            <input type="range" id="rev-volume" min="0" max="200" value="100"/>
                            <span class="value" id="rev-volume-val">100%</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Knock Volume</span>
                            <input type="range" id="knock-volume" min="0" max="200" value="80"/>
                            <span class="value" id="knock-volume-val">80%</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Start Volume</span>
                            <input type="range" id="start-volume" min="0" max="200" value="90"/>
                            <span class="value" id="start-volume-val">90%</span>
                        </div>
                    </div>
                </div>

                <!-- Engine Characteristics Card -->
                <div class="card">
                    <h2>Engine Characteristics</h2>
                    <div class="engine-container">
                        <div class="slider-row">
                            <span class="label">Max RPM %</span>
                            <input type="range" id="max-rpm" min="150" max="500" value="300"/>
                            <span class="value" id="max-rpm-val">300%</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Acceleration</span>
                            <input type="range" id="acceleration" min="1" max="9" value="2"/>
                            <span class="value" id="acceleration-val">2</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Deceleration</span>
                            <input type="range" id="deceleration" min="1" max="5" value="1"/>
                            <span class="value" id="deceleration-val">1</span>
                        </div>
                        <div class="row">
                            <span class="label">Jake Brake</span>
                            <label class="toggle">
                                <input type="checkbox" id="jake-brake" checked/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                        <div class="row">
                            <span class="label">V8 Mode</span>
                            <label class="toggle">
                                <input type="checkbox" id="v8-mode" checked/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                    </div>
                </div>

                <!-- Sound Effects Card -->
                <div class="card">
                    <h2>Sound Effects</h2>
                    <div class="effects-container">
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="air-brake-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Air Brake</span>
                            <input type="range" id="air-brake-volume" min="0" max="100" value="70" class="effect-slider"/>
                            <span class="value" id="air-brake-volume-val">70%</span>
                        </div>
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="reverse-beep-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Reverse Beep</span>
                            <input type="range" id="reverse-beep-volume" min="0" max="100" value="70" class="effect-slider"/>
                            <span class="value" id="reverse-beep-volume-val">70%</span>
                        </div>
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="gear-shift-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Gear Shift</span>
                            <input type="range" id="gear-shift-volume" min="0" max="100" value="70" class="effect-slider"/>
                            <span class="value" id="gear-shift-volume-val">70%</span>
                        </div>
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="wastegate-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Wastegate</span>
                            <input type="range" id="wastegate-volume" min="0" max="100" value="70" class="effect-slider"/>
                            <span class="value" id="wastegate-volume-val">70%</span>
                        </div>
                        <div class="hint">Effects trigger automatically based on driving conditions</div>
                    </div>
                </div>

                <!-- Horn & Mode Switch Card -->
                <div class="card">
                    <h2>Horn & Mode Switch</h2>
                    <div class="effects-container">
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="horn-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Horn</span>
                            <input type="range" id="horn-volume" min="0" max="100" value="80" class="effect-slider"/>
                            <span class="value" id="horn-volume-val">80%</span>
                        </div>
                        <div class="row" style="margin-top: 8px; margin-bottom: 8px;">
                            <span class="label">Horn Type</span>
                            <select id="horn-type" class="select">
                                <option value="0">Truck Horn</option>
                                <option value="1">MAN TGE Horn</option>
                                <option value="2">La Cucaracha</option>
                                <option value="3">Two-Tone</option>
                                <option value="4">Dixie</option>
                                <option value="5">Peterbilt</option>
                                <option value="6">Outlaw</option>
                            </select>
                        </div>
                        <div class="effect-row">
                            <label class="toggle-inline">
                                <input type="checkbox" id="mode-switch-enabled" checked/>
                                <span class="toggle-slider-small"></span>
                            </label>
                            <span class="label">Mode Switch Sound</span>
                            <input type="range" id="mode-switch-volume" min="0" max="100" value="80" class="effect-slider"/>
                            <span class="value" id="mode-switch-volume-val">80%</span>
                        </div>
                        <div class="hint">Horn: CH3 button. Mode switch: CH4 momentary.</div>
                    </div>
                </div>

                <!-- Advanced Settings Card -->
                <div class="card collapsible">
                    <h2 class="collapsible-header" id="advanced-toggle">Advanced Settings <span class="collapse-icon">+</span></h2>
                    <div class="advanced-container collapsed" id="advanced-content">
                        <div class="slider-row">
                            <span class="label">Rev Switch Point</span>
                            <input type="range" id="rev-switch" min="50" max="200" value="80"/>
                            <span class="value" id="rev-switch-val">80</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Idle End Point</span>
                            <input type="range" id="idle-end" min="100" max="500" value="300"/>
                            <span class="value" id="idle-end-val">300</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Knock Start Point</span>
                            <input type="range" id="knock-start" min="50" max="300" value="150"/>
                            <span class="value" id="knock-start-val">150</span>
                        </div>
                        <div class="slider-row">
                            <span class="label">Knock Interval</span>
                            <input type="range" id="knock-interval" min="4" max="12" value="8"/>
                            <span class="value" id="knock-interval-val">8</span>
                        </div>
                        <div class="hint">Knock interval typically matches cylinder count (6, 8, or 12)</div>
                    </div>
                </div>

                <!-- Save Button -->
                <div class="card">
                    <div class="save-container">
                        <button id="sound-save-btn" class="btn btn-primary">Save Settings</button>
                        <span id="sound-save-status" class="status-text"></span>
                    </div>
                </div>
            </div>
        `;
    }

    init() {
        this.elements = {
            // Profile
            profile: document.getElementById('sound-profile'),
            profileDesc: document.getElementById('profile-desc'),
            enabled: document.getElementById('sound-enabled'),
            currentRpm: document.getElementById('current-rpm'),
            // Volume Presets
            volLow: document.getElementById('vol-low'),
            volLowBtn: document.getElementById('vol-low-btn'),
            presetLowItem: document.getElementById('preset-low-item'),
            volMedium: document.getElementById('vol-medium'),
            volMediumBtn: document.getElementById('vol-medium-btn'),
            presetMediumItem: document.getElementById('preset-medium-item'),
            volHigh: document.getElementById('vol-high'),
            volHighBtn: document.getElementById('vol-high-btn'),
            presetHighItem: document.getElementById('preset-high-item'),
            // Component Volumes
            idleVolume: document.getElementById('idle-volume'),
            idleVolumeVal: document.getElementById('idle-volume-val'),
            revVolume: document.getElementById('rev-volume'),
            revVolumeVal: document.getElementById('rev-volume-val'),
            knockVolume: document.getElementById('knock-volume'),
            knockVolumeVal: document.getElementById('knock-volume-val'),
            startVolume: document.getElementById('start-volume'),
            startVolumeVal: document.getElementById('start-volume-val'),
            // Engine
            maxRpm: document.getElementById('max-rpm'),
            maxRpmVal: document.getElementById('max-rpm-val'),
            acceleration: document.getElementById('acceleration'),
            accelerationVal: document.getElementById('acceleration-val'),
            deceleration: document.getElementById('deceleration'),
            decelerationVal: document.getElementById('deceleration-val'),
            jakeBrake: document.getElementById('jake-brake'),
            v8Mode: document.getElementById('v8-mode'),
            // Sound Effects
            airBrakeEnabled: document.getElementById('air-brake-enabled'),
            airBrakeVolume: document.getElementById('air-brake-volume'),
            airBrakeVolumeVal: document.getElementById('air-brake-volume-val'),
            reverseBeepEnabled: document.getElementById('reverse-beep-enabled'),
            reverseBeepVolume: document.getElementById('reverse-beep-volume'),
            reverseBeepVolumeVal: document.getElementById('reverse-beep-volume-val'),
            gearShiftEnabled: document.getElementById('gear-shift-enabled'),
            gearShiftVolume: document.getElementById('gear-shift-volume'),
            gearShiftVolumeVal: document.getElementById('gear-shift-volume-val'),
            wastegateEnabled: document.getElementById('wastegate-enabled'),
            wastegateVolume: document.getElementById('wastegate-volume'),
            wastegateVolumeVal: document.getElementById('wastegate-volume-val'),
            // Horn & Mode Switch
            hornEnabled: document.getElementById('horn-enabled'),
            hornVolume: document.getElementById('horn-volume'),
            hornVolumeVal: document.getElementById('horn-volume-val'),
            hornType: document.getElementById('horn-type'),
            modeSwitchEnabled: document.getElementById('mode-switch-enabled'),
            modeSwitchVolume: document.getElementById('mode-switch-volume'),
            modeSwitchVolumeVal: document.getElementById('mode-switch-volume-val'),
            // Advanced
            advancedToggle: document.getElementById('advanced-toggle'),
            advancedContent: document.getElementById('advanced-content'),
            revSwitch: document.getElementById('rev-switch'),
            revSwitchVal: document.getElementById('rev-switch-val'),
            idleEnd: document.getElementById('idle-end'),
            idleEndVal: document.getElementById('idle-end-val'),
            knockStart: document.getElementById('knock-start'),
            knockStartVal: document.getElementById('knock-start-val'),
            knockInterval: document.getElementById('knock-interval'),
            knockIntervalVal: document.getElementById('knock-interval-val'),
            // Save
            saveBtn: document.getElementById('sound-save-btn'),
            saveStatus: document.getElementById('sound-save-status')
        };

        // Setup slider value displays
        this.setupSlider('idleVolume', '%');
        this.setupSlider('revVolume', '%');
        this.setupSlider('knockVolume', '%');
        this.setupSlider('startVolume', '%');
        this.setupSlider('maxRpm', '%');
        this.setupSlider('acceleration', '');
        this.setupSlider('deceleration', '');
        // Sound effects
        this.setupSlider('airBrakeVolume', '%');
        this.setupSlider('reverseBeepVolume', '%');
        this.setupSlider('gearShiftVolume', '%');
        this.setupSlider('wastegateVolume', '%');
        // Horn & Mode Switch
        this.setupSlider('hornVolume', '%');
        this.setupSlider('modeSwitchVolume', '%');
        // Advanced
        this.setupSlider('revSwitch', '');
        this.setupSlider('idleEnd', '');
        this.setupSlider('knockStart', '');
        this.setupSlider('knockInterval', '');

        // Volume preset state
        this.currentPreset = 1;  // 0=Low, 1=Medium, 2=High

        // Preset apply buttons - click to apply that preset
        this.elements.volLowBtn.addEventListener('click', () => this.applyPreset(0));
        this.elements.volMediumBtn.addEventListener('click', () => this.applyPreset(1));
        this.elements.volHighBtn.addEventListener('click', () => this.applyPreset(2));

        // Profile change handler
        this.elements.profile.addEventListener('change', () => this.onProfileChange());

        // Advanced section toggle
        this.elements.advancedToggle.addEventListener('click', () => this.toggleAdvanced());

        // Save button
        this.elements.saveBtn.addEventListener('click', () => this.saveConfig());

        // Load data
        this.loadProfiles();
        this.loadConfig();
    }

    setupSlider(name, suffix) {
        const slider = this.elements[name];
        const valEl = this.elements[name + 'Val'];
        if (slider && valEl) {
            slider.addEventListener('input', () => {
                valEl.textContent = slider.value + suffix;
            });
        }
    }

    applyPreset(index) {
        // Apply the selected preset - sets master volume to that preset value
        this.currentPreset = index;
        this.updatePresetUI();

        // Get the preset value and apply it
        const presetValue = parseInt(this.getPresetValue(index));

        // Send to device immediately (applies volume)
        fetch('/api/sound', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                masterVolumeLevel1: presetValue,
                masterVolumeLevel2: presetValue
            })
        }).catch(err => console.error('Failed to apply preset:', err));
    }

    getPresetValue(index) {
        switch (index) {
            case 0: return this.elements.volLow.value;
            case 1: return this.elements.volMedium.value;
            case 2: return this.elements.volHigh.value;
            default: return this.elements.volMedium.value;
        }
    }

    updatePresetUI() {
        // Update active state on preset items
        this.elements.presetLowItem.classList.toggle('active', this.currentPreset === 0);
        this.elements.presetMediumItem.classList.toggle('active', this.currentPreset === 1);
        this.elements.presetHighItem.classList.toggle('active', this.currentPreset === 2);
    }

    toggleAdvanced() {
        const content = this.elements.advancedContent;
        const icon = this.elements.advancedToggle.querySelector('.collapse-icon');
        if (content.classList.contains('collapsed')) {
            content.classList.remove('collapsed');
            icon.textContent = '-';
        } else {
            content.classList.add('collapsed');
            icon.textContent = '+';
        }
    }

    loadProfiles() {
        fetch('/api/sound/profiles')
            .then(r => r.json())
            .then(data => {
                this.profiles = data.profiles || [];
                this.updateProfileSelect();
            })
            .catch(err => console.error('Failed to load profiles:', err));
    }

    updateProfileSelect() {
        const select = this.elements.profile;
        select.innerHTML = '';
        this.profiles.forEach(p => {
            const opt = document.createElement('option');
            opt.value = p.id;
            opt.textContent = p.name;
            select.appendChild(opt);
        });

        // Update description
        if (this.config) {
            select.value = this.config.profile;
            this.updateProfileDesc();
        }
    }

    updateProfileDesc() {
        const id = parseInt(this.elements.profile.value);
        const profile = this.profiles.find(p => p.id === id);
        if (profile) {
            this.elements.profileDesc.textContent = profile.description +
                ' (' + profile.cylinders + ' cyl' +
                (profile.hasJakeBrake ? ', jake brake' : '') + ')';
        }
    }

    onProfileChange() {
        this.updateProfileDesc();
    }

    loadConfig() {
        fetch('/api/sound')
            .then(r => r.json())
            .then(data => {
                this.config = data;
                this.updateUI();
            })
            .catch(err => console.error('Failed to load sound config:', err));
    }

    updateUI() {
        const cfg = this.config;
        if (!cfg) return;

        const el = this.elements;

        // Profile
        el.profile.value = cfg.profile;
        this.updateProfileDesc();
        el.enabled.checked = cfg.enabled;
        el.currentRpm.textContent = cfg.rpm;

        // Volume Presets - load values and current preset
        el.volLow.value = cfg.volumePresetLow || 20;
        el.volMedium.value = cfg.volumePresetMedium || 100;
        el.volHigh.value = cfg.volumePresetHigh || 170;
        this.currentPreset = cfg.currentVolumePreset || 1;
        this.updatePresetUI();

        el.idleVolume.value = cfg.idleVolume;
        el.idleVolumeVal.textContent = cfg.idleVolume + '%';
        el.revVolume.value = cfg.revVolume;
        el.revVolumeVal.textContent = cfg.revVolume + '%';
        el.knockVolume.value = cfg.knockVolume;
        el.knockVolumeVal.textContent = cfg.knockVolume + '%';
        el.startVolume.value = cfg.startVolume;
        el.startVolumeVal.textContent = cfg.startVolume + '%';

        // Engine
        el.maxRpm.value = cfg.maxRpmPercent;
        el.maxRpmVal.textContent = cfg.maxRpmPercent + '%';
        el.acceleration.value = cfg.acceleration;
        el.accelerationVal.textContent = cfg.acceleration;
        el.deceleration.value = cfg.deceleration;
        el.decelerationVal.textContent = cfg.deceleration;
        el.jakeBrake.checked = cfg.jakeBrakeEnabled;
        el.v8Mode.checked = cfg.v8Mode;

        // Sound Effects
        el.airBrakeEnabled.checked = cfg.airBrakeEnabled;
        el.airBrakeVolume.value = cfg.airBrakeVolume;
        el.airBrakeVolumeVal.textContent = cfg.airBrakeVolume + '%';
        el.reverseBeepEnabled.checked = cfg.reverseBeepEnabled;
        el.reverseBeepVolume.value = cfg.reverseBeepVolume;
        el.reverseBeepVolumeVal.textContent = cfg.reverseBeepVolume + '%';
        el.gearShiftEnabled.checked = cfg.gearShiftEnabled;
        el.gearShiftVolume.value = cfg.gearShiftVolume;
        el.gearShiftVolumeVal.textContent = cfg.gearShiftVolume + '%';
        el.wastegateEnabled.checked = cfg.wastegateEnabled;
        el.wastegateVolume.value = cfg.wastegateVolume;
        el.wastegateVolumeVal.textContent = cfg.wastegateVolume + '%';

        // Horn & Mode Switch
        el.hornEnabled.checked = cfg.hornEnabled;
        el.hornVolume.value = cfg.hornVolume;
        el.hornVolumeVal.textContent = cfg.hornVolume + '%';
        el.hornType.value = cfg.hornType;
        el.modeSwitchEnabled.checked = cfg.modeSwitchEnabled;
        el.modeSwitchVolume.value = cfg.modeSwitchVolume;
        el.modeSwitchVolumeVal.textContent = cfg.modeSwitchVolume + '%';

        // Advanced
        el.revSwitch.value = cfg.revSwitchPoint;
        el.revSwitchVal.textContent = cfg.revSwitchPoint;
        el.idleEnd.value = cfg.idleEndPoint;
        el.idleEndVal.textContent = cfg.idleEndPoint;
        el.knockStart.value = cfg.knockStartPoint;
        el.knockStartVal.textContent = cfg.knockStartPoint;
        el.knockInterval.value = cfg.knockInterval;
        el.knockIntervalVal.textContent = cfg.knockInterval;
    }

    onData(data) {
        // Update RPM display from WebSocket data if available
        // (would need to add rpm to status JSON in main.c)
    }

    saveConfig() {
        const el = this.elements;

        const config = {
            profile: parseInt(el.profile.value),
            enabled: el.enabled.checked,
            volumePresetLow: parseInt(el.volLow.value),
            volumePresetMedium: parseInt(el.volMedium.value),
            volumePresetHigh: parseInt(el.volHigh.value),
            idleVolume: parseInt(el.idleVolume.value),
            revVolume: parseInt(el.revVolume.value),
            knockVolume: parseInt(el.knockVolume.value),
            startVolume: parseInt(el.startVolume.value),
            maxRpmPercent: parseInt(el.maxRpm.value),
            acceleration: parseInt(el.acceleration.value),
            deceleration: parseInt(el.deceleration.value),
            jakeBrakeEnabled: el.jakeBrake.checked,
            v8Mode: el.v8Mode.checked,
            // Sound effects
            airBrakeEnabled: el.airBrakeEnabled.checked,
            airBrakeVolume: parseInt(el.airBrakeVolume.value),
            reverseBeepEnabled: el.reverseBeepEnabled.checked,
            reverseBeepVolume: parseInt(el.reverseBeepVolume.value),
            gearShiftEnabled: el.gearShiftEnabled.checked,
            gearShiftVolume: parseInt(el.gearShiftVolume.value),
            wastegateEnabled: el.wastegateEnabled.checked,
            wastegateVolume: parseInt(el.wastegateVolume.value),
            // Horn & Mode Switch
            hornEnabled: el.hornEnabled.checked,
            hornVolume: parseInt(el.hornVolume.value),
            hornType: parseInt(el.hornType.value),
            modeSwitchEnabled: el.modeSwitchEnabled.checked,
            modeSwitchVolume: parseInt(el.modeSwitchVolume.value),
            // Advanced
            revSwitchPoint: parseInt(el.revSwitch.value),
            idleEndPoint: parseInt(el.idleEnd.value),
            knockStartPoint: parseInt(el.knockStart.value),
            knockInterval: parseInt(el.knockInterval.value)
        };

        el.saveBtn.disabled = true;
        el.saveBtn.textContent = 'Saving...';
        el.saveStatus.textContent = '';

        fetch('/api/sound', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        })
        .then(r => r.json())
        .then(data => {
            el.saveBtn.textContent = 'Saved!';
            el.saveStatus.textContent = 'Settings saved successfully';
            el.saveStatus.className = 'status-text success';
            setTimeout(() => {
                el.saveBtn.textContent = 'Save Settings';
                el.saveBtn.disabled = false;
                el.saveStatus.textContent = '';
            }, 2000);
        })
        .catch(err => {
            console.error('Failed to save sound config:', err);
            el.saveBtn.textContent = 'Error!';
            el.saveStatus.textContent = 'Failed to save settings';
            el.saveStatus.className = 'status-text error';
            setTimeout(() => {
                el.saveBtn.textContent = 'Save Settings';
                el.saveBtn.disabled = false;
            }, 2000);
        });
    }

    destroy() {
    }
}
