"""
Flight Statistics and Report Generation
Generates detailed statistics and HTML reports for rocket flight data
"""

import numpy as np
import pandas as pd
from pathlib import Path
from datetime import datetime


class FlightStatistics:
    """Calculates detailed flight statistics and generates reports"""

    def __init__(self, dataframe, flight_info):
        """Initialize statistics generator with flight data"""
        self.df = dataframe
        self.info = flight_info
        self.stats = {}

    def calculate_all_statistics(self):
        """Calculate comprehensive flight statistics"""

        # === ALTITUDE STATISTICS ===
        self.stats['altitude'] = {
            'max_msl': float(self.df['Altitude'].max()),
            'min_msl': float(self.df['Altitude'].min()),
            'ground_msl': float(self.df['Altitude'].iloc[0]),
            'apogee_agl': float(self.info['apogee_agl']),
            'apogee_time': float(self.info['apogee_time']),
            'avg_ascent_rate': self._calculate_avg_ascent_rate(),
            'max_ascent_rate': self._calculate_max_ascent_rate(),
            'avg_descent_rate': self._calculate_avg_descent_rate(),
            'max_descent_rate': self._calculate_max_descent_rate()
        }

        # === ACCELERATION STATISTICS ===
        self.stats['acceleration'] = {
            'max_total': float(self.df['Accel_Total'].max()),
            'max_x': float(self.df['AccelX'].max()),
            'min_x': float(self.df['AccelX'].min()),
            'max_y': float(self.df['AccelY'].max()),
            'min_y': float(self.df['AccelY'].min()),
            'max_z': float(self.df['AccelZ'].max()),
            'min_z': float(self.df['AccelZ'].min()),
            'avg_boost_accel': self._calculate_avg_boost_accel(),
            'boost_duration': self._calculate_boost_duration()
        }

        # === VELOCITY STATISTICS ===
        self.stats['velocity'] = {
            'max_velocity': float(self.df['Velocity'].max()),
            'max_velocity_time': float(self.df.loc[self.df['Velocity'].idxmax(), 'Time_sec']),
            'burnout_velocity': self._estimate_burnout_velocity()
        }

        # === ENVIRONMENTAL STATISTICS ===
        self.stats['environment'] = {
            'min_pressure': float(self.df['Pressure'].min()),
            'max_pressure': float(self.df['Pressure'].max()),
            'min_temperature': float(self.df['Temperature'].min()),
            'max_temperature': float(self.df['Temperature'].max()),
            'avg_temperature': float(self.df['Temperature'].mean())
        }

        # === FLIGHT PHASE DURATIONS ===
        self.stats['phases'] = self._calculate_phase_durations()

        # === GPS STATISTICS ===
        if self.df['Latitude'].notna().any():
            self.stats['gps'] = self._calculate_gps_statistics()
        else:
            self.stats['gps'] = None

        # === PYRO STATISTICS ===
        self.stats['pyro'] = {
            'total_events': len(self.info['pyro_events']),
            'events': self.info['pyro_events']
        }

        # === DATA QUALITY ===
        self.stats['data_quality'] = {
            'total_samples': len(self.df),
            'sample_rate_hz': float(self.info['sample_rate']),
            'duration_seconds': float(self.info['duration']),
            'accel_valid_pct': float((self.df['AccelX'].notna().sum() / len(self.df)) * 100),
            'baro_valid_pct': float((self.df['Pressure'].notna().sum() / len(self.df)) * 100),
            'gps_valid_pct': float((self.df['Latitude'].notna().sum() / len(self.df)) * 100)
        }

        return self.stats

    def _calculate_avg_ascent_rate(self):
        """Calculate average ascent rate (m/s)"""
        ascent_data = self.df[self.df['Time_sec'] <= self.info['apogee_time']]
        if len(ascent_data) < 2:
            return 0.0

        altitude_gain = ascent_data['Altitude'].iloc[-1] - ascent_data['Altitude'].iloc[0]
        time_delta = ascent_data['Time_sec'].iloc[-1] - ascent_data['Time_sec'].iloc[0]

        return altitude_gain / time_delta if time_delta > 0 else 0.0

    def _calculate_max_ascent_rate(self):
        """Calculate maximum ascent rate (m/s)"""
        ascent_data = self.df[self.df['Time_sec'] <= self.info['apogee_time']]
        if len(ascent_data) < 2:
            return 0.0

        # Calculate rate of change of altitude
        ascent_data = ascent_data.copy()
        ascent_data['altitude_rate'] = ascent_data['Altitude'].diff() / ascent_data['Time_sec'].diff()

        return float(ascent_data['altitude_rate'].max())

    def _calculate_avg_descent_rate(self):
        """Calculate average descent rate (m/s)"""
        descent_data = self.df[self.df['Time_sec'] >= self.info['apogee_time']]
        if len(descent_data) < 2:
            return 0.0

        altitude_loss = descent_data['Altitude'].iloc[0] - descent_data['Altitude'].iloc[-1]
        time_delta = descent_data['Time_sec'].iloc[-1] - descent_data['Time_sec'].iloc[0]

        return altitude_loss / time_delta if time_delta > 0 else 0.0

    def _calculate_max_descent_rate(self):
        """Calculate maximum descent rate (m/s)"""
        descent_data = self.df[self.df['Time_sec'] >= self.info['apogee_time']]
        if len(descent_data) < 2:
            return 0.0

        # Calculate rate of change of altitude (absolute value)
        descent_data = descent_data.copy()
        descent_data['altitude_rate'] = descent_data['Altitude'].diff() / descent_data['Time_sec'].diff()

        return abs(float(descent_data['altitude_rate'].min()))

    def _calculate_avg_boost_accel(self):
        """Calculate average acceleration during boost phase"""
        # Find boost phase
        boost_data = self.df[self.df['State_Name'] == 'BOOST']

        if boost_data.empty:
            # If no BOOST state, estimate from high acceleration period
            boost_data = self.df[self.df['Accel_Total'] > 2.0]  # >2G

        if boost_data.empty:
            return 0.0

        return float(boost_data['Accel_Total'].mean())

    def _calculate_boost_duration(self):
        """Calculate duration of boost phase (seconds)"""
        boost_data = self.df[self.df['State_Name'] == 'BOOST']

        if boost_data.empty:
            return 0.0

        return float(boost_data['Time_sec'].iloc[-1] - boost_data['Time_sec'].iloc[0])

    def _estimate_burnout_velocity(self):
        """Estimate velocity at motor burnout"""
        boost_data = self.df[self.df['State_Name'] == 'BOOST']

        if boost_data.empty:
            return 0.0

        # Return velocity at end of boost phase
        return float(boost_data['Velocity'].iloc[-1])

    def _calculate_phase_durations(self):
        """Calculate duration of each flight phase"""
        phases = {}

        state_changes = self.df[self.df['State'] != "Sleep"]

        for i, (idx, row) in enumerate(state_changes.iterrows()):
            state_name = row['State_Name']
            start_time = row['Time_sec']

            # Find next state change or end of flight
            if i < len(state_changes) - 1:
                next_idx = state_changes.index[i + 1]
                end_time = self.df.loc[next_idx, 'Time_sec']
            else:
                end_time = self.df['Time_sec'].iloc[-1]

            duration = end_time - start_time
            phases[state_name] = {
                'start_time': float(start_time),
                'duration': float(duration)
            }

        return phases

    def _calculate_gps_statistics(self):
        """Calculate GPS-based statistics"""
        gps_data = self.df[self.df['Latitude'].notna() & self.df['Longitude'].notna()]

        if gps_data.empty:
            return None

        # Calculate horizontal distance traveled
        lat1 = gps_data['Latitude'].iloc[0]
        lon1 = gps_data['Longitude'].iloc[0]
        lat2 = gps_data['Latitude'].iloc[-1]
        lon2 = gps_data['Longitude'].iloc[-1]

        # Haversine formula for distance
        R = 6371000  # Earth radius in meters
        phi1 = np.radians(lat1)
        phi2 = np.radians(lat2)
        delta_phi = np.radians(lat2 - lat1)
        delta_lambda = np.radians(lon2 - lon1)

        a = np.sin(delta_phi/2)**2 + np.cos(phi1) * np.cos(phi2) * np.sin(delta_lambda/2)**2
        c = 2 * np.arctan2(np.sqrt(a), np.sqrt(1-a))
        distance = R * c

        return {
            'launch_lat': float(lat1),
            'launch_lon': float(lon1),
            'landing_lat': float(lat2),
            'landing_lon': float(lon2),
            'horizontal_distance': float(distance),
            'max_gps_altitude': float(gps_data['GPS_Alt'].max()),
            'gps_samples': len(gps_data)
        }

    def generate_html_report(self, output_dir):
        """Generate comprehensive HTML report"""
        output_dir = Path(output_dir)
        output_dir.mkdir(exist_ok=True)

        # Calculate all statistics
        self.calculate_all_statistics()

        # Generate HTML
        html = self._generate_html_content()

        # Save to file
        # timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = output_dir / f'flight_report.html'

        with open(report_path, 'w', encoding='utf-8') as f:
            f.write(html)

        return report_path

    def _generate_html_content(self):
        """Generate HTML content for report"""
        html = f"""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Rocket Flight Analysis Report</title>
    <style>
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background-color: white;
            padding: 30px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
            border-radius: 8px;
        }}
        h1 {{
            color: #2c3e50;
            border-bottom: 3px solid #3498db;
            padding-bottom: 10px;
        }}
        h2 {{
            color: #34495e;
            margin-top: 30px;
            border-bottom: 2px solid #95a5a6;
            padding-bottom: 5px;
        }}
        .stat-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin: 20px 0;
        }}
        .stat-card {{
            background-color: #ecf0f1;
            padding: 15px;
            border-radius: 5px;
            border-left: 4px solid #3498db;
        }}
        .stat-label {{
            font-weight: bold;
            color: #7f8c8d;
            font-size: 0.9em;
        }}
        .stat-value {{
            font-size: 1.5em;
            color: #2c3e50;
            margin-top: 5px;
        }}
        .phase-table {{
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
        }}
        .phase-table th, .phase-table td {{
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }}
        .phase-table th {{
            background-color: #3498db;
            color: white;
        }}
        .phase-table tr:hover {{
            background-color: #f5f5f5;
        }}
        .warning {{
            background-color: #fff3cd;
            border-left: 4px solid #ffc107;
            padding: 15px;
            margin: 20px 0;
            border-radius: 5px;
        }}
        .success {{
            background-color: #d4edda;
            border-left: 4px solid #28a745;
            padding: 15px;
            margin: 20px 0;
            border-radius: 5px;
        }}
        .footer {{
            margin-top: 30px;
            padding-top: 20px;
            border-top: 1px solid #ddd;
            text-align: center;
            color: #7f8c8d;
            font-size: 0.9em;
        }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 Rocket Flight Analysis Report</h1>
        <p><strong>Generated:</strong> {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
        <p><strong>Flight Duration:</strong> {self.info['duration']:.2f} seconds</p>

        <h2>Altitude Performance</h2>
        <div class="stat-grid">
            <div class="stat-card">
                <div class="stat-label">Apogee (AGL)</div>
                <div class="stat-value">{self.stats['altitude']['apogee_agl']:.1f} m</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Apogee (MSL)</div>
                <div class="stat-value">{self.stats['altitude']['max_msl']:.1f} m</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Apogee Time</div>
                <div class="stat-value">{self.stats['altitude']['apogee_time']:.2f} s</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Avg Ascent Rate</div>
                <div class="stat-value">{self.stats['altitude']['avg_ascent_rate']:.1f} m/s</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Max Ascent Rate</div>
                <div class="stat-value">{self.stats['altitude']['max_ascent_rate']:.1f} m/s</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Avg Descent Rate</div>
                <div class="stat-value">{self.stats['altitude']['avg_descent_rate']:.1f} m/s</div>
            </div>
        </div>

        <h2>Acceleration & Velocity</h2>
        <div class="stat-grid">
            <div class="stat-card">
                <div class="stat-label">Max Total Acceleration</div>
                <div class="stat-value">{self.stats['acceleration']['max_total']:.2f} G</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Avg Boost Acceleration</div>
                <div class="stat-value">{self.stats['acceleration']['avg_boost_accel']:.2f} G</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Boost Duration</div>
                <div class="stat-value">{self.stats['acceleration']['boost_duration']:.2f} s</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Max Velocity</div>
                <div class="stat-value">{self.stats['velocity']['max_velocity']:.1f} m/s</div>
            </div>
        </div>

        <h2>Flight Phase Timeline</h2>
        <table class="phase-table">
            <thead>
                <tr>
                    <th>Phase</th>
                    <th>Start Time (s)</th>
                    <th>Duration (s)</th>
                </tr>
            </thead>
            <tbody>
"""

        # Add phase rows
        for phase_name, phase_data in self.stats['phases'].items():
            html += f"""
                <tr>
                    <td><strong>{phase_name}</strong></td>
                    <td>{phase_data['start_time']:.2f}</td>
                    <td>{phase_data['duration']:.2f}</td>
                </tr>
"""

        html += """
            </tbody>
        </table>

        <h2>Pyrotechnic Events</h2>
"""

        if self.stats['pyro']['total_events'] > 0:
            html += '<table class="phase-table"><thead><tr><th>Channel</th><th>Time (s)</th><th>State</th></tr></thead><tbody>'
            for event in self.stats['pyro']['events']:
                html += f"<tr><td>Channel {event['channel']}</td><td>{event['time']:.2f}</td><td>{event['state']}</td></tr>"
            html += '</tbody></table>'
        else:
            html += '<div class="warning">⚠️ No pyrotechnic events recorded</div>'

        # GPS section
        if self.stats['gps']:
            html += f"""
        <h2>GPS Data</h2>
        <div class="stat-grid">
            <div class="stat-card">
                <div class="stat-label">Horizontal Distance</div>
                <div class="stat-value">{self.stats['gps']['horizontal_distance']:.1f} m</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Launch Position</div>
                <div class="stat-value">{self.stats['gps']['launch_lat']:.6f}, {self.stats['gps']['launch_lon']:.6f}</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Landing Position</div>
                <div class="stat-value">{self.stats['gps']['landing_lat']:.6f}, {self.stats['gps']['landing_lon']:.6f}</div>
            </div>
        </div>
"""
        else:
            html += '<div class="warning">⚠️ No GPS data available in this flight</div>'

        # Data quality
        html += f"""
        <h2>Data Quality</h2>
        <div class="stat-grid">
            <div class="stat-card">
                <div class="stat-label">Total Samples</div>
                <div class="stat-value">{self.stats['data_quality']['total_samples']}</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Sample Rate</div>
                <div class="stat-value">{self.stats['data_quality']['sample_rate_hz']:.1f} Hz</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Accelerometer Valid</div>
                <div class="stat-value">{self.stats['data_quality']['accel_valid_pct']:.1f}%</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Barometer Valid</div>
                <div class="stat-value">{self.stats['data_quality']['baro_valid_pct']:.1f}%</div>
            </div>
        </div>

        <div class="success">
            ✅ Flight analysis complete. All data processed successfully.
        </div>

        <div class="footer">
            <p>Rocket Flight Computer Data Analysis Tool v1.0</p>
            <p>Generated with Python • Pandas • Matplotlib</p>
        </div>
    </div>
</body>
</html>
"""

        return html
