const ctx = document.getElementById('liveChart').getContext('2d');

const gradient = ctx.createLinearGradient(0, 0, 0, 300);
gradient.addColorStop(0, 'rgba(140, 122, 242, 0.5)');
gradient.addColorStop(1, 'rgba(140, 122, 242, 0.0)');

const chartConfig = {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Skin Conductance (µS)',
            data: [],
            borderColor: '#8c7af2',
            backgroundColor: gradient,
            borderWidth: 2,
            pointRadius: 0,
            fill: true,
            tension: 0.4
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            legend: { display: false },
            tooltip: {
                mode: 'index',
                intersect: false,
            }
        },
        scales: {
            x: {
                display: false, // hide x axis for a clean look
            },
            y: {
                display: true,
                title: {
                    display: true,
                    text: 'Conductance (µS)'
                },
                grid: {
                    color: '#edf2f7'
                }
            }
        },
        animation: {
            duration: 0
        }
    }
};

const liveChart = new Chart(ctx, chartConfig);

let baseline = 0.59;
const MAX_DATAPOINTS = 100;

async function fetchData() {
    try {
        const response = await fetch('/api/data');
        const data = await response.json();
        
        if (data.history && data.history.length > 0) {
            updateDashboard(data.history, data.inference);
        }
    } catch (error) {
        console.error("Error fetching data:", error);
        document.getElementById('nav-connection-status').textContent = 'Disconnected';
        document.getElementById('nav-connection-status').previousElementSibling.className = 'status-dot offline';
    }
}

function updateDashboard(history, inference) {
    const latest = history[history.length - 1];
    
    // Update Hero
    document.getElementById('hero-conductance').textContent = latest.conductance.toFixed(2);
    document.getElementById('hero-temp').textContent = latest.temperature.toFixed(1);
    document.getElementById('hero-humidity').textContent = latest.humidity.toFixed(0);

    // Update Connection Status
    if (latest.wifiConnected) {
        document.getElementById('nav-connection-status').textContent = 'Connected';
        document.getElementById('nav-connection-status').previousElementSibling.className = 'status-dot online';
    } else {
        document.getElementById('nav-connection-status').textContent = 'Offline Logging';
        document.getElementById('nav-connection-status').previousElementSibling.className = 'status-dot offline';
    }

    // Update Battery Status (Assuming 4.2 max, 3.3 min)
    let batteryPct = ((latest.batteryVoltage - 3.3) / (4.2 - 3.3)) * 100;
    batteryPct = Math.min(100, Math.max(0, batteryPct));
    document.getElementById('nav-battery-status').textContent = `${batteryPct.toFixed(0)}% - Active`;
    
    // Update Chart
    const recentHistory = history.slice(-MAX_DATAPOINTS);
    liveChart.data.labels = recentHistory.map(d => new Date(d.timestamp * 1000).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'}));
    liveChart.data.datasets[0].data = recentHistory.map(d => d.conductance);
    liveChart.update();

    // Update Inference state
    const stateEl = document.getElementById('live-state');
    const heroTrend = document.getElementById('hero-conductance-trend');
    
    if (inference) {
        stateEl.textContent = inference.state;
        // Add source info
        if (inference.source !== "Monitoring") {
            stateEl.textContent += ` (via ${inference.source})`;
        }

        if (inference.state.includes("Detected")) {
            stateEl.className = 'state-event';
            heroTrend.textContent = inference.source.includes("Temp") ? 'Temp Spike' : 'Rising Rapidly';
            heroTrend.className = 'trend text-red';
        } else {
            stateEl.className = 'state-stable';
            heroTrend.textContent = 'Stable';
            heroTrend.className = 'trend neutral';
        }
    }

    // Update rolling baseline
    baseline = recentHistory.reduce((acc, d) => acc + d.conductance, 0) / recentHistory.length;
    document.getElementById('live-baseline').textContent = `${baseline.toFixed(2)} µS`;

    document.getElementById('nav-last-sync').textContent = new Date().toLocaleTimeString();
}

// Calibration Form Handling
document.getElementById('calibration-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const offset = parseFloat(document.getElementById('cal-offset').value);
    const multiplier = parseFloat(document.getElementById('cal-multiplier').value);
    const statusEl = document.getElementById('cal-status');
    
    try {
        const response = await fetch('/api/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ offset, multiplier })
        });
        if (response.ok) {
            statusEl.textContent = 'Calibration saved successfully!';
            statusEl.style.color = '#48bb78';
            setTimeout(() => statusEl.textContent = '', 3000);
        }
    } catch (error) {
        statusEl.textContent = 'Error saving calibration.';
        statusEl.style.color = '#f56565';
    }
});

// Fetch initially
fetchData();
// Poll every 2 seconds
setInterval(fetchData, 2000);
