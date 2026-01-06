// Settings Page - WiFi, OTA updates, SPIFFS management

export class SettingsPage {
    constructor() {
        this.elements = {};
        this.activeXhr = null;
    }

    render() {
        return `
            <div class="settings">
                <!-- WiFi Settings Card -->
                <div class="card">
                    <h2>WiFi Settings</h2>
                    <div class="wifi-container">
                        <div class="row">
                            <span class="label">External WiFi</span>
                            <label class="toggle">
                                <input type="checkbox" id="wifi-enabled"/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                        <div class="wifi-status">
                            <span class="label">Status:</span>
                            <span id="wifi-conn-status" class="status">-</span>
                        </div>
                        <div class="wifi-ip" id="wifi-ip-row" style="display:none">
                            <span class="label">IP:</span>
                            <span class="value" id="wifi-ip">-</span>
                        </div>
                        <div class="wifi-form">
                            <input type="text" id="wifi-ssid" placeholder="WiFi SSID" maxlength="32"/>
                            <input type="password" id="wifi-pass" placeholder="Password" maxlength="64"/>
                            <button id="wifi-save-btn" class="btn btn-primary">Save & Connect</button>
                        </div>
                        <div class="hint">Connect to your home WiFi for OTA updates without switching networks</div>
                    </div>
                </div>

                <!-- Firmware Update Card -->
                <div class="card">
                    <h2>Firmware Update</h2>
                    <div class="ota-container">
                        <div class="row">
                            <span class="label">Current Version:</span>
                            <span class="value" id="fw-version">-</span>
                        </div>
                        <input type="file" id="ota-file" accept=".bin"/>
                        <button id="ota-btn" class="btn btn-primary">Upload Firmware</button>
                        <div class="progress" id="ota-progress">
                            <div class="progress-bar" id="ota-bar"></div>
                        </div>
                        <div class="status-text" id="ota-status"></div>
                    </div>
                </div>

                <!-- Web UI Update Card -->
                <div class="card">
                    <h2>Web UI Update</h2>
                    <div class="spiffs-container">
                        <div class="row">
                            <span class="label">Storage:</span>
                            <span class="value" id="spiffs-usage">-</span>
                        </div>
                        <div class="spiffs-files" id="spiffs-files">
                            <div class="spiffs-empty">Loading...</div>
                        </div>
                        <input type="file" id="spiffs-file" multiple accept=".html,.css,.js,.json,.ico,.svg"/>
                        <button id="spiffs-btn" class="btn btn-primary">Upload Files</button>
                        <div class="progress" id="spiffs-progress">
                            <div class="progress-bar" id="spiffs-bar"></div>
                        </div>
                        <div class="status-text" id="spiffs-status"></div>
                        <div class="hint">Upload web files (html, css, js). Refresh page after upload.</div>
                    </div>
                </div>

                <!-- System Controls Card -->
                <div class="card">
                    <h2>System</h2>
                    <div class="system-container">
                        <div class="system-buttons">
                            <button id="restart-btn" class="btn btn-secondary">Restart</button>
                            <button id="bootloader-btn" class="btn btn-danger">Restart for Flash</button>
                        </div>
                        <div class="hint">Use "Restart for Flash" then run idf.py flash within a few seconds. For ESP32, hold BOOT button during restart if auto-flash fails.</div>
                    </div>
                </div>
            </div>
        `;
    }

