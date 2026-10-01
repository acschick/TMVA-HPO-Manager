"""TMVA HPO - Clean System Package

A comprehensive hyperparameter optimization framework for TMVA machine learning
in particle physics, specifically designed for electron-pion separation in the
GlueX CPP experiment.

Main components:
- tmva_hpo_manager: Core orchestration and job management
- extract_tmva_metrics: ROC AUC extraction from TMVA output
- merge_results: Combine results from multiple jobs
- analyze_tmva_results: Result analysis and visualization

Author: acschick@jlab.org
Version: 1.0.0
"""

__version__ = "1.0.0"
__author__ = "acschick@jlab.org"

# Make core classes available at package level
from .tmva_hpo_manager import TMVAHPOManager

__all__ = ["TMVAHPOManager"]
