// 8x8 Crawler Web Dashboard
// WebSocket client for real-time status updates

let ws = null;
let reconnectTimer = null;

// Mode descriptions (indices match steering_mode_t enum)
const modeDescriptions = [
    "Front: Axles 1-2 steer (car-like)",      // 0 - STEER_MODE_FRONT
    "Rear: Axles 3-4 steer",                   // 1 - STEER_MODE_REAR
    "All Axle: 1-2 opposite to 3-4 (tight)",  // 2 - STEER_MODE_ALL_AXLE
    "Crab: All same direction (sideways)"      // 3 - STEER_MODE_CRAB
];

// Aux switch to mode mapping info
const auxModeMap = {
    "OFF/OFF": "Front",
    "ON/OFF": "All Axle", 
    "OFF/ON": "Crab",
    "ON/ON": "Rear"
};

// DOM elements
const elements = {
    conn: document.getElementById('conn'),
    thr: document.getElementById('thr'),
    str: document.getElementById('str'),
    thrBar: document.getElementById('thr-bar'),
    strBar: document.getElementById('str-bar'),
    sig: document.getElementById('sig'),
    aux1St: document.getElementById('aux1-st'),
    aux2St: document.getElementById('aux2-st'),
    escVal: document.getElementById('esc-val'),
    a1Val: document.getElementById('a1-val'),
    a2Val: document.getElementById('a2-val'),
    a3Val: document.getElementById('a3-val'),
    a4Val: document.getElementById('a4-val'),
    cal: document.getElementById('cal'),
    up: document.getElementById('up'),
    fwVer: document.getElementById('fw-ver'),
    modeBtns: document.querySelectorAll('.mode-btn:not(.aux-btn)'),
    auxBtn: document.getElementById('aux-mode'),
    modeDesc: document.getElementById('mode-desc'),
    // Wheel elements for rotation
    wheel1l: document.getElementById('wheel1l'),
    wheel1r: document.getElementById('wheel1r'),
    wheel2l: document.getElementById('wheel2l'),
    wheel2r: document.getElementById('wheel2r'),
    wheel3l: document.getElementById('wheel3l'),
    wheel3r: document.getElementById('wheel3r'),
    wheel4l: document.getElementById('wheel4l'),
    wheel4r: document.getElementById('wheel4r'),
    // OTA elements
    otaFile: document.getElementById('ota-file'),
    otaBtn: document.getElementById('ota-btn'),
    otaProgress: document.getElementById('ota-progress'),
    otaBar: document.getElementById('ota-bar'),
    otaStatus: document.getElementById('ota-status'),
    // WiFi elements
    wifiEnabled: document.getElementById('wifi-enabled'),
    wifiConnStatus: document.getElementById('wifi-conn-status'),
    wifiIpRow: document.getElementById('wifi-ip-row'),
    wifiIp: document.getElementById('wifi-ip'),
    wifiSsid: document.getElementById('wifi-ssid'),
    wifiPass: document.getElementById('wifi-pass'),
    wifiSaveBtn: document.getElementById('wifi-save-btn'),
    // SPIFFS elements
    spiffsUsage: document.getElementById('spiffs-usage'),
    spiffsFiles: document.getElementById('spiffs-files'),
    spiffsFile: document.getElementById('spiffs-file'),
    spiffsBtn: document.getElementById('spiffs-btn'),
    spiffsProgress: document.getElementById('spiffs-progress'),
    spiffsBar: document.getElementById('spiffs-bar'),
    spiffsStatus: document.getElementById('spiffs-status')
};