    init() {
        this.elements = {
            // WiFi
            wifiEnabled: document.getElementById('wifi-enabled'),
            wifiConnStatus: document.getElementById('wifi-conn-status'),
            wifiIpRow: document.getElementById('wifi-ip-row'),
            wifiIp: document.getElementById('wifi-ip'),
            wifiSsid: document.getElementById('wifi-ssid'),
            wifiPass: document.getElementById('wifi-pass'),
            wifiSaveBtn: document.getElementById('wifi-save-btn'),
            // OTA
            fwVersion: document.getElementById('fw-version'),
            otaFile: document.getElementById('ota-file'),
            otaBtn: document.getElementById('ota-btn'),
            otaProgress: document.getElementById('ota-progress'),
            otaBar: document.getElementById('ota-bar'),
            otaStatus: document.getElementById('ota-status'),
            // SPIFFS
            spiffsUsage: document.getElementById('spiffs-usage'),
            spiffsFiles: document.getElementById('spiffs-files'),
            spiffsFile: document.getElementById('spiffs-file'),
            spiffsBtn: document.getElementById('spiffs-btn'),
            spiffsProgress: document.getElementById('spiffs-progress'),
            spiffsBar: document.getElementById('spiffs-bar'),
            spiffsStatus: document.getElementById('spiffs-status'),
            // System
            restartBtn: document.getElementById('restart-btn'),
            bootloaderBtn: document.getElementById('bootloader-btn')
        };

        // Event handlers
        this.elements.wifiSaveBtn.addEventListener('click', () => this.saveWifiConfig());
        this.elements.otaBtn.addEventListener('click', () => this.uploadFirmware());
        this.elements.spiffsBtn.addEventListener('click', () => this.uploadSpiffsFiles());
        this.elements.restartBtn.addEventListener('click', () => this.restartDevice());
        this.elements.bootloaderBtn.addEventListener('click', () => this.enterBootloader());

        // Load initial data
        this.loadWifiConfig();
        this.loadSpiffsFiles();
    }

    onData(data) {
        // Update firmware version (JSON key: v=version, b=build)
        if (data.v && this.elements.fwVersion) {
            this.elements.fwVersion.textContent = data.v + (data.b ? ' (build ' + data.b + ')' : '');
        }

        // Update WiFi status from WebSocket (JSON keys: wse=enabled, wsc=connected, wsi=ip)
        if (data.wse !== undefined || data.wsc !== undefined) {
            this.updateWifiStatusFromWs(data);
        }
    }

    updateWifiStatusFromWs(data) {
        const el = this.elements;
        if (!el.wifiConnStatus) return;

        if (data.wsc && data.wsi) {
            el.wifiConnStatus.textContent = 'Connected';
            el.wifiConnStatus.className = 'status ok';
            el.wifiIpRow.style.display = 'flex';
            el.wifiIp.textContent = data.wsi;
        } else if (data.wse) {
            // Show disconnect reason if available (wsr=reason code, wsrs=reason string)
            if (data.wsr && data.wsrs) {
                el.wifiConnStatus.textContent = 'Failed: ' + data.wsrs;
                el.wifiConnStatus.className = 'status err';
            } else {
                el.wifiConnStatus.textContent = 'Connecting...';
                el.wifiConnStatus.className = 'status warn';
            }
            el.wifiIpRow.style.display = 'none';
        } else {
            el.wifiConnStatus.textContent = 'Disabled';
            el.wifiConnStatus.className = 'status';
            el.wifiIpRow.style.display = 'none';
        }
    }

    // =========================================================================
    // WiFi
    // =========================================================================

    loadWifiConfig() {
        fetch('/api/wifi')
            .then(r => r.json())
            .then(data => {
                this.elements.wifiEnabled.checked = data.enabled;
                this.elements.wifiSsid.value = data.ssid || '';
                this.updateWifiStatus(data);
            })
            .catch(err => console.error('Failed to load WiFi config:', err));
    }

    updateWifiStatus(data) {
        const el = this.elements;
        if (!el.wifiConnStatus) return;

        if (data.wifi_sta_connected || data.connected) {
            el.wifiConnStatus.textContent = 'Connected';
            el.wifiConnStatus.className = 'status ok';
            if (data.wifi_sta_ip || data.sta_ip) {
                el.wifiIpRow.style.display = 'flex';
                el.wifiIp.textContent = data.wifi_sta_ip || data.sta_ip;
            }
        } else if (data.enabled) {
            el.wifiConnStatus.textContent = 'Connecting...';
            el.wifiConnStatus.className = 'status warn';
            el.wifiIpRow.style.display = 'none';
        } else {
            el.wifiConnStatus.textContent = 'Disabled';
            el.wifiConnStatus.className = 'status';
            el.wifiIpRow.style.display = 'none';
        }
    }

