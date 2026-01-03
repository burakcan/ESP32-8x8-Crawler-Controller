// 8x8 Crawler Web UI - Main Application Module
// ES6 module-based architecture with hash routing

import { DashboardPage } from './dashboard.js';
import { SettingsPage } from './settings.js';
import { CalibrationPage } from './calibration.js';
import { TuningPage } from './tuning.js';
import { SoundPage } from './sound.js';

// =============================================================================
// STATE
// =============================================================================

let ws = null;
let reconnectTimer = null;
let currentPage = null;
const RECONNECT_DELAY = 2000;

// Shared state accessible by all pages
export const state = {
    connected: false,
    status: null,
    wifiConfig: null
};

// Page registry
const pages = {
    dashboard: DashboardPage,
    settings: SettingsPage,
    calibration: CalibrationPage,
    tuning: TuningPage,
    sound: SoundPage
};

// =============================================================================
// WEBSOCKET
// =============================================================================

function connect() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(protocol + '//' + location.host + '/ws');

    ws.onopen = () => {
        state.connected = true;
        updateConnectionStatus(true);
        console.log('WebSocket connected');
    };

    ws.onclose = () => {
        state.connected = false;
        updateConnectionStatus(false);
        console.log('WebSocket disconnected, reconnecting...');
        reconnectTimer = setTimeout(connect, RECONNECT_DELAY);
    };

    ws.onerror = (err) => {
        console.error('WebSocket error:', err);
        ws.close();
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            state.status = data;

            // Update sidebar info
            updateSidebarInfo(data);

            // Notify current page
            if (currentPage && currentPage.onData) {
                currentPage.onData(data);
            }
        } catch (e) {
            console.error('Failed to parse message:', e);
        }
    };
}

function updateConnectionStatus(connected) {
    const el = document.getElementById('conn');
    if (connected) {
        el.textContent = 'Connected';
        el.className = 'status ok';
    } else {
        el.textContent = 'Disconnected';
        el.className = 'status err';
    }
}

function updateSidebarInfo(data) {
    // Uptime (JSON key: u=uptime_ms)
    const uptimeEl = document.getElementById('sidebar-uptime');
    if (uptimeEl && data.u !== undefined) {
        const secs = Math.floor(data.u / 1000);
        const mins = Math.floor(secs / 60);
        const hours = Math.floor(mins / 60);
        if (hours > 0) {
            uptimeEl.textContent = hours + 'h ' + (mins % 60) + 'm';
        } else if (mins > 0) {
            uptimeEl.textContent = mins + 'm ' + (secs % 60) + 's';
        } else {
            uptimeEl.textContent = secs + 's';
        }
    }

    // Signal status (JSON key: sl=signal_lost)
    const signalEl = document.getElementById('sidebar-signal');
    if (signalEl && data.sl !== undefined) {
        signalEl.textContent = data.sl ? 'LOST' : 'OK';
        signalEl.className = 'status ' + (data.sl ? 'err' : 'ok');
    }

    // Firmware version in header (JSON key: v=version)
    const versionEl = document.getElementById('header-version');
    if (versionEl && data.v) {
        versionEl.textContent = 'v' + data.v;
    }

    // WiFi STA status (JSON keys: wsc=connected, wsi=ip)
    const wifiRow = document.getElementById('sidebar-wifi-row');
    const wifiIpEl = document.getElementById('sidebar-wifi-ip');
    if (wifiRow && wifiIpEl) {
        if (data.wsc && data.wsi) {
            wifiRow.style.display = 'flex';
            wifiIpEl.textContent = data.wsi;
        } else {
            wifiRow.style.display = 'none';
        }
    }
}

// Export for pages that need to send data
export function sendMessage(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
    }
}

// =============================================================================
// ROUTER
// =============================================================================

function getRoute() {
    const hash = location.hash || '#/';
    const path = hash.slice(2) || 'dashboard'; // Remove '#/'
    return path.split('/')[0]; // Get first segment
}

function navigate() {
    const route = getRoute();
    const PageClass = pages[route] || pages.dashboard;

    // Cleanup previous page
    if (currentPage && currentPage.destroy) {
        currentPage.destroy();
    }

    // Create and render new page
    currentPage = new PageClass();
    const content = document.getElementById('content');
    content.innerHTML = currentPage.render();

    // Initialize page
    if (currentPage.init) {
        currentPage.init();
    }

    // If we have cached data, send it to the page
    if (state.status && currentPage.onData) {
        currentPage.onData(state.status);
    }

    // Update active nav link
    updateActiveNavLink(route);

    // Close mobile sidebar
    closeSidebar();
}

function updateActiveNavLink(route) {
    document.querySelectorAll('.nav-link').forEach(link => {
        const page = link.dataset.page;
        if (page === route || (route === '' && page === 'dashboard')) {
            link.classList.add('active');
        } else {
            link.classList.remove('active');
        }
    });
}

// =============================================================================
// SIDEBAR
// =============================================================================

function toggleSidebar() {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('sidebar-overlay');
    const toggle = document.getElementById('menu-toggle');

    sidebar.classList.toggle('open');
    overlay.classList.toggle('open');
    toggle.classList.toggle('active');
}

function closeSidebar() {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('sidebar-overlay');
    const toggle = document.getElementById('menu-toggle');

    sidebar.classList.remove('open');
    overlay.classList.remove('open');
    toggle.classList.remove('active');
}

// =============================================================================
// INITIALIZATION
// =============================================================================

document.addEventListener('DOMContentLoaded', () => {
    // Setup sidebar toggle
    document.getElementById('menu-toggle').addEventListener('click', toggleSidebar);
    document.getElementById('sidebar-overlay').addEventListener('click', closeSidebar);

    // Setup router
    window.addEventListener('hashchange', navigate);

    // Initial navigation
    navigate();

    // Connect WebSocket
    connect();
});
