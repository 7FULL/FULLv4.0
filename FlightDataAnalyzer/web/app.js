// Global variables
let flightData = null;
let charts = {};
let currentFile = null;

// Initialize the application
document.addEventListener('DOMContentLoaded', () => {
    initializeDropZone();
    initializeFileInput();
    initializeChartControls();
});

// Drop Zone Initialization
function initializeDropZone() {
    const dropZone = document.getElementById('dropZone');

    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('drag-over');
    });

    dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('drag-over');
    });

    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('drag-over');

        const files = e.dataTransfer.files;
        if (files.length > 0) {
            handleFile(files[0]);
        }
    });
}

// File Input Initialization
function initializeFileInput() {
    const fileInput = document.getElementById('fileInput');
    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) {
            handleFile(e.target.files[0]);
        }
    });
}

// Chart Controls Initialization
function initializeChartControls() {
    const checkboxes = document.querySelectorAll('.checkbox-label input[type="checkbox"]');
    checkboxes.forEach(checkbox => {
        checkbox.addEventListener('change', updateChartVisibility);
    });
}

// Handle File Upload
function handleFile(file) {
    if (!file.name.endsWith('.csv')) {
        alert('Please upload a CSV file');
        return;
    }

    currentFile = file;
    document.getElementById('fileInfo').textContent = `Loading ${file.name}...`;

    const reader = new FileReader();
    reader.onload = (e) => {
        try {
            parseCSV(e.target.result);
            document.getElementById('fileInfo').textContent = `✓ ${file.name} loaded successfully`;
        } catch (error) {
            alert('Error parsing CSV file: ' + error.message);
            document.getElementById('fileInfo').textContent = '✗ Error loading file';
        }
    };
    reader.readAsText(file);
}

// Parse CSV Data
function parseCSV(csvText) {
    const lines = csvText.trim().split('\n');
    const headers = lines[0].split(',');

    flightData = {
        timestamps: [],
        accelX: [],
        accelY: [],
        accelZ: [],
        gyroX: [],
        gyroY: [],
        gyroZ: [],
        pressure: [],
        temperature: [],
        altitude: [],
        latitude: [],
        longitude: [],
        gpsAlt: [],
        state: [],
        pyro0: [],
        pyro1: [],
        pyro2: [],
        pyro3: []
    };

    // Parse data rows
    for (let i = 1; i < lines.length; i++) {
        const values = lines[i].split(',');
        if (values.length < headers.length) continue;

        flightData.timestamps.push(parseFloat(values[0]));
        flightData.accelX.push(parseFloat(values[1]));
        flightData.accelY.push(parseFloat(values[2]));
        flightData.accelZ.push(parseFloat(values[3]));
        flightData.gyroX.push(parseFloat(values[4]));
        flightData.gyroY.push(parseFloat(values[5]));
        flightData.gyroZ.push(parseFloat(values[6]));
        flightData.pressure.push(parseFloat(values[7]));
        flightData.temperature.push(parseFloat(values[8]));
        flightData.altitude.push(parseFloat(values[9]));
        flightData.latitude.push(parseFloat(values[10]));
        flightData.longitude.push(parseFloat(values[11]));
        flightData.gpsAlt.push(parseFloat(values[12]));
        flightData.state.push(values[13]);
        flightData.pyro0.push(parseInt(values[14]));
        flightData.pyro1.push(parseInt(values[15]));
        flightData.pyro2.push(parseInt(values[16]));
        flightData.pyro3.push(parseInt(values[17]));
    }

    // Process data
    processFlightData();
    displayStatistics();
    displayStateTimeline();
    displayFlightEvents();
    createCharts();

    // Show sections
    document.getElementById('statsSection').style.display = 'block';
    document.getElementById('controlsSection').style.display = 'block';
    document.getElementById('chartsSection').style.display = 'grid';
    document.getElementById('eventsSection').style.display = 'block';

    setTimeout(()=>{
        resetZoom()
    }, 1000)
}