// Connect to WebSocket
function connect() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${location.host}/ws`);
    
    ws.onopen = function() {
        console.log('WebSocket connected');
        elements.conn.className = 'status ok';
        elements.conn.textContent = 'Connected';
    };
    
    ws.onclose = function() {
        console.log('WebSocket disconnected');
        elements.conn.className = 'status err';
        elements.conn.textContent = 'Disconnected';
        reconnectTimer = setTimeout(connect, 1000);
    };
    
    ws.onerror = function(err) {
        console.error('WebSocket error:', err);
        ws.close();
    };
    
    ws.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            updateUI(data);
        } catch (e) {
            console.error('Failed to parse message:', e);
        }
    };
}

// Update wheel rotation based on servo pulse value
function updateWheelRotation(wheelL, wheelR, pulseUs) {
    // Convert pulse (1000-2000us) to angle (-30 to +30 degrees)
    // 1500 = center = 0 degrees
    const angle = ((pulseUs - 1500) / 500) * 30;
    
    // Get wheel dimensions for rotation center
    const rect = wheelL.getBBox();
    const cx = rect.x + rect.width / 2;
    const cy = rect.y + rect.height / 2;
    wheelL.setAttribute('transform', `rotate(${angle}, ${cx}, ${cy})`);
    
    const rect2 = wheelR.getBBox();
    const cx2 = rect2.x + rect2.width / 2;
    const cy2 = rect2.y + rect2.height / 2;
    wheelR.setAttribute('transform', `rotate(${angle}, ${cx2}, ${cy2})`);
    
    // Color based on turn direction
    const colorClass = angle < -3 ? 'turning-left' : (angle > 3 ? 'turning-right' : '');
    wheelL.className.baseVal = 'wheel ' + colorClass;
    wheelR.className.baseVal = 'wheel ' + colorClass;
}

// Update UI with received data
function updateUI(data) {
    // RC values
    elements.thr.textContent = data.t;
    elements.str.textContent = data.s;
    
    // Progress bars (convert -1000..+1000 to 0..100%)
    elements.thrBar.style.width = ((data.t + 1000) / 20) + '%';
    elements.strBar.style.width = ((data.s + 1000) / 20) + '%';
    
    // Aux switch status
    const aux1On = data.x1 > 200;
    const aux2On = data.x2 > 200;
    elements.aux1St.textContent = aux1On ? 'ON' : 'OFF';
    elements.aux1St.className = 'aux-val ' + (aux1On ? 'aux-on' : 'aux-off');
    elements.aux2St.textContent = aux2On ? 'ON' : 'OFF';
    elements.aux2St.className = 'aux-val ' + (aux2On ? 'aux-on' : 'aux-off');
    
    // ESC value
    elements.escVal.textContent = data.e;
    
    // Axle servo values
    elements.a1Val.textContent = data.a1;
    elements.a2Val.textContent = data.a2;
    elements.a3Val.textContent = data.a3;
    elements.a4Val.textContent = data.a4;
    
    // Update wheel rotations
    updateWheelRotation(elements.wheel1l, elements.wheel1r, data.a1);
    updateWheelRotation(elements.wheel2l, elements.wheel2r, data.a2);
    updateWheelRotation(elements.wheel3l, elements.wheel3r, data.a3);
    updateWheelRotation(elements.wheel4l, elements.wheel4r, data.a4);
    
    // Signal status
    if (data.sl) {
        elements.sig.className = 'status err';
        elements.sig.textContent = 'LOST';
    } else {
        elements.sig.className = 'status ok';
        elements.sig.textContent = 'OK';
    }
    
    // Calibration status
    if (data.cg) {
        elements.cal.className = 'status warn';
        elements.cal.textContent = data.cp + '%';
    } else if (data.cd) {
        elements.cal.className = 'status ok';
        elements.cal.textContent = 'Done';
    } else {
        elements.cal.className = 'status err';
        elements.cal.textContent = 'Needed';
    }
    
    // Steering mode buttons and description
    // data.ui = true means UI is controlling the mode, false = AUX switches
    elements.modeBtns.forEach(btn => {
        const mode = parseInt(btn.dataset.m);
        btn.classList.toggle('active', data.ui && mode === data.m);
    });
    elements.auxBtn.classList.toggle('active', !data.ui);
    
    const modeSource = data.ui ? '' : ' (AUX)';
    elements.modeDesc.textContent = (modeDescriptions[data.m] || 'Unknown') + modeSource;
    
    // Uptime
    elements.up.textContent = formatUptime(data.u);

    // Firmware version
    if (data.v) {
        elements.fwVer.textContent = 'v' + data.v + ' (b' + data.b + ')';
    }

    // WiFi STA status
    updateWifiStatus(data.wse, data.wsc, data.wss, data.wsi);
}

// Format uptime
function formatUptime(seconds) {
    if (seconds < 60) {
        return seconds + 's';
    } else if (seconds < 3600) {
        const mins = Math.floor(seconds / 60);
        const secs = seconds % 60;
        return mins + 'm ' + secs + 's';
    } else {
        const hours = Math.floor(seconds / 3600);
        const mins = Math.floor((seconds % 3600) / 60);
        return hours + 'h ' + mins + 'm';
    }
}

// Send command to ESP32
function sendCommand(cmd, value) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const msg = value !== undefined 
            ? JSON.stringify({cmd: cmd, v: value})
            : JSON.stringify({cmd: cmd});
        ws.send(msg);
        console.log('Sent:', msg);
    }
}

// Set steering mode via UI
function setMode(mode) {
    sendCommand('mode', mode);
}

// Revert to AUX switch control
function useAuxControl() {
    sendCommand('aux');
}

// Start connection
document.addEventListener('DOMContentLoaded', function() {
    connect();
    
    // Add click handlers to mode buttons
    elements.modeBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const mode = parseInt(this.dataset.m);
            setMode(mode);
        });
    });
    
    // Auto (AUX switches) button
    elements.auxBtn.addEventListener('click', function() {
        useAuxControl();
    });
});

// Reconnect on visibility change
document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'visible' && (!ws || ws.readyState !== WebSocket.OPEN)) {
        connect();
    }
});

// OTA Update Functions
function uploadFirmware() {
    const file = elements.otaFile.files[0];
    if (!file) {
        setOtaStatus('Please select a firmware file', 'error');
        return;
    }

    if (!file.name.endsWith('.bin')) {
        setOtaStatus('Invalid file type. Please select a .bin file', 'error');
        return;
    }

    // Confirm before upload
    if (!confirm('Upload firmware: ' + file.name + ' (' + formatBytes(file.size) + ')?\n\nThe device will reboot after update.')) {
        return;
    }

    // Disable button during upload
    elements.otaBtn.disabled = true;
    elements.otaProgress.classList.add('active');
    setOtaStatus('Uploading...', '');

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota', true);

    // Progress handler
    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            elements.otaBar.style.width = percent + '%';
            setOtaStatus('Uploading: ' + percent + '%', '');
        }
    };

    // Complete handler
    xhr.onload = function() {
        if (xhr.status === 200) {
            elements.otaBar.style.width = '100%';
            setOtaStatus('Update complete! Rebooting...', 'success');
            // The device will reboot, so we'll lose connection
            setTimeout(function() {
                setOtaStatus('Reconnecting in 5 seconds...', 'success');
            }, 2000);
            setTimeout(function() {
                location.reload();
            }, 7000);
        } else {
            let errorMsg = 'Upload failed';
            try {
                const resp = JSON.parse(xhr.responseText);
                if (resp.message) errorMsg = resp.message;
            } catch (e) {}
            setOtaStatus(errorMsg, 'error');
            resetOtaUI();
        }
    };

    // Error handler
    xhr.onerror = function() {
        setOtaStatus('Upload failed: Network error', 'error');
        resetOtaUI();
    };

    // Timeout handler
    xhr.timeout = 120000; // 2 minute timeout
    xhr.ontimeout = function() {
        setOtaStatus('Upload failed: Timeout', 'error');
        resetOtaUI();
    };

    // Send the file
    xhr.send(file);
}

function setOtaStatus(message, type) {
    elements.otaStatus.textContent = message;
    elements.otaStatus.className = 'ota-status' + (type ? ' ' + type : '');
}

function resetOtaUI() {
    elements.otaBtn.disabled = false;
    elements.otaProgress.classList.remove('active');
    elements.otaBar.style.width = '0%';
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
}

// Add OTA button click handler
document.addEventListener('DOMContentLoaded', function() {
    elements.otaBtn.addEventListener('click', uploadFirmware);
});

// WiFi Settings Functions
let wifiInitialized = false;

function updateWifiStatus(enabled, connected, ssid, ip) {
    // Update checkbox without triggering change event
    if (!wifiInitialized) {
        elements.wifiEnabled.checked = enabled;
        if (ssid) elements.wifiSsid.value = ssid;
        wifiInitialized = true;
    }

    // Update connection status
    if (enabled) {
        if (connected) {
            elements.wifiConnStatus.textContent = 'Connected';
            elements.wifiConnStatus.className = 'status ok';
            elements.wifiIpRow.style.display = 'flex';
            elements.wifiIp.textContent = ip || '-';
        } else {
            elements.wifiConnStatus.textContent = 'Connecting...';
            elements.wifiConnStatus.className = 'status warn';
            elements.wifiIpRow.style.display = 'none';
        }
    } else {
        elements.wifiConnStatus.textContent = 'Disabled';
        elements.wifiConnStatus.className = 'status';
        elements.wifiIpRow.style.display = 'none';
    }
}

function saveWifiConfig() {
    const enabled = elements.wifiEnabled.checked;
    const ssid = elements.wifiSsid.value.trim();
    const password = elements.wifiPass.value;

    if (enabled && !ssid) {
        alert('Please enter a WiFi SSID');
        return;
    }

    elements.wifiSaveBtn.disabled = true;
    elements.wifiSaveBtn.textContent = 'Saving...';

    const config = {
        enabled: enabled,
        ssid: ssid,
        password: password
    };

    fetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
    })
    .then(response => response.json())
    .then(data => {
        elements.wifiSaveBtn.textContent = 'Saved!';
        setTimeout(() => {
            elements.wifiSaveBtn.disabled = false;
            elements.wifiSaveBtn.textContent = 'Save & Connect';
        }, 2000);
        // Clear password field after save
        elements.wifiPass.value = '';
    })
    .catch(error => {
        console.error('WiFi config error:', error);
        elements.wifiSaveBtn.textContent = 'Error!';
        setTimeout(() => {
            elements.wifiSaveBtn.disabled = false;
            elements.wifiSaveBtn.textContent = 'Save & Connect';
        }, 2000);
    });
}

// WiFi event handlers
document.addEventListener('DOMContentLoaded', function() {
    elements.wifiSaveBtn.addEventListener('click', saveWifiConfig);
});

// SPIFFS File Management Functions
function loadSpiffsFiles() {
    fetch('/api/spiffs')
        .then(response => response.json())
        .then(data => {
            // Update storage usage
            const usedKB = (data.used / 1024).toFixed(1);
            const totalKB = (data.total / 1024).toFixed(1);
            const percent = Math.round((data.used / data.total) * 100);
            elements.spiffsUsage.textContent = usedKB + ' / ' + totalKB + ' KB (' + percent + '%)';

            // Update file list
            let html = '';
            if (data.files && data.files.length > 0) {
                data.files.forEach(file => {
                    const sizeStr = file.size < 1024 ? file.size + ' B' : (file.size / 1024).toFixed(1) + ' KB';
                    html += '<div class="spiffs-file-row">' +
                        '<span class="spiffs-filename">' + file.name + '</span>' +
                        '<span class="spiffs-filesize">' + sizeStr + '</span>' +
                        '</div>';
                });
            } else {
                html = '<div class="spiffs-empty">No files</div>';
            }
            elements.spiffsFiles.innerHTML = html;
        })
        .catch(error => {
            console.error('Failed to load SPIFFS files:', error);
            elements.spiffsFiles.innerHTML = '<div class="spiffs-empty">Failed to load</div>';
        });
}

function uploadSpiffsFiles() {
    const files = elements.spiffsFile.files;
    if (files.length === 0) {
        setSpiffsStatus('Please select files to upload', 'error');
        return;
    }

    // Confirm upload
    let fileList = Array.from(files).map(f => f.name).join(', ');
    if (!confirm('Upload ' + files.length + ' file(s)?\n\n' + fileList)) {
        return;
    }

    elements.spiffsBtn.disabled = true;
    elements.spiffsProgress.classList.add('active');

    let uploaded = 0;
    const total = files.length;

    function uploadNext(index) {
        if (index >= total) {
            // All done
            setSpiffsStatus('Uploaded ' + uploaded + ' file(s). Refresh page to see changes.', 'success');
            elements.spiffsBtn.disabled = false;
            elements.spiffsProgress.classList.remove('active');
            elements.spiffsFile.value = '';
            loadSpiffsFiles();
            return;
        }

        const file = files[index];
        setSpiffsStatus('Uploading ' + file.name + '...', '');
        elements.spiffsBar.style.width = ((index / total) * 100) + '%';

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/spiffs?file=' + encodeURIComponent(file.name), true);

        xhr.onload = function() {
            if (xhr.status === 200) {
                uploaded++;
                uploadNext(index + 1);
            } else {
                setSpiffsStatus('Failed to upload ' + file.name, 'error');
                elements.spiffsBtn.disabled = false;
                elements.spiffsProgress.classList.remove('active');
            }
        };

        xhr.onerror = function() {
            setSpiffsStatus('Network error uploading ' + file.name, 'error');
            elements.spiffsBtn.disabled = false;
            elements.spiffsProgress.classList.remove('active');
        };

        xhr.send(file);
    }

    uploadNext(0);
}

function setSpiffsStatus(message, type) {
    elements.spiffsStatus.textContent = message;
    elements.spiffsStatus.className = 'ota-status' + (type ? ' ' + type : '');
}

// SPIFFS event handlers
document.addEventListener('DOMContentLoaded', function() {
    elements.spiffsBtn.addEventListener('click', uploadSpiffsFiles);
    loadSpiffsFiles();
});