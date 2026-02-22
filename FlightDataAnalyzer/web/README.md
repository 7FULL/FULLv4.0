# Rocket Flight Data Analyzer - Web Interface

Interactive web-based analysis tool for rocket flight computer data (STM32F411RET6 Master MCU).

## Features

### 📊 Interactive Charts
- **Altitude Profile**: Real-time altitude AGL with zoom/pan capabilities
  - **NEW**: Interactive zoom with mouse wheel and pinch gestures
  - **NEW**: Pan through data by clicking and dragging
  - Color-coded state backgrounds (SLEEP, ARMED, BOOST, COAST, APOGEE, PARACHUTE, LANDED)
  - Visual state transitions overlay on chart
- **3-Axis Acceleration**: X, Y, Z acceleration data visualization
  - **NEW**: Interactive zoom and pan enabled
- **Vertical Velocity**: Calculated from acceleration integration (not in CSV)
  - **NEW**: Automatically computed from AccelX data
  - State backgrounds showing flight phases
  - No noise when stationary on ground
  - Accurate during all flight phases
  - **NEW**: Interactive zoom and pan
- **Atmospheric Conditions**: Pressure and temperature dual-axis chart
  - **NEW**: Interactive zoom and pan enabled
- **Gyroscope Data**: 3-axis angular velocity tracking
  - **NEW**: Interactive zoom and pan enabled
- **GPS Track**: Flight path visualization (when GPS data available)

### 📈 Flight Statistics
- Flight duration
- Maximum altitude (AGL)
- Maximum acceleration
- Apogee time
- Maximum descent rate (calculated from velocity)
- Total data points logged
- **NEW**: Sample rate (Hz) - average logging frequency
- **NEW**: Drift from launch - horizontal distance between launch and landing (GPS)

### 🎯 Flight Phase Timeline
Visual timeline showing:
- SLEEP → ARMED → BOOST → COAST → APOGEE → PARACHUTE → LANDED
- Duration of each phase
- Color-coded state indicators

### 🔔 Event Log
Chronological list of:
- State transitions
- Pyro channel activations (Drogue, Main, Backup, Separation)
- Critical flight events

### 🎨 Modern UI/UX
- Dark theme optimized for long analysis sessions
- Responsive design (works on desktop, tablet, mobile)
- Drag & drop file upload
- Real-time chart updates
- Export functionality for reports

## Usage

### Quick Start

1. **Open the Web App**
   ```bash
   # Simply open index.html in your browser
   # No server required - runs entirely client-side
   ```

   Or double-click `index.html` in your file explorer.

2. **Load Flight Data**
   - Drag & drop a CSV file onto the upload area
   - OR click "Select File" to browse

3. **Analyze**
   - View statistics automatically calculated
   - Interact with charts (zoom, pan, hover for values)
   - Review flight phases and events
   - Export report as JSON

### Supported CSV Format

The analyzer expects CSV files with the following columns:
```
Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3
```

Example:
```csv
16135,0.978,0.031,0.060,0.0,0.0,0.0,947.78,23.80,559.00,0.0,0.0,0.0,SLEEP,0,0,0,0
16443,0.981,0.030,0.060,0.0,0.0,0.0,947.59,23.67,561.00,0.0,0.0,0.0,ARMED,0,0,0,0
```

### Chart Interactions

#### Zoom & Pan
- **Mouse Wheel**: Scroll over chart to zoom in/out on time axis
- **Pinch Gesture**: Use two fingers on touch devices to zoom
- **Click & Drag**: Click and drag to pan through data (when zoomed in)
- **Double Click**: Reset zoom to full view
- **Zoom Mode**: All charts support horizontal (time axis) zoom

#### Hover
- Hover over any data point to see exact values
- Timestamp, value, and current flight state displayed in tooltip
- Works on all charts simultaneously

### Export Report

Click "Export Report" to download a JSON file containing:
- File metadata
- Calculated statistics
- Complete raw flight data
- Timestamp of export