// Process Flight Data
function processFlightData() {
    // Normalize timestamps to seconds
    const startTime = flightData.timestamps[0];
    flightData.timeSeconds = flightData.timestamps.map(t => (t - startTime) / 1000);

    // Calculate ground altitude (average of first 10 readings in SLEEP state)
    let sleepAltitudes = [];
    for (let i = 0; i < Math.min(20, flightData.altitude.length); i++) {
        if (flightData.state[i] === 'SLEEP') {
            sleepAltitudes.push(flightData.altitude[i]);
        }
    }
    flightData.groundAltitude = sleepAltitudes.length > 0
        ? sleepAltitudes.reduce((a, b) => a + b, 0) / sleepAltitudes.length
        : flightData.altitude[0];

    // Calculate altitude AGL (Above Ground Level)
    flightData.altitudeAGL = flightData.altitude.map(alt => alt - flightData.groundAltitude);

    // Calculate sample rate (Hz) - average frequency
    let timeDiffs = [];
    for (let i = 1; i < flightData.timestamps.length; i++) {
        timeDiffs.push(flightData.timestamps[i] - flightData.timestamps[i - 1]);
    }
    const avgTimeDiff = timeDiffs.reduce((a, b) => a + b, 0) / timeDiffs.length;
    flightData.sampleRateHz = 1000 / avgTimeDiff; // Convert ms to Hz

    // Calculate vertical velocity from acceleration integration
    // accelX is the vertical axis (along rocket body)
    flightData.verticalVelocity = [0]; // Start with zero velocity
    let velocity = 0;

    for (let i = 1; i < flightData.accelX.length; i++) {
        const dt = (flightData.timestamps[i] - flightData.timestamps[i - 1]) / 1000; // Convert ms to seconds
        const accel = flightData.accelX[i]; // Acceleration in G

        // Integrate acceleration to get velocity (subtract 1G for gravity)
        // Only integrate when not in SLEEP/ARMED (stationary)
        if (flightData.state[i] === 'SLEEP' || flightData.state[i] === 'ARMED') {
            velocity = 0;
        } else {
            velocity += (accel - 1.0) * 9.81 * dt; // Convert G to m/s²
        }

        flightData.verticalVelocity.push(velocity);
    }
}

// Display Statistics
function displayStatistics() {
    // Flight Duration
    const duration = flightData.timeSeconds[flightData.timeSeconds.length - 1];
    document.getElementById('statDuration').textContent = `${duration.toFixed(1)}s`;

    // Max Altitude AGL
    const maxAlt = Math.max(...flightData.altitudeAGL);
    document.getElementById('statMaxAlt').textContent = `${maxAlt.toFixed(1)}m`;

    // Max Acceleration
    const maxAccel = Math.max(...flightData.accelX);
    document.getElementById('statMaxAccel').textContent = `${maxAccel.toFixed(2)}G`;

    // Apogee Time
    const apogeeIndex = flightData.altitudeAGL.indexOf(maxAlt);
    const apogeeTime = flightData.timeSeconds[apogeeIndex];
    document.getElementById('statApogeeTime').textContent = `${apogeeTime.toFixed(1)}s`;

    // Max Descent Rate (calculated from vertical velocity)
    const maxDescent = Math.min(...flightData.verticalVelocity);
    document.getElementById('statMaxDescent').textContent = `${Math.abs(maxDescent).toFixed(1)} m/s`;

    // Data Points
    document.getElementById('statDataPoints').textContent = flightData.timestamps.length;

    // Sample Rate (Hz)
    document.getElementById('statSampleRate').textContent = `${flightData.sampleRateHz.toFixed(1)} Hz`;

    // Drift from Launch Point (horizontal distance using GPS)
    const drift = calculateDriftFromLaunch();
    if (drift !== null) {
        document.getElementById('statDrift').textContent = `${drift.toFixed(1)}m`;
    } else {
        document.getElementById('statDrift').textContent = 'No GPS';
    }
}

// Calculate horizontal drift from launch point using GPS coordinates
function calculateDriftFromLaunch() {
    // Find first valid GPS coordinate (launch point)
    let launchLat = null;
    let launchLon = null;
    for (let i = 0; i < flightData.latitude.length; i++) {
        if (flightData.latitude[i] !== 0 && flightData.longitude[i] !== 0) {
            launchLat = flightData.latitude[i];
            launchLon = flightData.longitude[i];
            break;
        }
    }

    // Find last valid GPS coordinate (landing point)
    let landingLat = null;
    let landingLon = null;
    for (let i = flightData.latitude.length - 1; i >= 0; i--) {
        if (flightData.latitude[i] !== 0 && flightData.longitude[i] !== 0) {
            landingLat = flightData.latitude[i];
            landingLon = flightData.longitude[i];
            break;
        }
    }

    // If no valid GPS data, return null
    if (launchLat === null || landingLat === null) {
        return null;
    }

    // Haversine formula to calculate distance between two GPS coordinates
    const R = 6371000; // Earth's radius in meters
    const φ1 = launchLat * Math.PI / 180;
    const φ2 = landingLat * Math.PI / 180;
    const Δφ = (landingLat - launchLat) * Math.PI / 180;
    const Δλ = (landingLon - launchLon) * Math.PI / 180;

    const a = Math.sin(Δφ/2) * Math.sin(Δφ/2) +
              Math.cos(φ1) * Math.cos(φ2) *
              Math.sin(Δλ/2) * Math.sin(Δλ/2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));

    const distance = R * c; // Distance in meters

    return distance;
}

