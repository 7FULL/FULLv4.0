# 🚀 Rocket Flight Data Analyzer

A comprehensive Python tool for analyzing flight data from the rocket's flight computer. This tool reads CSV files logged during flight and provides detailed visualizations, statistics, and reports.

## Features

- **Comprehensive Data Analysis**: Altitude, acceleration, velocity, temperature, pressure
- **State Machine Tracking**: Visualize flight state transitions (SLEEP, ARMED, BOOST, COAST, APOGEE, PARACHUTE, LANDED)
- **Pyrotechnic Event Logging**: Track all pyro channel activations with timestamps
- **GPS Ground Track**: Visualize GPS trajectory (if available)
- **Automated Plotting**: Generate publication-quality plots automatically
- **HTML Reports**: Detailed flight reports with all statistics
- **Flight Statistics**: Apogee, max acceleration, ascent/descent rates, and more

## Installation

### Prerequisites

- Python 3.8 or higher
- pip (Python package manager)

### Setup

1. Navigate to the FlightDataAnalyzer directory:
```bash
cd FlightDataAnalyzer
```

2. Install required dependencies:
```bash
pip install -r requirements.txt
```

Or install manually:
```bash
pip install pandas numpy matplotlib scipy
```

## Usage

### Basic Analysis

Simply provide the CSV file path to get a flight summary:

```bash
python analyzer.py flight_data.csv
```

This will display:
- Flight duration
- Maximum altitude (AGL and MSL)
- Maximum acceleration
- State transitions with timestamps
- Pyrotechnic events

### Generate Plots

Create comprehensive visualization plots:

```bash
python analyzer.py flight_data.csv --plot
```

This generates:
- **Altitude Profile**: Altitude vs time with apogee marker
- **Acceleration Plots**: 3-axis and total acceleration
- **Pressure/Temperature**: Environmental data
- **State Timeline**: Visual flight state progression
- **Pyro Channels**: Pyrotechnic activation timeline
- **GPS Track**: Ground track map (if GPS data available)

Plots are saved to `./analysis_output/` by default.

### Generate HTML Report

Create a detailed HTML report with all statistics:

```bash
python analyzer.py flight_data.csv --report
```

The report includes:
- Complete flight statistics
- Phase durations
- Acceleration and velocity analysis
- GPS data (if available)
- Data quality metrics

### Full Analysis (Plots + Report)

Run complete analysis with all outputs:

```bash
python analyzer.py flight_data.csv --plot --report
```

### Custom Output Directory

Specify a custom output directory:

```bash
python analyzer.py flight_data.csv --plot --report --output ./my_analysis
```

## CSV Data Format

The analyzer expects CSV files with the following columns (no header):

```
Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3
```

### Column Descriptions:

- **Timestamp**: Milliseconds since MCU boot
- **AccelX, AccelY, AccelZ**: Acceleration in G (gravity units)
- **GyroX, GyroY, GyroZ**: Angular velocity (reserved for future use)
- **Pressure**: Barometric pressure in Pa
- **Temperature**: Temperature in °C
- **Altitude**: Calculated altitude in meters MSL
- **Latitude, Longitude**: GPS coordinates (decimal degrees)
- **GPS_Alt**: GPS altitude in meters
- **State**: State machine state (0-8)
- **Pyro0-3**: Pyro channel states (0=inactive, 1=active)

### State Mapping:

| State | Name       | Description                  |
|-------|------------|------------------------------|
| 0     | SLEEP      | Pre-flight idle              |
| 1     | ARMED      | Armed and ready for launch   |
| 2     | BOOST      | Motor burning                |
| 3     | COAST      | Coasting to apogee           |
| 4     | APOGEE     | At or near apogee            |
| 5     | PARACHUTE  | Parachute deployed, descending|
| 6     | LANDED     | Rocket has landed            |
| 7     | ERROR      | Error state                  |
| 8     | ABORT      | Aborted flight               |

## Example Workflow

### 1. Copy Flight Data from SD Card

After recovering the rocket, copy CSV files from the SD card:
- `flights/flight_YYYYMMDD_HHMMSS.csv` - Flight data logged during flight
- `recovery/recovery_YYYYMMDD_HHMMSS.csv` - Data exported after landing

### 2. Analyze the Flight

