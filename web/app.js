/**
 * AutoLevel DJ - Mobile WebGUI Client
 * Real-time SSE Telemetry, Touch Controls & Dynamic 6-Band Canvas Visualizer
 */

(function() {
    'use strict';

    // State cache
    let isConnected = false;
    let eventSource = null;
    let reconnectTimer = null;
    let canvas = null;
    let ctx = null;

    // Visual State buffers for 60fps smoothing
    const visualState = {
        momentaryLufs: -14.0,
        integratedLufs: -14.0,
        appliedGainDb: 0.0,
        limiterGrDb: 0.0,
        peakL: -60.0,
        peakR: -60.0,
        isFrozen: false,
        mbcGrDb: [0, 0, 0, 0, 0, 0],
        mbcThresholdsDb: [-12, -22, -22, -25, -32, -31],
        targetProfile: 1, // 0 = Linear, 1 = Modern Mix
        bassLiftDb: 0.0,
        airLiftDb: 0.0
    };

    // DOM Elements
    const elements = {
        connectionDot: document.getElementById('connection-dot'),
        connectionStatus: document.getElementById('connection-status'),
        audioDeviceInfo: document.getElementById('audio-device-info'),
        btnReset: document.getElementById('btn-reset'),
        btnBypass: document.getElementById('btn-bypass'),

        // Meters
        valMomentary: document.getElementById('val-momentary'),
        valIntegrated: document.getElementById('val-integrated'),
        meterMomentary: document.getElementById('meter-momentary'),
        meterIntegrated: document.getElementById('meter-integrated'),
        valRider: document.getElementById('val-rider'),
        meterRider: document.getElementById('meter-rider'),
        valLimiterGr: document.getElementById('val-limiter-gr'),
        meterLimiterGr: document.getElementById('meter-limiter-gr'),
        valPeak: document.getElementById('val-peak'),
        peakL: document.getElementById('peak-l'),
        peakR: document.getElementById('peak-r'),
        clipL: document.getElementById('clip-l'),
        clipR: document.getElementById('clip-r'),
        freezeBadge: document.getElementById('freeze-badge'),
        activeProfileTag: document.getElementById('active-profile-tag'),
        valBassLift: document.getElementById('val-bass-lift'),
        valAirLift: document.getElementById('val-air-lift'),

        // Faders
        faderTargetLufs: document.getElementById('fader-target-lufs'),
        numTargetLufs: document.getElementById('num-target-lufs'),
        faderLevelResponse: document.getElementById('fader-level-response'),
        numLevelResponse: document.getElementById('num-level-response'),
        faderToneSlope: document.getElementById('fader-tone-slope'),
        numToneSlope: document.getElementById('num-tone-slope'),
        faderCeiling: document.getElementById('fader-ceiling'),
        numCeiling: document.getElementById('num-ceiling'),

        // Segmented buttons
        segTargetProfile: document.getElementById('seg-target-profile'),
        segMbcSpeed: document.getElementById('seg-mbc-speed'),
        segBassLift: document.getElementById('seg-bass-lift'),
        segAirLift: document.getElementById('seg-air-lift'),
        segHpf: document.getElementById('seg-hpf'),
        segFreeze: document.getElementById('seg-freeze')
    };

    // Initialize application
    function init() {
        initCanvas();
        setupEventListeners();
        fetchState();
        connectEventSource();
        requestAnimationFrame(renderLoop);
    }

    // Initialize Canvas
    function initCanvas() {
        canvas = document.getElementById('tone-canvas');
        if (!canvas) return;
        ctx = canvas.getContext('2d');

        // Handle high DPI displays
        const dpr = window.devicePixelRatio || 1;
        const rect = canvas.getBoundingClientRect();
        canvas.width = rect.width * dpr;
        canvas.height = rect.height * dpr;
        ctx.scale(dpr, dpr);

        window.addEventListener('resize', () => {
            const r = canvas.getBoundingClientRect();
            canvas.width = r.width * dpr;
            canvas.height = r.height * dpr;
            ctx.scale(dpr, dpr);
        });
    }

    // Connect to Server-Sent Events (SSE) telemetry stream
    function connectEventSource() {
        if (eventSource) {
            eventSource.close();
        }

        elements.connectionDot.className = 'pulse-dot';
        elements.connectionStatus.textContent = 'CONNECTING';

        eventSource = new EventSource('/api/stream');

        eventSource.onopen = function() {
            isConnected = true;
            elements.connectionDot.className = 'pulse-dot online';
            elements.connectionStatus.textContent = 'LIVE 30FPS';
        };

        eventSource.onmessage = function(e) {
            try {
                const data = JSON.parse(e.data);
                updateVisualState(data);
            } catch (err) {
                console.error('Failed to parse telemetry frame', err);
            }
        };

        eventSource.onerror = function() {
            isConnected = false;
            elements.connectionDot.className = 'pulse-dot';
            elements.connectionStatus.textContent = 'RECONNECTING';
            eventSource.close();
            clearTimeout(reconnectTimer);
            reconnectTimer = setTimeout(connectEventSource, 2000);
        };
    }

    // Update internal telemetry state
    function updateVisualState(d) {
        if (d.momentaryLufs !== undefined) visualState.momentaryLufs = d.momentaryLufs;
        if (d.integratedLufs !== undefined) visualState.integratedLufs = d.integratedLufs;
        if (d.appliedGainDb !== undefined) visualState.appliedGainDb = d.appliedGainDb;
        if (d.limiterGrDb !== undefined) visualState.limiterGrDb = d.limiterGrDb;
        if (d.peakL !== undefined) visualState.peakL = d.peakL;
        if (d.peakR !== undefined) visualState.peakR = d.peakR;
        if (d.isFrozen !== undefined) visualState.isFrozen = d.isFrozen;
        if (d.mbcGrDb && Array.isArray(d.mbcGrDb)) visualState.mbcGrDb = d.mbcGrDb;
        if (d.targetProfile !== undefined) visualState.targetProfile = d.targetProfile;
        if (d.bassLiftDb !== undefined) visualState.bassLiftDb = d.bassLiftDb;
        if (d.airLiftDb !== undefined) visualState.airLiftDb = d.airLiftDb;
        if (d.deviceInfo) elements.audioDeviceInfo.textContent = d.deviceInfo;

        updateMetersDom();
    }

    // Update DOM meters
    function updateMetersDom() {
        // LUFS values
        const mom = visualState.momentaryLufs;
        const integ = visualState.integratedLufs;

        elements.valMomentary.textContent = mom <= -60 ? '-∞' : mom.toFixed(1);
        elements.valIntegrated.textContent = integ <= -60 ? '-∞' : integ.toFixed(1);

        // Normalize LUFS (-36 to 0 LUFS)
        const momPct = Math.max(0, Math.min(100, ((mom + 36) / 36) * 100));
        const integPct = Math.max(0, Math.min(100, ((integ + 36) / 36) * 100));

        elements.meterMomentary.style.width = momPct + '%';
        elements.meterIntegrated.style.width = integPct + '%';

        elements.meterMomentary.classList.toggle('hot', mom > -9.0);
        elements.meterIntegrated.classList.toggle('hot', integ > -9.0);

        // Gain Rider (-18dB to +12dB, center 0 at 60%)
        const gain = visualState.appliedGainDb;
        elements.valRider.textContent = (gain >= 0 ? '+' : '') + gain.toFixed(1) + ' dB';
        
        // Zero at 60% of track (range -18dB to +12dB = 30dB span)
        const zeroPct = 60; 
        if (gain >= 0) {
            const width = Math.min(40, (gain / 12) * 40);
            elements.meterRider.style.left = zeroPct + '%';
            elements.meterRider.style.width = width + '%';
        } else {
            const width = Math.min(60, (Math.abs(gain) / 18) * 60);
            elements.meterRider.style.left = (zeroPct - width) + '%';
            elements.meterRider.style.width = width + '%';
        }

        // Limiter GR (0 to -6 dB)
        const gr = Math.abs(visualState.limiterGrDb);
        elements.valLimiterGr.textContent = (gr > 0.05 ? '-' : '') + gr.toFixed(1) + ' dB';
        const grPct = Math.min(100, (gr / 6.0) * 100);
        elements.meterLimiterGr.style.width = grPct + '%';

        // Peak Meters (-60dB to 0dB)
        const pkL = visualState.peakL;
        const pkR = visualState.peakR;
        const maxPeak = Math.max(pkL, pkR);
        elements.valPeak.textContent = maxPeak <= -60 ? '-∞ dBFS' : maxPeak.toFixed(1) + ' dBFS';

        const pkLPct = Math.max(0, Math.min(100, ((pkL + 48) / 48) * 100));
        const pkRPct = Math.max(0, Math.min(100, ((pkR + 48) / 48) * 100));
        elements.peakL.style.width = pkLPct + '%';
        elements.peakR.style.width = pkRPct + '%';

        elements.clipL.classList.toggle('active', pkL >= -0.1);
        elements.clipR.classList.toggle('active', pkR >= -0.1);

        // Breakdown Freeze Badge
        elements.freezeBadge.classList.toggle('active', visualState.isFrozen);
        elements.freezeBadge.textContent = visualState.isFrozen ? 'FREEZE ACTIVE' : 'FREEZE IDLE';

        // Enhancers
        elements.valBassLift.textContent = '+' + visualState.bassLiftDb.toFixed(1) + ' dB';
        elements.valAirLift.textContent = '+' + visualState.airLiftDb.toFixed(1) + ' dB';

        // Profile mode tag
        elements.activeProfileTag.textContent = visualState.targetProfile === 0 ? 'LINEAR (PINK)' : 'MODERN MIX';
    }

    // 60FPS Canvas Render Loop for Dynamic Tone Visualizer
    function renderLoop() {
        drawToneVisualizer();
        requestAnimationFrame(renderLoop);
    }

    function drawToneVisualizer() {
        if (!ctx || !canvas) return;

        const w = canvas.getBoundingClientRect().width;
        const h = canvas.getBoundingClientRect().height;

        ctx.clearRect(0, 0, w, h);

        // Draw background grid lines
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
        ctx.lineWidth = 1;
        for (let y = 0; y < h; y += h / 4) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
        }

        const bandWidth = w / 6;
        const mbcGr = visualState.mbcGrDb;

        // Draw per-band dynamic gain reduction bars
        for (let i = 0; i < 6; i++) {
            const gr = Math.abs(mbcGr[i] || 0);
            const x = i * bandWidth + 6;
            const barW = bandWidth - 12;

            // Gradient per band
            const grad = ctx.createLinearGradient(0, 0, 0, h);
            grad.addColorStop(0, 'rgba(0, 229, 255, 0.25)');
            grad.addColorStop(1, 'rgba(124, 77, 255, 0.05)');

            ctx.fillStyle = grad;
            ctx.fillRect(x, 0, barW, h);

            // Active compression bar hanging down from top
            if (gr > 0.1) {
                const grHeight = Math.min(h, (gr / 12) * h);
                const grGrad = ctx.createLinearGradient(0, 0, 0, grHeight);
                grGrad.addColorStop(0, '#ff1744');
                grGrad.addColorStop(1, '#ffab00');
                ctx.fillStyle = grGrad;
                ctx.fillRect(x, 0, barW, grHeight);

                // GR value text
                ctx.fillStyle = '#ffab00';
                ctx.font = '10px JetBrains Mono';
                ctx.textAlign = 'center';
                ctx.fillText('-' + gr.toFixed(1) + 'dB', x + barW / 2, Math.max(16, grHeight + 12));
            }
        }

        // Draw active target curve line
        ctx.beginPath();
        ctx.strokeStyle = visualState.targetProfile === 0 ? '#7c4dff' : '#00e5ff';
        ctx.lineWidth = 2.5;
        ctx.shadowColor = visualState.targetProfile === 0 ? 'rgba(124, 77, 255, 0.5)' : 'rgba(0, 229, 255, 0.5)';
        ctx.shadowBlur = 8;

        const modernOffsets = [1.5, -2.5, -0.5, 1.0, -2.0, 1.5]; // dB offsets
        const midY = h * 0.5;

        for (let i = 0; i < 6; i++) {
            const x = i * bandWidth + bandWidth / 2;
            let offset = (visualState.targetProfile === 1) ? modernOffsets[i] : 0;
            const y = midY - (offset * (h / 12));

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }

            // Draw glowing node
            ctx.fillStyle = '#fff';
            ctx.fillRect(x - 3, y - 3, 6, 6);
        }
        ctx.stroke();
        ctx.shadowBlur = 0; // reset
    }

    // Fetch initial parameters from backend
    async function fetchState() {
        try {
            const res = await fetch('/api/state');
            if (!res.ok) return;
            const state = await res.json();
            applyStateToUi(state);
        } catch (err) {
            console.warn('Initial state fetch error:', err);
        }
    }

    // Apply state JSON to UI controls
    function applyStateToUi(s) {
        if (s.target_lufs !== undefined) {
            elements.faderTargetLufs.value = s.target_lufs;
            elements.numTargetLufs.textContent = parseFloat(s.target_lufs).toFixed(1) + ' LUFS';
        }
        if (s.level_response !== undefined) {
            elements.faderLevelResponse.value = s.level_response;
            elements.numLevelResponse.textContent = parseFloat(s.level_response).toFixed(2);
        }
        if (s.tone_slope !== undefined) {
            elements.faderToneSlope.value = s.tone_slope;
            elements.numToneSlope.textContent = parseFloat(s.tone_slope).toFixed(1) + ' dB/oct';
        }
        if (s.ceiling_db !== undefined) {
            elements.faderCeiling.value = s.ceiling_db;
            elements.numCeiling.textContent = parseFloat(s.ceiling_db).toFixed(1) + ' dBFS';
        }

        // Segmented controls
        if (s.target_profile !== undefined) setActiveSegment(elements.segTargetProfile, s.target_profile);
        if (s.mbc_speed !== undefined) setActiveSegment(elements.segMbcSpeed, s.mbc_speed);
        if (s.sub_weight !== undefined) setActiveSegment(elements.segBassLift, s.sub_weight);
        if (s.air_exciter !== undefined) setActiveSegment(elements.segAirLift, s.air_exciter);
        if (s.hpf_freq !== undefined) setActiveSegment(elements.segHpf, s.hpf_freq > 25 ? 1 : 0);
        if (s.freeze_breakdowns !== undefined) setActiveSegment(elements.segFreeze, s.freeze_breakdowns ? 1 : 0);

        if (s.bypass !== undefined) {
            elements.btnBypass.setAttribute('data-active', s.bypass ? 'true' : 'false');
        }
    }

    function setActiveSegment(container, val) {
        if (!container) return;
        const buttons = container.querySelectorAll('.seg-btn');
        buttons.forEach(btn => {
            btn.classList.toggle('active', btn.getAttribute('data-value') == val);
        });
    }

    // Debounced Parameter Dispatch to Backend
    const debounceTimers = {};
    function setParam(paramId, value) {
        clearTimeout(debounceTimers[paramId]);
        debounceTimers[paramId] = setTimeout(() => {
            fetch('/api/param', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: paramId, value: parseFloat(value) })
            }).catch(err => console.error('Failed to set param', paramId, err));
        }, 15);
    }

    // Setup Event Listeners
    function setupEventListeners() {
        // Target LUFS
        elements.faderTargetLufs.addEventListener('input', (e) => {
            const val = parseFloat(e.target.value).toFixed(1);
            elements.numTargetLufs.textContent = val + ' LUFS';
            setParam('target_lufs', val);
        });

        // Level Response
        elements.faderLevelResponse.addEventListener('input', (e) => {
            const val = parseFloat(e.target.value);
            let desc = val < 0.3 ? ' (Slow)' : val > 0.8 ? ' (Fast)' : ' (Medium)';
            elements.numLevelResponse.textContent = val.toFixed(2) + desc;
            setParam('level_response', val);
        });

        // Tone Slope
        elements.faderToneSlope.addEventListener('input', (e) => {
            const val = parseFloat(e.target.value).toFixed(1);
            elements.numToneSlope.textContent = val + ' dB/oct';
            setParam('tone_slope', val);
        });

        // Safety Limiter Ceiling
        elements.faderCeiling.addEventListener('input', (e) => {
            const val = parseFloat(e.target.value).toFixed(1);
            elements.numCeiling.textContent = val + ' dBFS';
            setParam('ceiling_db', val);
        });

        // Segmented button click handlers
        setupSegmentedControl(elements.segTargetProfile, 'target_profile');
        setupSegmentedControl(elements.segMbcSpeed, 'mbc_speed');
        setupSegmentedControl(elements.segBassLift, 'sub_weight');
        setupSegmentedControl(elements.segAirLift, 'air_exciter');
        setupSegmentedControl(elements.segHpf, 'hpf_freq', (val) => val === '1' ? 30.0 : 20.0);
        setupSegmentedControl(elements.segFreeze, 'freeze_breakdowns', (val) => val === '1');

        // Reset Button
        elements.btnReset.addEventListener('click', () => {
            fetch('/api/reset', { method: 'POST' }).catch(err => console.error(err));
            elements.btnReset.style.transform = 'scale(0.9)';
            setTimeout(() => elements.btnReset.style.transform = '', 150);
        });

        // Bypass Button
        elements.btnBypass.addEventListener('click', () => {
            const current = elements.btnBypass.getAttribute('data-active') === 'true';
            const next = !current;
            elements.btnBypass.setAttribute('data-active', next ? 'true' : 'false');
            setParam('bypass', next ? 1 : 0);
        });
    }

    function setupSegmentedControl(container, paramId, valueTransform) {
        if (!container) return;
        container.addEventListener('click', (e) => {
            const btn = e.target.closest('.seg-btn');
            if (!btn) return;
            const val = btn.getAttribute('data-value');
            setActiveSegment(container, val);
            const finalVal = valueTransform ? valueTransform(val) : val;
            setParam(paramId, finalVal);
        });
    }

    // Start on DOM ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
