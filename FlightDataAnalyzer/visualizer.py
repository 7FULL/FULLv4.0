"""
Flight Data Visualizer
Generates comprehensive plots and visualizations of rocket flight data
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
from pathlib import Path


class FlightVisualizer:
    """Handles all visualization and plotting for flight data"""

    # Color scheme for plots
    COLORS = {
        'altitude': '#1f77b4',
        'velocity': '#ff7f0e',
        'accel': '#2ca02c',
        'pressure': '#d62728',
        'temp': '#9467bd',
        'pyro': '#e377c2'
    }

    # State colors for background shading
    STATE_COLORS = {
        'SLEEP': '#E0E0E0',
        'ARMED': '#FFF9C4',
        'BOOST': '#FFCCBC',
        'COAST': '#C5E1A5',
        'APOGEE': '#B2DFDB',
        'PARACHUTE': '#BBDEFB',
        'LANDED': '#D1C4E9',
        'ERROR': '#FFCDD2',
        'ABORT': '#FFAB91'
    }

    def __init__(self, dataframe, flight_info):
        """Initialize visualizer with flight data"""
        self.df = dataframe
        self.info = flight_info

        # Set plotting style
        plt.style.use('seaborn-v0_8-darkgrid')
        plt.rcParams['figure.figsize'] = (12, 8)
        plt.rcParams['font.size'] = 10

    def add_state_backgrounds(self, ax):
        """Add colored background regions for each flight state"""
        state_changes = self.df[self.df['State'] != "Sleep"]

        for i, (idx, row) in enumerate(state_changes.iterrows()):
            start_time = row['Time_sec']

            # Find next state change or end of flight
            if i < len(state_changes) - 1:
                next_idx = state_changes.index[i + 1]
                end_time = self.df.loc[next_idx, 'Time_sec']
            else:
                end_time = self.df['Time_sec'].iloc[-1]

            state_name = row['State_Name']
            color = self.STATE_COLORS.get(state_name, '#FFFFFF')

            ax.axvspan(start_time, end_time, alpha=0.2, color=color, zorder=0)

    def add_pyro_markers(self, ax):
        """Add vertical lines for pyrotechnic events"""
        for event in self.info['pyro_events']:
            ax.axvline(event['time'], color=self.COLORS['pyro'],
                      linestyle='--', linewidth=1.5, alpha=0.7,
                      label=f"Pyro Ch{event['channel']}")

    def plot_altitude_profile(self, output_dir):
        """Generate altitude vs time plot"""
        fig, ax = plt.subplots(figsize=(14, 8))

        # Add state backgrounds
        self.add_state_backgrounds(ax)

        # Plot altitude
        ax.plot(self.df['Time_sec'], self.df['Altitude'],
               color=self.COLORS['altitude'], linewidth=2, label='Barometric Altitude')

        # Plot GPS altitude if available and not 0
        if self.df['GPS_Alt'].notna().any() and (self.df['GPS_Alt'] != 0).any():
            ax.plot(self.df['Time_sec'], self.df['GPS_Alt'],
                   color='orange', linewidth=1.5, linestyle='--', label='GPS Altitude', alpha=0.7)

        # Mark apogee
        apogee_idx = self.df['Altitude'].idxmax()
        apogee_time = self.df.loc[apogee_idx, 'Time_sec']
        apogee_alt = self.df.loc[apogee_idx, 'Altitude']

        ax.plot(apogee_time, apogee_alt, 'r*', markersize=20, label=f'Apogee: {apogee_alt:.1f}m')

        # Add pyro markers
        self.add_pyro_markers(ax)

        ax.set_xlabel('Time (seconds)', fontsize=12)
        ax.set_ylabel('Altitude (meters MSL)', fontsize=12)
        ax.set_title('Flight Altitude Profile', fontsize=14, fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)

        plt.tight_layout()
        output_path = output_dir / 'altitude_profile.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def plot_acceleration(self, output_dir):
        """Generate acceleration plots"""
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10))

        # === Plot 1: 3-axis acceleration ===
        self.add_state_backgrounds(ax1)

        ax1.plot(self.df['Time_sec'], self.df['AccelX'],
                label='Accel X (vertical)', linewidth=1.5, alpha=0.8)
        ax1.plot(self.df['Time_sec'], self.df['AccelY'],
                label='Accel Y', linewidth=1.5, alpha=0.8)
        ax1.plot(self.df['Time_sec'], self.df['AccelZ'],
                label='Accel Z', linewidth=2, color=self.COLORS['accel'])

        self.add_pyro_markers(ax1)

        ax1.set_xlabel('Time (seconds)', fontsize=12)
        ax1.set_ylabel('Acceleration (G)', fontsize=12)
        ax1.set_title('3-Axis Acceleration', fontsize=14, fontweight='bold')
        ax1.legend(loc='best')
        ax1.grid(True, alpha=0.3)
        ax1.axhline(0, color='black', linewidth=0.8, linestyle='-')

        # === Plot 2: Total acceleration magnitude ===
        self.add_state_backgrounds(ax2)

        ax2.plot(self.df['Time_sec'], self.df['Accel_Total'],
                color=self.COLORS['accel'], linewidth=2, label='Total Acceleration')

        # Mark maximum acceleration
        max_accel_idx = self.df['Accel_Total'].idxmax()
        max_accel_time = self.df.loc[max_accel_idx, 'Time_sec']
        max_accel = self.df.loc[max_accel_idx, 'Accel_Total']

        ax2.plot(max_accel_time, max_accel, 'r*', markersize=20,
                label=f'Max: {max_accel:.2f}G @ {max_accel_time:.2f}s')

        self.add_pyro_markers(ax2)

        ax2.set_xlabel('Time (seconds)', fontsize=12)
        ax2.set_ylabel('Total Acceleration (G)', fontsize=12)
        ax2.set_title('Total Acceleration Magnitude', fontsize=14, fontweight='bold')
        ax2.legend(loc='best')
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        output_path = output_dir / 'acceleration.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def plot_pressure_temperature(self, output_dir):
        """Generate pressure and temperature plots"""
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10))

        # === Plot 1: Pressure ===
        self.add_state_backgrounds(ax1)

        ax1.plot(self.df['Time_sec'], self.df['Pressure'] / 100,  # Convert to hPa
                color=self.COLORS['pressure'], linewidth=2, label='Barometric Pressure')

        self.add_pyro_markers(ax1)

        ax1.set_xlabel('Time (seconds)', fontsize=12)
        ax1.set_ylabel('Pressure (hPa)', fontsize=12)
        ax1.set_title('Barometric Pressure', fontsize=14, fontweight='bold')
        ax1.legend(loc='best')
        ax1.grid(True, alpha=0.3)

        # === Plot 2: Temperature ===
        self.add_state_backgrounds(ax2)

        ax2.plot(self.df['Time_sec'], self.df['Temperature'],
                color=self.COLORS['temp'], linewidth=2, label='Temperature')

        self.add_pyro_markers(ax2)

        ax2.set_xlabel('Time (seconds)', fontsize=12)
        ax2.set_ylabel('Temperature (°C)', fontsize=12)
        ax2.set_title('Temperature Profile', fontsize=14, fontweight='bold')
        ax2.legend(loc='best')
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        output_path = output_dir / 'pressure_temperature.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def plot_state_timeline(self, output_dir):
        """Generate state timeline visualization"""
        fig, ax = plt.subplots(figsize=(14, 6))

        # Create state timeline
        state_changes = self.df[self.df['State'] != "Sleep"]

        for i, (idx, row) in enumerate(state_changes.iterrows()):
            start_time = row['Time_sec']

            # Find next state change or end of flight
            if i < len(state_changes) - 1:
                next_idx = state_changes.index[i + 1]
                end_time = self.df.loc[next_idx, 'Time_sec']
            else:
                end_time = self.df['Time_sec'].iloc[-1]

            state_name = row['State_Name']
            color = self.STATE_COLORS.get(state_name, '#FFFFFF')
            duration = end_time - start_time

            # Draw state bar
            ax.barh(0, duration, left=start_time, height=0.5,
                   color=color, edgecolor='black', linewidth=1.5)

            # Add state label if duration is significant
            if duration > (self.df['Time_sec'].iloc[-1] * 0.05):  # >5% of flight time
                ax.text(start_time + duration/2, 0, state_name,
                       ha='center', va='center', fontsize=10, fontweight='bold')

        # Add pyro event markers
        for event in self.info['pyro_events']:
            ax.plot(event['time'], 0, 'v', color=self.COLORS['pyro'],
                   markersize=15, label=f"Pyro Ch{event['channel']}")

        ax.set_xlabel('Time (seconds)', fontsize=12)
        ax.set_yticks([])
        ax.set_ylim(-0.5, 0.5)
        ax.set_xlim(0, self.df['Time_sec'].iloc[-1])
        ax.set_title('Flight State Timeline', fontsize=14, fontweight='bold')
        ax.grid(True, axis='x', alpha=0.3)

        # Remove duplicate labels in legend
        handles, labels = ax.get_legend_handles_labels()
        by_label = dict(zip(labels, handles))
        ax.legend(by_label.values(), by_label.keys(), loc='upper right')

        plt.tight_layout()
        output_path = output_dir / 'state_timeline.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def plot_gps_track(self, output_dir):
        """Generate GPS ground track plot if GPS data is available"""
        # Check if GPS data exists
        if not self.df['Latitude'].notna().any() or not self.df['Longitude'].notna().any():
            print("  ⚠ No GPS data available, skipping GPS track plot")
            return None

        fig, ax = plt.subplots(figsize=(10, 10))

        # Filter valid GPS points
        gps_data = self.df[self.df['Latitude'].notna() & self.df['Longitude'].notna()]

        # Plot GPS track
        scatter = ax.scatter(gps_data['Longitude'], gps_data['Latitude'],
                           c=gps_data['Altitude'], cmap='viridis',
                           s=20, alpha=0.7, edgecolors='black', linewidth=0.5)

        # Mark launch and landing
        ax.plot(gps_data['Longitude'].iloc[0], gps_data['Latitude'].iloc[0],
               'g^', markersize=15, label='Launch', markeredgecolor='black')
        ax.plot(gps_data['Longitude'].iloc[-1], gps_data['Latitude'].iloc[-1],
               'rs', markersize=15, label='Landing', markeredgecolor='black')

        # Colorbar for altitude
        cbar = plt.colorbar(scatter, ax=ax, label='Altitude (m MSL)')

        ax.set_xlabel('Longitude', fontsize=12)
        ax.set_ylabel('Latitude', fontsize=12)
        ax.set_title('GPS Ground Track', fontsize=14, fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)
        ax.set_aspect('equal', adjustable='box')

        plt.tight_layout()
        output_path = output_dir / 'gps_track.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def plot_pyro_channels(self, output_dir):
        """Generate pyrotechnic channel activation plot"""
        fig, ax = plt.subplots(figsize=(14, 6))

        self.add_state_backgrounds(ax)

        # Plot each pyro channel
        colors = ['red', 'orange', 'green', 'blue']
        for i in range(4):
            pyro_col = f'Pyro{i}'
            ax.plot(self.df['Time_sec'], self.df[pyro_col] * (i + 1),
                   label=f'Channel {i}', color=colors[i], linewidth=2,
                   drawstyle='steps-post')

        ax.set_xlabel('Time (seconds)', fontsize=12)
        ax.set_ylabel('Pyro Channel', fontsize=12)
        ax.set_yticks([0, 1, 2, 3, 4])
        ax.set_yticklabels(['OFF', 'Ch0', 'Ch1', 'Ch2', 'Ch3'])
        ax.set_title('Pyrotechnic Channel Activations', fontsize=14, fontweight='bold')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)

        plt.tight_layout()
        output_path = output_dir / 'pyro_channels.png'
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        plt.close()

        return output_path

    def generate_all_plots(self, output_dir):
        """Generate all visualization plots"""
        output_dir = Path(output_dir)
        output_dir.mkdir(exist_ok=True)

        plots = []

        print("  → Generating altitude profile...")
        plots.append(self.plot_altitude_profile(output_dir))

        print("  → Generating acceleration plots...")
        plots.append(self.plot_acceleration(output_dir))

        print("  → Generating pressure/temperature plots...")
        plots.append(self.plot_pressure_temperature(output_dir))

        print("  → Generating state timeline...")
        plots.append(self.plot_state_timeline(output_dir))

        print("  → Generating pyro channel plot...")
        plots.append(self.plot_pyro_channels(output_dir))

        print("  → Generating GPS track...")
        gps_plot = self.plot_gps_track(output_dir)
        if gps_plot:
            plots.append(gps_plot)

        return [p for p in plots if p is not None]