```bash
# Quick summary
python analyzer.py ../MS/SD_CARD/flights/flight_20250115_143022.csv

# Generate plots
python analyzer.py ../MS/SD_CARD/flights/flight_20250115_143022.csv --plot

# Full analysis with report
python analyzer.py ../MS/SD_CARD/flights/flight_20250115_143022.csv --plot --report
```

### 3. Review Results

- **Console**: Flight summary with key statistics
- **Plots**: PNG images in `./analysis_output/`
- **Report**: HTML file with comprehensive analysis

## Output Files

When running with `--plot` and `--report`, the following files are generated:

```
analysis_output/
├── altitude_profile.png          # Altitude vs time
├── acceleration.png               # Acceleration plots
├── pressure_temperature.png       # Environmental data
├── state_timeline.png             # Flight state visualization
├── pyro_channels.png              # Pyro activation timeline
├── gps_track.png                  # GPS ground track (if available)
└── flight_report_YYYYMMDD_HHMMSS.html  # Detailed HTML report
```

## Troubleshooting

### Import Errors

If you see `ModuleNotFoundError`, ensure all dependencies are installed:
```bash
pip install -r requirements.txt
```

### CSV Format Errors

If the analyzer fails to read the CSV:
1. Verify the file has no header row
2. Check that columns match the expected format
3. Ensure numeric values are properly formatted (no spaces, correct decimal separator)

### Missing GPS Data

GPS plots will be skipped if no valid GPS data is found. This is normal for:
- Indoor testing
- Simulation mode
- Flights where GPS didn't lock

### Empty Plots

If plots appear empty:
- Check that the CSV contains actual flight data (not just initialization)
- Verify timestamps are increasing
- Ensure sensor values are within reasonable ranges

## Advanced Usage

### Analyzing Multiple Flights

Use a shell script to batch process multiple flights:

```bash
# Windows (PowerShell)
Get-ChildItem ../MS/SD_CARD/flights/*.csv | ForEach-Object {
    python analyzer.py $_.FullName --plot --report --output "./analysis_$($_.BaseName)"
}

# Linux/Mac
for file in ../MS/SD_CARD/flights/*.csv; do
    basename=$(basename "$file" .csv)
    python analyzer.py "$file" --plot --report --output "./analysis_$basename"
done
```

### Programmatic Access

You can also use the analyzer classes in your own Python scripts:

```python
from analyzer import FlightDataAnalyzer
from visualizer import FlightVisualizer
from statistics import FlightStatistics

# Load and analyze data
analyzer = FlightDataAnalyzer('flight_data.csv')
analyzer.load_data()
analyzer.extract_flight_info()

# Access flight data directly
print(f"Apogee: {analyzer.flight_info['apogee_agl']:.1f} m")
print(f"Max acceleration: {analyzer.flight_info['max_accel']:.2f} G")

# Generate custom plots
visualizer = FlightVisualizer(analyzer.df, analyzer.flight_info)
visualizer.plot_altitude_profile('./output')

# Calculate detailed statistics
stats = FlightStatistics(analyzer.df, analyzer.flight_info)
stats.calculate_all_statistics()
print(stats.stats)
```

## Data Analysis Tips

### Verifying Flight Performance

1. **Apogee Detection**: Check that apogee detection occurred at peak altitude
2. **State Transitions**: Verify smooth transitions between states
3. **Pyro Timing**: Confirm pyro events happened at expected times/states
4. **Sensor Health**: Review data quality percentages in the report

### Identifying Issues

- **Early ERROR State**: Check sensor health indicators
- **Missing State Transitions**: Review arming conditions and thresholds
- **Unexpected Pyro Activations**: Check pyro configuration and safety interlocks
- **GPS Dropouts**: Normal during high acceleration or altitude changes

### Simulation Mode Analysis

When analyzing simulation data:
- Expect synthetic sensor values
- State transitions will follow simulation timeline
- GPS data may be simulated coordinates
- Use to verify state machine logic before real flights

## Contributing

This tool is part of the rocket flight computer project. Improvements and bug fixes are welcome!

## Version History

- **v1.0** (2025-01-15): Initial release
  - Basic CSV parsing and analysis
  - Comprehensive plotting suite
  - HTML report generation
  - GPS ground track visualization
  - Pyrotechnic event tracking

## License

This tool is part of the experimental rocket flight computer project.
Use at your own risk for educational and research purposes.

---

**Safety Note**: Always follow proper rocketry safety procedures. This tool is for post-flight analysis only and does not replace proper pre-flight checks and safety protocols.
