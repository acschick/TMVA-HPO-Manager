# TMVA Hyperparameter Optimization Framework

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Python 3.7+](https://img.shields.io/badge/python-3.7+-blue.svg)](https://www.python.org/downloads/)
[![ROOT 6.x](https://img.shields.io/badge/ROOT-6.x-blue.svg)](https://root.cern/)

A comprehensive hyperparameter optimization framework for TMVA machine learning in particle physics. Originally developed for electron-pion separation in the GlueX Charged Pion Polarizability (CPP) experiment, but generalizable to any TMVA classification task.

## Features

- **Automated Grid Search**: Generate thousands of TMVA training jobs with different hyperparameter combinations
- **Feature Engineering**: Binary-encoded feature selection system (10-bit FCAL features)
- **Batch Farm Integration**: Submit jobs to SWIF2 batch farms or run locally
- **Metric Extraction**: Automatic ROC AUC extraction from TMVA ROOT output files
- **Result Tracking**: Centralized JSON-based result management with full traceability
- **Analysis Tools**: Parallel coordinate plots, result summaries, and performance comparisons

## Quick Start

### Installation

```bash
# Clone repository
git clone https://github.com/yourusername/tmva-hpo.git
cd tmva-hpo

# Install in development mode
pip install -e .

# Or install dependencies manually
pip install -r requirements.txt
```

### Run Minimal Test (2 jobs, ~10 minutes)

```bash
# Generate 2 test jobs
python bin/tmva-hpo generate --config config/grid_configs/minimal.json

# Run jobs locally
python bin/tmva-hpo run --local

# Extract ROC AUC metrics
python bin/tmva-hpo extract-metrics

# Check results
python bin/tmva-hpo status
```

See [QUICKSTART.md](QUICKSTART.md) for detailed walkthrough.

## System Requirements

- **Python**: 3.7 or higher
- **ROOT**: 6.x with TMVA enabled
- **GlueX Environment**: For full CPP analysis (optional)
- **Batch System**: SWIF2 (optional, for farm submission)
- **RAM**: 4-16 GB per job depending on training sample size
- **Disk**: ~1 GB per 1000 jobs for outputs

## Project Structure

```
tmva-hpo/
├── bin/              # Command-line interface scripts
├── src/              # Core Python modules
├── macros/           # ROOT C++ macros (training, data prep)
├── data_prep/        # Physics study-specific data preparation
│   └── epem_pippim/  # Electron-pion separation (e+e- vs π+π-)
├── config/           # Configuration templates and grid definitions
├── scripts/          # Analysis and utility scripts
├── docs/             # Extended documentation
├── tests/            # Test suite
└── examples/         # Example runs and outputs
```

## Key Concepts

### Feature Encoding (10-bit Binary System)

Features are selected using a 10-bit binary code where each bit enables/disables a FCAL variable:

```
fcal_1111111000 = 7 features (baseline)
Bit 0: EoverPkin_FCAL      (E/p kinematic)       [ON]
Bit 1: TrackFCAL_DOCA      (Distance of CA)      [ON]
Bit 2: E9E25_FCAL          (9/25 crystal ratio)  [ON]
Bit 3: E1E9_FCAL           (1/9 crystal ratio)   [ON]
Bit 4: SumU_FCAL           (U coordinate sum)    [ON]
Bit 5: SumV_FCAL           (V coordinate sum)    [ON]
Bit 6: UV_Asymmetry_FCAL   (UV asymmetry)        [ON]
Bit 7: Saturation_FCAL     (Saturation flag)     [OFF]
Bit 8: (Reserved)
Bit 9: EoverPmeas_FCAL     (E/p measured)        [OFF]
```

See [NAMING_CONVENTIONS.md](NAMING_CONVENTIONS.md) for complete reference.

### Hyperparameter Grid

Configure search space in JSON files:

```json
{
  "mlp_hidden": ["N+5", "N+10", "N+15"],
  "mlp_ncycles": ["200", "400", "600"],
  "bdt_ntrees": ["400", "800", "1200"],
  "bdt_maxdepth": ["2", "3"],
  "feature_sets": ["fcal_1111111000", "fcal_1110000000"],
  "train_events": ["500000", "1000000"]
}
```

Total jobs = product of all list lengths (e.g., 3×3×3×2×2×2 = 216 jobs)

### Workflow

1. **Generate**: Create job directories with hyperparameter configurations
2. **Submit**: Run jobs (locally or on batch farm)
3. **Extract**: Parse TMVA output files for ROC AUC metrics
4. **Analyze**: Summarize results, create visualizations

## Documentation

- [QUICKSTART.md](QUICKSTART.md) - Get started in 5 minutes
- [SYSTEM_ARCHITECTURE.md](docs/SYSTEM_ARCHITECTURE.md) - How components connect
- [NAMING_CONVENTIONS.md](NAMING_CONVENTIONS.md) - File naming and feature encoding
- [docs/TUTORIAL.md](docs/TUTORIAL.md) - Step-by-step walkthrough
- [docs/BATCH_FARM_USAGE.md](docs/BATCH_FARM_USAGE.md) - SWIF2 integration guide
- [docs/FAQ.md](docs/FAQ.md) - Frequently asked questions

## Example Results

From comprehensive HPO study (8640 jobs):

- **Best MLP Performance**: ROC AUC = 0.9983 (N+20, 600 cycles, sigmoid, 1.5M training)
- **Best BDT Performance**: ROC AUC = 0.9869 (800 trees, depth 3, 1M training)
- **Optimal Features**: 8 features (all FCAL except measured E/p)
- **Training Sample Sweet Spot**: Diminishing returns beyond 1M events

See published results: [Link to paper/note]

## Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Citation

If you use this framework in your research, please cite:

```bibtex
@software{tmva_hpo,
  author = {Your Name},
  title = {TMVA Hyperparameter Optimization Framework},
  year = {2026},
  url = {https://github.com/yourusername/tmva-hpo}
}
```

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Contact

- **Author**: acschick@jlab.org
- **Issues**: https://github.com/yourusername/tmva-hpo/issues
- **GlueX Wiki**: https://halldweb.jlab.org/wiki/

## Acknowledgments

- GlueX Collaboration at Jefferson Lab
- CPP Analysis Working Group
- ROOT/TMVA development team

---

**Status**: Production-ready ✅  
**Last Updated**: May 29, 2026  
**Version**: 1.0.0
