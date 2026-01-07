# Quick Start Guide

Get started analyzing your rocket flight data in 3 simple steps!

## Step 1: Install Dependencies

```bash
cd FlightDataAnalyzer
pip install -r requirements.txt
```

## Step 2: Analyze Your Flight

```bash
python analyzer.py path/to/your/flight_data.csv
```

## Step 3: Generate Plots and Report

```bash
python analyzer.py path/to/your/flight_data.csv --plot --report
```

That's it! Your analysis results will be in `./analysis_output/`

---

## Common Use Cases

### Quick Summary Only
```bash
python analyzer.py flight_data.csv
```

### Just Plots
```bash
python analyzer.py flight_data.csv --plot
```

### Just Report
```bash
python analyzer.py flight_data.csv --report
```

### Everything
```bash
python analyzer.py flight_data.csv --plot --report
```

### Custom Output Location
```bash
python analyzer.py flight_data.csv --plot --report --output ./my_results
```

---

## Where to Find Flight Data

After recovering your rocket, connect the SD card and look for:

- **MS/SD_CARD/flights/** - Binary flight data (converted to CSV after landing)
- **MS/SD_CARD/recovery/** - Recovery CSV files (if generated on landing)
- **MS/SD_CARD/debug.txt** - Debug log file

---

## Troubleshooting

**Q: "ModuleNotFoundError: No module named 'pandas'"**
A: Run `pip install -r requirements.txt`

**Q: "FileNotFoundError: CSV file not found"**
A: Check that your file path is correct. Use absolute paths if needed.

**Q: "No GPS data available"**
A: This is normal if GPS didn't lock or for simulation flights. Other plots will still work.

---

## Next Steps

- Read [README.md](README.md) for detailed documentation
- Run `python example_usage.py flight_data.csv` to see programmatic usage examples
- Check the generated HTML report for comprehensive flight statistics

---

**Happy analyzing! 🚀**