    saveWifiConfig() {
        const el = this.elements;
        const config = {
            enabled: el.wifiEnabled.checked,
            ssid: el.wifiSsid.value,
            password: el.wifiPass.value
        };

        el.wifiSaveBtn.disabled = true;
        el.wifiSaveBtn.textContent = 'Saving...';

        fetch('/api/wifi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        })
        .then(r => r.json())
        .then(data => {
            el.wifiSaveBtn.textContent = 'Saved!';
            setTimeout(() => {
                el.wifiSaveBtn.textContent = 'Save & Connect';
                el.wifiSaveBtn.disabled = false;
            }, 2000);
        })
        .catch(err => {
            console.error('Failed to save WiFi config:', err);
            el.wifiSaveBtn.textContent = 'Error!';
            setTimeout(() => {
                el.wifiSaveBtn.textContent = 'Save & Connect';
                el.wifiSaveBtn.disabled = false;
            }, 2000);
        });
    }

    // =========================================================================
    // OTA Firmware Update
    // =========================================================================

    uploadFirmware() {
        const el = this.elements;
        const file = el.otaFile.files[0];

        if (!file) {
            this.setOtaStatus('Please select a firmware file', 'error');
            return;
        }

        if (!file.name.endsWith('.bin')) {
            this.setOtaStatus('Invalid file type. Please select a .bin file', 'error');
            return;
        }

        if (!confirm('Upload firmware: ' + file.name + '?\n\nThe device will restart after update.')) {
            return;
        }

        el.otaBtn.disabled = true;
        el.otaProgress.classList.add('active');
        this.setOtaStatus('Uploading...', '');

        const xhr = new XMLHttpRequest();
        this.activeXhr = xhr;
        xhr.open('POST', '/api/ota', true);

        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                el.otaBar.style.width = pct + '%';
                this.setOtaStatus('Uploading: ' + pct + '%', '');
            }
        };

        xhr.onload = () => {
            this.activeXhr = null;
            if (xhr.status === 200) {
                this.setOtaStatus('Update complete! Restarting...', 'success');
                setTimeout(() => location.reload(), 5000);
            } else {
                this.setOtaStatus('Upload failed: ' + xhr.responseText, 'error');
                el.otaBtn.disabled = false;
                el.otaProgress.classList.remove('active');
            }
        };

        xhr.onerror = () => {
            this.activeXhr = null;
            this.setOtaStatus('Network error', 'error');
            el.otaBtn.disabled = false;
            el.otaProgress.classList.remove('active');
        };