// Display State Timeline
function displayStateTimeline() {
    const timelineContent = document.getElementById('timelineContent');
    timelineContent.innerHTML = '';

    let currentState = null;
    let stateStartTime = 0;

    flightData.state.forEach((state, index) => {
        if (state !== currentState) {
            if (currentState !== null) {
                // Create timeline item for previous state
                const duration = flightData.timeSeconds[index] - stateStartTime;
                const item = document.createElement('div');
                item.className = `timeline-item ${currentState.toLowerCase()}`;
                item.innerHTML = `
                    <strong>${currentState}</strong><br>
                    ${stateStartTime.toFixed(1)}s - ${flightData.timeSeconds[index].toFixed(1)}s
                    (${duration.toFixed(1)}s)
                `;
                timelineContent.appendChild(item);
            }
            currentState = state;
            stateStartTime = flightData.timeSeconds[index];
        }
    });

    // Add final state
    if (currentState !== null) {
        const lastIndex = flightData.state.length - 1;
        const duration = flightData.timeSeconds[lastIndex] - stateStartTime;
        const item = document.createElement('div');
        item.className = `timeline-item ${currentState.toLowerCase()}`;
        item.innerHTML = `
            <strong>${currentState}</strong><br>
            ${stateStartTime.toFixed(1)}s - ${flightData.timeSeconds[lastIndex].toFixed(1)}s
            (${duration.toFixed(1)}s)
        `;
        timelineContent.appendChild(item);
    }
}

// Display Flight Events
function displayFlightEvents() {
    const eventsContainer = document.getElementById('eventsContainer');
    eventsContainer.innerHTML = '';

    const events = [];

    // Track state changes
    let lastState = null;
    flightData.state.forEach((state, index) => {
        if (state !== lastState) {
            events.push({
                time: flightData.timeSeconds[index],
                type: 'info',
                description: `State changed to ${state}`
            });
            lastState = state;
        }
    });

    // Track pyro activations
    for (let i = 1; i < flightData.pyro0.length; i++) {
        if (flightData.pyro0[i] && !flightData.pyro0[i-1]) {
            events.push({
                time: flightData.timeSeconds[i],
                type: 'critical',
                description: 'Pyro Channel 0 activated (Drogue)'
            });
        }
        if (flightData.pyro1[i] && !flightData.pyro1[i-1]) {
            events.push({
                time: flightData.timeSeconds[i],
                type: 'critical',
                description: 'Pyro Channel 1 activated (Main)'
            });
        }
        if (flightData.pyro2[i] && !flightData.pyro2[i-1]) {
            events.push({
                time: flightData.timeSeconds[i],
                type: 'warning',
                description: 'Pyro Channel 2 activated (Separation)'
            });
        }
        if (flightData.pyro3[i] && !flightData.pyro3[i-1]) {
            events.push({
                time: flightData.timeSeconds[i],
                type: 'warning',
                description: 'Pyro Channel 3 activated (Backup)'
            });
        }
    }

    // Sort events by time
    events.sort((a, b) => a.time - b.time);

    // Display events
    events.forEach(event => {
        const item = document.createElement('div');
        item.className = `event-item ${event.type}`;
        item.innerHTML = `
            <div class="event-time">T+${event.time.toFixed(2)}s</div>
            <div class="event-description">${event.description}</div>
        `;
        eventsContainer.appendChild(item);
    });
}

// Create Charts
function createCharts() {
    createAltitudeChart();
    createAccelerationChart();
    createVelocityChart(); // Re-enabled - now calculated from acceleration integration
    createAtmosphericChart();
    createGyroChart();

    // Create GPS chart if data available
    const hasGPS = flightData.latitude.some(lat => lat !== 0);
    if (hasGPS) {
        document.getElementById('gpsContainer').style.display = 'block';
        createGPSChart();
    }
}

