#!/usr/bin/env python3
"""
Example Usage Script
Demonstrates how to use the Flight Data Analyzer programmatically
"""

from analyzer import FlightDataAnalyzer
from visualizer import FlightVisualizer
from statistics import FlightStatistics
from pathlib import Path


def example_basic_analysis(csv_file):
    """Example: Basic flight analysis"""
    print("\n" + "="*60)
    print("EXAMPLE 1: Basic Flight Analysis")
    print("="*60)

    # Create analyzer instance
    analyzer = FlightDataAnalyzer(csv_file)

    # Load and parse data
    if not analyzer.load_data():
        print("Failed to load data!")
        return

    # Extract flight information
    info = analyzer.extract_flight_info()

    # Print summary
    analyzer.print_summary()

    # Access specific data
    print("\nCustom Data Access:")
    print(f"  Apogee occurred at: {info['apogee_time']:.2f} seconds")
    print(f"  Maximum altitude AGL: {info['apogee_agl']:.1f} meters")
    print(f"  Sample rate: {info['sample_rate']:.1f} Hz")


def example_generate_plots(csv_file, output_dir='./example_output'):
    """Example: Generate visualization plots"""
    print("\n" + "="*60)
    print("EXAMPLE 2: Generate Visualization Plots")
    print("="*60)

    # Setup
    analyzer = FlightDataAnalyzer(csv_file)
    analyzer.load_data()
    analyzer.extract_flight_info()

    # Create visualizer
    visualizer = FlightVisualizer(analyzer.df, analyzer.flight_info)

    # Generate all plots
    output_path = Path(output_dir)
    output_path.mkdir(exist_ok=True)

    print("\nGenerating plots...")
    plot_files = visualizer.generate_all_plots(output_path)

    print(f"\n✓ Generated {len(plot_files)} plots in: {output_path}")
    for plot_file in plot_files:
        print(f"  - {plot_file.name}")


def example_statistics_report(csv_file, output_dir='./example_output'):
    """Example: Generate statistics and HTML report"""
    print("\n" + "="*60)
    print("EXAMPLE 3: Statistics and HTML Report")
    print("="*60)

    # Setup
    analyzer = FlightDataAnalyzer(csv_file)
    analyzer.load_data()
    analyzer.extract_flight_info()

    # Create statistics generator
    stats = FlightStatistics(analyzer.df, analyzer.flight_info)

    # Calculate all statistics
    flight_stats = stats.calculate_all_statistics()

    # Print some statistics
    print("\nFlight Statistics:")
    print(f"  Altitude:")
    print(f"    - Apogee AGL: {flight_stats['altitude']['apogee_agl']:.1f} m")
    print(f"    - Avg ascent rate: {flight_stats['altitude']['avg_ascent_rate']:.1f} m/s")
    print(f"    - Max descent rate: {flight_stats['altitude']['max_descent_rate']:.1f} m/s")

    print(f"\n  Acceleration:")
    print(f"    - Max total: {flight_stats['acceleration']['max_total']:.2f} G")
    print(f"    - Boost duration: {flight_stats['acceleration']['boost_duration']:.2f} s")

    print(f"\n  Data Quality:")
    print(f"    - Total samples: {flight_stats['data_quality']['total_samples']}")
    print(f"    - Sample rate: {flight_stats['data_quality']['sample_rate_hz']:.1f} Hz")
    print(f"    - Accel valid: {flight_stats['data_quality']['accel_valid_pct']:.1f}%")
    print(f"    - Baro valid: {flight_stats['data_quality']['baro_valid_pct']:.1f}%")

    # Generate HTML report
    output_path = Path(output_dir)
    output_path.mkdir(exist_ok=True)

    print("\nGenerating HTML report...")
    report_file = stats.generate_html_report(output_path)
    print(f"✓ Report saved to: {report_file}")


def example_custom_analysis(csv_file):
    """Example: Custom data analysis using pandas"""
    print("\n" + "="*60)
    print("EXAMPLE 4: Custom Data Analysis")
    print("="*60)

    # Setup
    analyzer = FlightDataAnalyzer(csv_file)
    analyzer.load_data()

    # Access the pandas DataFrame directly
    df = analyzer.df

    print("\nDataFrame Info:")
    print(f"  Rows: {len(df)}")
    print(f"  Columns: {len(df.columns)}")
    print(f"  Time range: {df['Time_sec'].min():.2f}s to {df['Time_sec'].max():.2f}s")

    # Custom analysis: Find time when acceleration exceeded 5G
    high_accel = df[df['Accel_Total'] > 5.0]
    if not high_accel.empty:
        print(f"\n  Acceleration exceeded 5G for {len(high_accel)} samples")
        print(f"  First occurrence: {high_accel['Time_sec'].iloc[0]:.2f}s")
        print(f"  Last occurrence: {high_accel['Time_sec'].iloc[-1]:.2f}s")
    else:
        print("\n  Acceleration never exceeded 5G")

    # Custom analysis: Time spent in each state
    print("\n  Time in each state:")
    for state_name in df['State_Name'].unique():
        state_data = df[df['State_Name'] == state_name]
        if len(state_data) > 0:
            duration = state_data['Time_sec'].iloc[-1] - state_data['Time_sec'].iloc[0]
            print(f"    {state_name}: {duration:.2f}s ({len(state_data)} samples)")


def main():
    """Main example runner"""
    import sys

    if len(sys.argv) < 2:
        print("Usage: python example_usage.py <flight_data.csv>")
        print("\nThis script demonstrates various ways to use the Flight Data Analyzer")
        return

    csv_file = sys.argv[1]

    if not Path(csv_file).exists():
        print(f"ERROR: File not found: {csv_file}")
        return

    print("\n" + "="*60)
    print("FLIGHT DATA ANALYZER - EXAMPLE USAGE")
    print("="*60)
    print(f"\nAnalyzing: {csv_file}")

    try:
        # Run all examples
        example_basic_analysis(csv_file)
        example_custom_analysis(csv_file)
        example_generate_plots(csv_file)
        example_statistics_report(csv_file)

        print("\n" + "="*60)
        print("ALL EXAMPLES COMPLETED SUCCESSFULLY!")
        print("="*60)
        print("\nCheck ./example_output/ for generated files\n")

    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()


if __name__ == '__main__':
    main()