        xhr.send(file);
    }

    setOtaStatus(message, type) {
        const el = this.elements.otaStatus;
        el.textContent = message;
        el.className = 'status-text' + (type ? ' ' + type : '');
    }

    // =========================================================================
    // SPIFFS File Management
    // =========================================================================

    loadSpiffsFiles() {
        fetch('/api/spiffs')
            .then(r => r.json())
            .then(data => {
                const el = this.elements;

                // Storage usage
                const usedKB = (data.used / 1024).toFixed(1);
                const totalKB = (data.total / 1024).toFixed(1);
                const pct = Math.round((data.used / data.total) * 100);
                el.spiffsUsage.textContent = usedKB + ' / ' + totalKB + ' KB (' + pct + '%)';

                // File list
                let html = '';
                if (data.files && data.files.length > 0) {
                    data.files.forEach(f => {
                        const size = f.size < 1024 ? f.size + ' B' : (f.size / 1024).toFixed(1) + ' KB';
                        html += '<div class="spiffs-file-row">' +
                            '<span class="spiffs-filename">' + f.name + '</span>' +
                            '<span class="spiffs-filesize">' + size + '</span>' +
                            '</div>';
                    });
                } else {
                    html = '<div class="spiffs-empty">No files</div>';
                }
                el.spiffsFiles.innerHTML = html;
            })
            .catch(err => {
                console.error('Failed to load SPIFFS files:', err);
                this.elements.spiffsFiles.innerHTML = '<div class="spiffs-empty">Failed to load</div>';
            });
    }

    uploadSpiffsFiles() {
        const el = this.elements;
        const files = el.spiffsFile.files;

        if (files.length === 0) {
            this.setSpiffsStatus('Please select files to upload', 'error');
            return;
        }

        const fileList = Array.from(files).map(f => f.name).join(', ');
        if (!confirm('Upload ' + files.length + ' file(s)?\n\n' + fileList)) {
            return;
        }

        el.spiffsBtn.disabled = true;
        el.spiffsProgress.classList.add('active');

        let uploaded = 0;
        const total = files.length;

        const uploadNext = (index) => {
            if (index >= total) {
                this.setSpiffsStatus('Uploaded ' + uploaded + ' file(s). Refresh to see changes.', 'success');
                el.spiffsBtn.disabled = false;
                el.spiffsProgress.classList.remove('active');
                el.spiffsFile.value = '';
                this.loadSpiffsFiles();
                return;
            }

            const file = files[index];
            this.setSpiffsStatus('Uploading ' + file.name + '...', '');
            el.spiffsBar.style.width = ((index / total) * 100) + '%';

            const xhr = new XMLHttpRequest();
            this.activeXhr = xhr;
            xhr.open('POST', '/api/spiffs?file=' + encodeURIComponent(file.name), true);

            xhr.onload = () => {
                this.activeXhr = null;
                if (xhr.status === 200) {
                    uploaded++;
                    uploadNext(index + 1);
                } else {
                    this.setSpiffsStatus('Failed to upload ' + file.name, 'error');
                    el.spiffsBtn.disabled = false;
                    el.spiffsProgress.classList.remove('active');
                }
            };

            xhr.onerror = () => {
                this.activeXhr = null;
                this.setSpiffsStatus('Network error uploading ' + file.name, 'error');
                el.spiffsBtn.disabled = false;
                el.spiffsProgress.classList.remove('active');
            };

            xhr.send(file);
        };

        uploadNext(0);
    }

    setSpiffsStatus(message, type) {
        const el = this.elements.spiffsStatus;
        el.textContent = message;
        el.className = 'status-text' + (type ? ' ' + type : '');
    }

    // =========================================================================
    // System Controls
    // =========================================================================

    restartDevice() {
        if (!confirm('Restart the device?\n\nYou will need to reconnect after restart.')) {
            return;
        }

        this.elements.restartBtn.disabled = true;
        this.elements.restartBtn.textContent = 'Restarting...';

        fetch('/api/restart', { method: 'POST' })
            .then(() => {
                // Connection will be lost, just wait and reload
                setTimeout(() => location.reload(), 5000);
            })
            .catch(() => {
                // Expected - device is restarting
                setTimeout(() => location.reload(), 5000);
            });
    }

    enterBootloader() {
        if (!confirm('Restart for USB flashing?\n\nAfter clicking OK, run "idf.py flash" immediately.\n\nFor original ESP32: if auto-flash fails, hold BOOT button while clicking OK.')) {
            return;
        }

        this.elements.bootloaderBtn.disabled = true;
        this.elements.bootloaderBtn.textContent = 'Restarting...';

        fetch('/api/bootloader', { method: 'POST' })
            .catch(() => {
                // Expected - device is restarting
            });
    }

    destroy() {
        if (this.activeXhr) {
            this.activeXhr.abort();
            this.activeXhr = null;
        }
    }
}