## File Structure

```
web/
├── index.html       # Main HTML page
├── styles.css       # Styling and themes
├── app.js           # JavaScript logic
└── README.md        # This file
```

## Browser Compatibility

Tested and supported on:
- ✅ Chrome/Edge (v90+)
- ✅ Firefox (v88+)
- ✅ Safari (v14+)
- ✅ Opera (v76+)

**Note**: Internet Explorer is NOT supported.

## Dependencies

- **Chart.js v4.4.0** (loaded from CDN)
- **Chart.js Zoom Plugin v2.0.1** (loaded from CDN)
- **Hammer.js v2.0.8** (for touch gestures, loaded from CDN)
- No installation required
- No build process needed
- 100% client-side processing

## Advanced Features

### Custom Data Processing

The analyzer automatically:
1. **Normalizes timestamps** to seconds from flight start
2. **Calculates ground altitude** from SLEEP state readings
3. **Computes altitude AGL** (Above Ground Level)
4. **Calculates vertical velocity** from acceleration integration
   - Integrates AccelX data to compute velocity
   - Subtracts 1G gravity compensation
   - Resets to zero during SLEEP/ARMED states
   - No barometer derivative noise
5. **Computes sample rate (Hz)** from timestamp intervals
6. **Calculates horizontal drift** from GPS coordinates
   - Uses Haversine formula for accurate distance
   - Measures distance between launch and landing points
   - Shows "No GPS" if GPS data unavailable
7. **Detects state transitions** and events
8. **Identifies pyro activations** from bit flags

### Performance

- Handles CSV files up to **100,000+ data points**
- Smooth rendering with optimized Chart.js settings
- Minimal memory footprint
- No server required (runs entirely in browser)

## Troubleshooting

### File Won't Load
- Ensure CSV has correct headers
- Check for encoding issues (UTF-8 recommended)
- Verify all rows have same number of columns

### Charts Not Displaying
- Check browser console for errors (F12)
- Ensure Chart.js loaded from CDN (requires internet)
- Try refreshing the page

### Performance Issues
- Large files (>50MB) may be slow
- Try reducing data logging frequency in firmware
- Close other browser tabs to free memory

## Development

### Modifying the Analyzer

**Add new chart:**
1. Add HTML canvas in `index.html`
2. Create chart function in `app.js`
3. Call function in `createCharts()`

**Customize theme:**
- Edit CSS variables in `:root` selector in `styles.css`
- Colors, spacing, and fonts are centralized

**Add new statistics:**
1. Calculate in `displayStatistics()` function
2. Update HTML stat card in `index.html`

## Privacy & Security

- **100% Client-Side**: No data uploaded to servers
- **No Tracking**: No analytics or cookies
- **Offline Capable**: Works without internet (after first load)
- **Secure**: No external data transmission

## Examples

Sample flight data CSV files can be found in:
```
FlightDataAnalyzer/flight_data.csv
```

## License

Part of the Rocket Flight Computer project.
For educational and experimental rocketry use.

## Support

For issues or questions:
1. Check CLAUDE.md in project root for system architecture
2. Review firmware documentation in MS/Core/
3. Analyze CSV format compatibility

---

**Version**: 1.1.0
**Last Updated**: 2026-01-08
**Compatible with**: STM32F411RET6 Flight Computer Firmware v4.0

### Changelog v1.1.0
- ✅ Added interactive zoom and pan to all charts (mouse wheel, pinch gestures)
- ✅ Added automatic vertical velocity calculation from acceleration integration
- ✅ Added sample rate (Hz) display in statistics
- ✅ Added drift from launch calculation using GPS coordinates (Haversine formula)
- ✅ Re-enabled velocity chart with calculated data
- ✅ Improved chart interaction with double-click reset
- ✅ State backgrounds now visible on altitude and velocity charts
- ✅ 8 statistics cards in balanced 2x4 grid layout
