#!/usr/bin/env python3
"""
Flight Data Analyzer - Main Script
Rocket Flight Computer Data Analysis Tool

This script reads CSV flight data from the rocket's SD card and provides
comprehensive analysis, visualization, and statistics generation.
"""

import pandas as pd
import numpy as np
import argparse
import os
from pathlib import Path
from datetime import datetime
import sys

# Import local modules
from visualizer import FlightVisualizer
from statistics import FlightStatistics


class FlightDataAnalyzer:
    """Main class for analyzing rocket flight data"""

    def __init__(self, csv_file_path):
        """Initialize analyzer with CSV file path"""
        self.csv_file = Path(csv_file_path)
        self.df = None
        self.flight_info = {}

        if not self.csv_file.exists():
            raise FileNotFoundError(f"CSV file not found: {csv_file_path}")

    def load_data(self):
        """Load and parse CSV flight data"""
        print(f"Loading flight data from: {self.csv_file.name}")

        # CSV Format: Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3
        column_names = [
            'Timestamp', 'AccelX', 'AccelY', 'AccelZ',
            'GyroX', 'GyroY', 'GyroZ',
            'Pressure', 'Temperature', 'Altitude',
            'Latitude', 'Longitude', 'GPS_Alt',
            'State', 'Pyro0', 'Pyro1', 'Pyro2', 'Pyro3'
        ]

        try:
            # First, try to read the CSV and detect if there's a header
            sample = pd.read_csv(self.csv_file, nrows=1, header=None)

            # Check if first row looks like a header (contains non-numeric strings)
            first_row_is_header = False
            try:
                # Try to convert first row to float - if it fails, it's probably a header
                pd.to_numeric(sample.iloc[0, 0])
            except (ValueError, TypeError):
                first_row_is_header = True
                print("  Detected header row, skipping it...")

            # Read CSV with appropriate settings
            if first_row_is_header:
                self.df = pd.read_csv(self.csv_file, names=column_names, skiprows=1, header=None)
            else:
                self.df = pd.read_csv(self.csv_file, names=column_names, header=None)

            # Convert all numeric columns to float, handling any string values
            numeric_columns = [
                'Timestamp', 'AccelX', 'AccelY', 'AccelZ',
                'GyroX', 'GyroY', 'GyroZ',
                'Pressure', 'Temperature', 'Altitude',
                'Latitude', 'Longitude', 'GPS_Alt',
                'Pyro0', 'Pyro1', 'Pyro2', 'Pyro3'
            ]

            print("  Converting data types...")
            for col in numeric_columns:
                self.df[col] = pd.to_numeric(self.df[col], errors='coerce')

            # Remove any rows with NaN timestamps (invalid data)
            initial_rows = len(self.df)
            self.df = self.df.dropna(subset=['Timestamp'])
            if len(self.df) < initial_rows:
                print(f"  ⚠ Removed {initial_rows - len(self.df)} rows with invalid timestamps")

            if len(self.df) == 0:
                print("ERROR: No valid data rows found in CSV!")
                return False

            # Convert timestamp from milliseconds to seconds (relative to start)
            self.df['Time_sec'] = (self.df['Timestamp'] - self.df['Timestamp'].iloc[0]) / 1000.0

            # Map state numbers to names
            self.df['State_Name'] = self.df['State']

            # Calculate total acceleration magnitude
            self.df['Accel_Total'] = np.sqrt(
                self.df['AccelX']**2 +
                self.df['AccelY']**2 +
                self.df['AccelZ']**2
            )

            # Calculate vertical velocity (integrate acceleration)
            self.df['Velocity'] = np.cumsum(self.df['AccelZ'] - 1.0) * (self.df['Time_sec'].diff().fillna(0))

            print(f"✓ Loaded {len(self.df)} data points")
            print(f"✓ Flight duration: {self.df['Time_sec'].iloc[-1]:.2f} seconds")

            return True

        except Exception as e:
            print(f"ERROR loading CSV: {e}")
            print("\nDEBUG INFO:")
            print(f"  File: {self.csv_file}")
            print(f"  File exists: {self.csv_file.exists()}")
            if self.csv_file.exists():
                print(f"  File size: {self.csv_file.stat().st_size} bytes")
                print("\nFirst few lines of file:")
                try:
                    with open(self.csv_file, 'r') as f:
                        for i, line in enumerate(f):
                            if i >= 3:
                                break
                            print(f"    Line {i+1}: {line.rstrip()}")
                except:
                    pass
            import traceback
            traceback.print_exc()
            return False

    def extract_flight_info(self):
        """Extract key flight information"""
        if self.df is None:
            print("ERROR: No data loaded")
            return

        # Basic flight info
        self.flight_info['duration'] = self.df['Time_sec'].iloc[-1]
        self.flight_info['data_points'] = len(self.df)
        self.flight_info['sample_rate'] = len(self.df) / self.flight_info['duration']

        # Altitude stats
        self.flight_info['max_altitude'] = self.df['Altitude'].max()
        self.flight_info['ground_altitude'] = self.df['Altitude'].iloc[0]
        self.flight_info['apogee_agl'] = self.flight_info['max_altitude'] - self.flight_info['ground_altitude']

        # Find apogee time
        apogee_idx = self.df['Altitude'].idxmax()
        self.flight_info['apogee_time'] = self.df.loc[apogee_idx, 'Time_sec']

        # Acceleration stats
        self.flight_info['max_accel'] = self.df['Accel_Total'].max()
        self.flight_info['max_accel_x'] = self.df['AccelX'].max()
        self.flight_info['max_accel_y'] = self.df['AccelY'].max()
        self.flight_info['max_accel_z'] = self.df['AccelZ'].max()

        # State transitions
        state_changes = self.df[self.df['State'] !=  "SLEEP"]
        self.flight_info['state_transitions'] = state_changes[['Time_sec', 'State', 'State_Name']].to_dict('records')

        # Pyro events
        pyro_events = []
        for i in range(4):
            pyro_col = f'Pyro{i}'
            activations = self.df[self.df[pyro_col].diff() > 0]
            if not activations.empty:
                for idx, row in activations.iterrows():
                    pyro_events.append({
                        'channel': i,
                        'time': row['Time_sec'],
                        'state': row['State_Name']
                    })
        self.flight_info['pyro_events'] = pyro_events

        return self.flight_info

    def print_summary(self):
        """Print flight summary to console"""
        info = self.flight_info

        print("\n" + "="*60)
        print("FLIGHT DATA SUMMARY")
        print("="*60)
        print(f"File: {self.csv_file.name}")
        print(f"Duration: {info['duration']:.2f} seconds")
        print(f"Data points: {info['data_points']} ({info['sample_rate']:.1f} Hz)")
        print()

        print("ALTITUDE:")
        print(f"  Ground level: {info['ground_altitude']:.1f} m MSL")
        print(f"  Maximum altitude: {info['max_altitude']:.1f} m MSL")
        print(f"  Apogee AGL: {info['apogee_agl']:.1f} m")
        print(f"  Apogee time: {info['apogee_time']:.2f} s")
        print()

        print("ACCELERATION:")
        print(f"  Max total: {info['max_accel']:.2f} G")
        print(f"  Max X-axis: {info['max_accel_x']:.2f} G")
        print(f"  Max Y-axis: {info['max_accel_y']:.2f} G")
        print(f"  Max Z-axis: {info['max_accel_z']:.2f} G")
        print()

        print("STATE TRANSITIONS:")
        for transition in info['state_transitions']:
            print(f"  {transition['Time_sec']:6.2f}s -> {transition['State_Name']}")
        print()

        if info['pyro_events']:
            print("PYRO EVENTS:")
            for event in info['pyro_events']:
                print(f"  Channel {event['channel']}: {event['time']:.2f}s ({event['state']})")
        else:
            print("PYRO EVENTS: None")

        print("="*60 + "\n")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(
        description='Rocket Flight Data Analyzer',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python analyzer.py flight_data.csv                    # Analyze and show summary
  python analyzer.py flight_data.csv --plot             # Generate all plots
  python analyzer.py flight_data.csv --report           # Generate detailed report
  python analyzer.py flight_data.csv --plot --report    # Full analysis
        """
    )

    parser.add_argument('csv_file', help='Path to flight data CSV file')
    parser.add_argument('--plot', action='store_true', help='Generate visualization plots')
    parser.add_argument('--report', action='store_true', help='Generate detailed HTML report')
    parser.add_argument('--output', '-o', help='Output directory for plots/reports (default: ./analysis_output)')

    args = parser.parse_args()

    # Create output directory
    output_dir = Path(args.output) if args.output else Path('./analysis_output')
    output_dir.mkdir(exist_ok=True)

    # Initialize analyzer
    try:
        analyzer = FlightDataAnalyzer(args.csv_file)
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        return 1

    # Load data
    if not analyzer.load_data():
        return 1

    # Extract flight info
    analyzer.extract_flight_info()

    # Print summary
    analyzer.print_summary()

    # Generate plots if requested
    if args.plot:
        print("Generating visualization plots...")
        visualizer = FlightVisualizer(analyzer.df, analyzer.flight_info)
        visualizer.generate_all_plots(output_dir)
        print(f"✓ Plots saved to: {output_dir}")

    # Generate report if requested
    if args.report:
        print("Generating detailed report...")
        stats = FlightStatistics(analyzer.df, analyzer.flight_info)
        report_path = stats.generate_html_report(output_dir)
        print(f"✓ Report saved to: {report_path}")

    print("\nAnalysis complete!")
    return 0


if __name__ == '__main__':
    sys.exit(main())