// Altitude Chart with State Overlay
function createAltitudeChart() {
    const ctx = document.getElementById('altitudeChart').getContext('2d');

    // Destroy existing chart if it exists
    if (charts.altitude) {
        charts.altitude.destroy();
    }

    // Create state background colors dataset
    const stateColors = {
        'SLEEP': 'rgba(100, 100, 100, 0.1)',
        'ARMED': 'rgba(245, 158, 11, 0.15)',  // Orange
        'BOOST': 'rgba(239, 68, 68, 0.15)',    // Red
        'COAST': 'rgba(16, 185, 129, 0.15)',   // Green
        'APOGEE': 'rgba(139, 92, 246, 0.15)',  // Purple
        'PARACHUTE': 'rgba(6, 182, 212, 0.15)', // Cyan
        'LANDED': 'rgba(34, 197, 94, 0.15)',   // Green
        'ERROR': 'rgba(239, 68, 68, 0.2)',     // Red
        'ABORT': 'rgba(239, 68, 68, 0.3)'      // Red
    };

    // Create state background bars
    const maxAlt = Math.max(...flightData.altitudeAGL);
    const stateBackgrounds = flightData.state.map((state, i) => ({
        x: flightData.timeSeconds[i],
        y: maxAlt * 1.1,
        state: state
    }));

    charts.altitude = new Chart(ctx, {
        type: 'line',
        data: {
            labels: flightData.timeSeconds,
            datasets: [
                {
                    label: 'Altitude AGL (m)',
                    data: flightData.altitudeAGL,
                    borderColor: 'rgb(59, 130, 246)',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: true,
                    tension: 0.4,
                    order: 1
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                },
                tooltip: {
                    callbacks: {
                        afterLabel: function(context) {
                            const index = context.dataIndex;
                            return 'State: ' + flightData.state[index];
                        }
                    }
                },
                zoom: {
                    zoom: {
                        wheel: {
                            enabled: true,
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',
                    },
                    pan: {
                        enabled: true,
                        mode: 'x',
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Time (seconds)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Altitude (m)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                }
            }
        },
        plugins: [{
            id: 'stateBackground',
            beforeDatasetsDraw: function(chart) {
                const ctx = chart.ctx;
                const chartArea = chart.chartArea;
                const meta = chart.getDatasetMeta(0);

                let currentState = null;
                let stateStartX = null;

                for (let i = 0; i < flightData.state.length; i++) {
                    const state = flightData.state[i];
                    const point = meta.data[i];

                    if (!point) continue;

                    if (state !== currentState) {
                        // Draw previous state region
                        if (currentState !== null && stateStartX !== null) {
                            ctx.fillStyle = stateColors[currentState] || 'rgba(100, 100, 100, 0.1)';
                            ctx.fillRect(
                                stateStartX,
                                chartArea.top,
                                point.x - stateStartX,
                                chartArea.bottom - chartArea.top
                            );
                        }
                        currentState = state;
                        stateStartX = point.x;
                    }
                }

                // Draw final state region
                if (currentState !== null && stateStartX !== null) {
                    const lastPoint = meta.data[meta.data.length - 1];
                    ctx.fillStyle = stateColors[currentState] || 'rgba(100, 100, 100, 0.1)';
                    ctx.fillRect(
                        stateStartX,
                        chartArea.top,
                        lastPoint.x - stateStartX,
                        chartArea.bottom - chartArea.top
                    );
                }
            }
        }]
    });
}

// Acceleration Chart
function createAccelerationChart() {
    const ctx = document.getElementById('accelChart').getContext('2d');

    if (charts.accel) {
        charts.accel.destroy();
    }

    charts.accel = new Chart(ctx, {
        type: 'line',
        data: {
            labels: flightData.timeSeconds,
            datasets: [
                {
                    label: 'Accel X (G)',
                    data: flightData.accelX,
                    borderColor: 'rgb(239, 68, 68)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                },
                {
                    label: 'Accel Y (G)',
                    data: flightData.accelY,
                    borderColor: 'rgb(34, 197, 94)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                },
                {
                    label: 'Accel Z (G)',
                    data: flightData.accelZ,
                    borderColor: 'rgb(59, 130, 246)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                },
                zoom: {
                    zoom: {
                        wheel: {
                            enabled: true,
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',
                    },
                    pan: {
                        enabled: true,
                        mode: 'x',
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Time (seconds)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Acceleration (G)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                }
            }
        }
    });
}

// Velocity Chart with State Overlay
function createVelocityChart() {
    const ctx = document.getElementById('velocityChart').getContext('2d');

    if (charts.velocity) {
        charts.velocity.destroy();
    }

    const stateColors = {
        'SLEEP': 'rgba(100, 100, 100, 0.1)',
        'ARMED': 'rgba(245, 158, 11, 0.15)',
        'BOOST': 'rgba(239, 68, 68, 0.15)',
        'COAST': 'rgba(16, 185, 129, 0.15)',
        'APOGEE': 'rgba(139, 92, 246, 0.15)',
        'PARACHUTE': 'rgba(6, 182, 212, 0.15)',
        'LANDED': 'rgba(34, 197, 94, 0.15)',
        'ERROR': 'rgba(239, 68, 68, 0.2)',
        'ABORT': 'rgba(239, 68, 68, 0.3)'
    };

    charts.velocity = new Chart(ctx, {
        type: 'line',
        data: {
            labels: flightData.timeSeconds,
            datasets: [{
                label: 'Vertical Velocity (m/s)',
                data: flightData.verticalVelocity,
                borderColor: 'rgb(139, 92, 246)',
                backgroundColor: 'rgba(139, 92, 246, 0.1)',
                borderWidth: 2,
                pointRadius: 0,
                fill: true,
                tension: 0.4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                },
                tooltip: {
                    callbacks: {
                        afterLabel: function(context) {
                            const index = context.dataIndex;
                            return 'State: ' + flightData.state[index];
                        }
                    }
                },
                zoom: {
                    zoom: {
                        wheel: {
                            enabled: true,
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',
                    },
                    pan: {
                        enabled: true,
                        mode: 'x',
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Time (seconds)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Velocity (m/s)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                }
            }
        },
        plugins: [{
            id: 'stateBackground',
            beforeDatasetsDraw: function(chart) {
                const ctx = chart.ctx;
                const chartArea = chart.chartArea;
                const meta = chart.getDatasetMeta(0);

                let currentState = null;
                let stateStartX = null;

                for (let i = 0; i < flightData.state.length; i++) {
                    const state = flightData.state[i];
                    const point = meta.data[i];

                    if (!point) continue;

                    if (state !== currentState) {
                        if (currentState !== null && stateStartX !== null) {
                            ctx.fillStyle = stateColors[currentState] || 'rgba(100, 100, 100, 0.1)';
                            ctx.fillRect(
                                stateStartX,
                                chartArea.top,
                                point.x - stateStartX,
                                chartArea.bottom - chartArea.top
                            );
                        }
                        currentState = state;
                        stateStartX = point.x;
                    }
                }

                if (currentState !== null && stateStartX !== null) {
                    const lastPoint = meta.data[meta.data.length - 1];
                    ctx.fillStyle = stateColors[currentState] || 'rgba(100, 100, 100, 0.1)';
                    ctx.fillRect(
                        stateStartX,
                        chartArea.top,
                        lastPoint.x - stateStartX,
                        chartArea.bottom - chartArea.top
                    );
                }
            }
        }]
    });
}

// Atmospheric Chart
function createAtmosphericChart() {
    const ctx = document.getElementById('atmosChart').getContext('2d');

    if (charts.atmos) {
        charts.atmos.destroy();
    }

    charts.atmos = new Chart(ctx, {
        type: 'line',
        data: {
            labels: flightData.timeSeconds,
            datasets: [
                {
                    label: 'Pressure (mbar)',
                    data: flightData.pressure,
                    borderColor: 'rgb(6, 182, 212)',
                    borderWidth: 2,
                    pointRadius: 0,
                    yAxisID: 'y',
                    tension: 0.4
                },
                {
                    label: 'Temperature (°C)',
                    data: flightData.temperature,
                    borderColor: 'rgb(245, 158, 11)',
                    borderWidth: 2,
                    pointRadius: 0,
                    yAxisID: 'y1',
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                },
                zoom: {
                    zoom: {
                        wheel: {
                            enabled: true,
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',
                    },
                    pan: {
                        enabled: true,
                        mode: 'x',
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Time (seconds)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Pressure (mbar)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Temperature (°C)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        drawOnChartArea: false,
                    }
                }
            }
        }
    });
}

// Gyro Chart
function createGyroChart() {
    const ctx = document.getElementById('gyroChart').getContext('2d');

    if (charts.gyro) {
        charts.gyro.destroy();
    }

    charts.gyro = new Chart(ctx, {
        type: 'line',
        data: {
            labels: flightData.timeSeconds,
            datasets: [
                {
                    label: 'Gyro X (°/s)',
                    data: flightData.gyroX,
                    borderColor: 'rgb(239, 68, 68)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                },
                {
                    label: 'Gyro Y (°/s)',
                    data: flightData.gyroY,
                    borderColor: 'rgb(34, 197, 94)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                },
                {
                    label: 'Gyro Z (°/s)',
                    data: flightData.gyroZ,
                    borderColor: 'rgb(59, 130, 246)',
                    borderWidth: 2,
                    pointRadius: 0,
                    tension: 0.4
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                },
                zoom: {
                    zoom: {
                        wheel: {
                            enabled: true,
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',
                    },
                    pan: {
                        enabled: true,
                        mode: 'x',
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Time (seconds)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Angular Velocity (°/s)',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                }
            }
        }
    });
}

// GPS Chart
function createGPSChart() {
    const ctx = document.getElementById('gpsChart').getContext('2d');

    if (charts.gps) {
        charts.gps.destroy();
    }

    // Filter out zero coordinates
    const validCoords = flightData.latitude.map((lat, i) => ({
        x: flightData.longitude[i],
        y: lat
    })).filter(coord => coord.x !== 0 && coord.y !== 0);

    charts.gps = new Chart(ctx, {
        type: 'scatter',
        data: {
            datasets: [{
                label: 'GPS Track',
                data: validCoords,
                borderColor: 'rgb(16, 185, 129)',
                backgroundColor: 'rgba(16, 185, 129, 0.5)',
                pointRadius: 4,
                showLine: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    labels: {
                        color: '#f1f5f9'
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Longitude',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Latitude',
                        color: '#f1f5f9'
                    },
                    ticks: {
                        color: '#94a3b8'
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.1)'
                    }
                }
            }
        }
    });
}

// Update Chart Visibility
function updateChartVisibility() {
    // Get checkbox states
    const showAccel = document.getElementById('showAccel').checked;
    const showAltitude = document.getElementById('showAltitude').checked;
    const showVelocity = document.getElementById('showVelocity').checked;
    const showPressure = document.getElementById('showPressure').checked;
    const showTemperature = document.getElementById('showTemperature').checked;
    const showGyro = document.getElementById('showGyro').checked;

    // Toggle chart containers by finding parent divs
    const altitudeContainer = document.getElementById('altitudeChart')?.closest('.chart-container');
    const accelContainer = document.getElementById('accelChart')?.closest('.chart-container');
    const velocityContainer = document.getElementById('velocityChart')?.closest('.chart-container');
    const atmosContainer = document.getElementById('atmosChart')?.closest('.chart-container');
    const gyroContainer = document.getElementById('gyroChart')?.closest('.chart-container');

    // Show/hide chart containers
    if (altitudeContainer) {
        altitudeContainer.style.display = showAltitude ? 'block' : 'none';
    }

    if (accelContainer) {
        accelContainer.style.display = showAccel ? 'block' : 'none';
    }

    if (velocityContainer) {
        velocityContainer.style.display = showVelocity ? 'block' : 'none';
    }

    if (gyroContainer) {
        gyroContainer.style.display = showGyro ? 'block' : 'none';
    }

    // For atmospheric chart, toggle dataset visibility
    // Pressure is dataset 0, Temperature is dataset 1
    if (charts.atmos) {
        charts.atmos.data.datasets[0].hidden = !showPressure;
        charts.atmos.data.datasets[1].hidden = !showTemperature;

        // Hide entire atmospheric chart if both pressure and temperature are unchecked
        if (atmosContainer) {
            atmosContainer.style.display = (showPressure || showTemperature) ? 'block' : 'none';
        }

        charts.atmos.update();
    }
}

// Reset Zoom
function resetZoom() {
    Object.values(charts).forEach(chart => {
        if (chart && chart.resetZoom) {
            chart.resetZoom();
        }
    });
}

// Export Data
function exportData() {
    if (!flightData) {
        alert('No data to export');
        return;
    }

    // Create a comprehensive report
    const report = {
        file: currentFile.name,
        statistics: {
            duration: flightData.timeSeconds[flightData.timeSeconds.length - 1],
            maxAltitude: Math.max(...flightData.altitudeAGL),
            maxAcceleration: Math.max(...flightData.accelX),
            dataPoints: flightData.timestamps.length
        },
        rawData: flightData
    };

    // Convert to JSON and download
    const dataStr = JSON.stringify(report, null, 2);
    const dataBlob = new Blob([dataStr], { type: 'application/json' });
    const url = URL.createObjectURL(dataBlob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `flight_report_${Date.now()}.json`;
    link.click();
    URL.revokeObjectURL(url);
}
