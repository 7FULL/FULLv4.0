"""
Rocket Flight Data Analyzer
A comprehensive tool for analyzing rocket flight computer data

Main modules:
- analyzer: Core data loading and analysis
- visualizer: Plot generation and visualization
- statistics: Statistical analysis and reporting
"""

__version__ = '1.0.0'
__author__ = 'Rocket Flight Computer Project'

from .analyzer import FlightDataAnalyzer
from .visualizer import FlightVisualizer
from .statistics import FlightStatistics

__all__ = ['FlightDataAnalyzer', 'FlightVisualizer', 'FlightStatistics']
