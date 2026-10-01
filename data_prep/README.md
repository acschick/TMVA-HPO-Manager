# Data Preparation Studies

This directory contains physics study-specific data preparation materials, organized by the physics process being studied.

## Organization Structure

Each subdirectory represents a distinct physics study or approach:

```
data_prep/
├── epem_pippim/           # Electron-pion separation (e+e- vs π+π-)
├── muon_classification/   # μ+μ- vs π+π- (future study)
└── ...
```

## Study Directory Contents

Each study directory should contain:

- **DSelector classes** (`.C` and `.h` files) - ROOT analysis selectors
- **README.md** - Study-specific documentation
- **run_info.txt** or similar - Run numbers, RCDB queries, data locations
- **Configuration files** - Any study-specific settings
- **Helper scripts** - Data validation, QA plots, etc.

## Current Studies

### epem_pippim

**Physics Goal**: Train MVA to separate electron tracks (e+e- signal) from pion tracks (π+π- background)

**DSelectors**:
- `DSelector_2eMissingProton.C/.h` - Extract e+e- tracks from γp → e+e-p reaction
- `DSelector_pippim__B4.C/.h` - Extract π+π- tracks from hadronic reactions

**Training Data**:
- Signal: Electron tracks from exclusive e+e- production
- Background: Pion tracks from various hadronic channels

**Features**: FCAL shower properties (E/p, DOCA, energy ratios, saturation, etc.)

See [epem_pippim/README.md](epem_pippim/README.md) for detailed information.

---

## Adding New Studies

To add a new data preparation study:

1. **Create study directory**:
   ```bash
   mkdir data_prep/my_new_study
   ```

2. **Add DSelector(s)**:
   ```bash
   cp MyDSelector.C MyDSelector.h data_prep/my_new_study/
   ```

3. **Document the study** - Create `README.md` with:
   - Physics motivation
   - Signal/background definitions
   - Run ranges and RCDB queries
   - Expected output format
   - Example usage

4. **Add run information** - Create `run_info.txt` or similar:
   ```
   # Run ranges for Spring 2018 production
   RCDB_QUERY: "@is_production and @status_approved"
   RUN_RANGE: 40856-41384
   DATA_LOCATION: /cache/halld/RunPeriod-2018-01/...
   ```

## Best Practices

- **One study per directory**: Keep related materials together
- **Multiple approaches**: If trying different selection strategies for the same physics, create separate subdirectories (e.g., `epem_pippim_v1`, `epem_pippim_tight_cuts`)
- **Document thoroughly**: Include RCDB queries, run numbers, known issues
- **Version control**: Git-track DSelectors and configs, .gitignore large ROOT files
- **Reproducibility**: Document software versions, environment setup

## Integration with TMVA Training

Data preparation outputs (ROOT trees with features) feed into the TMVA training pipeline:

```
data_prep/epem_pippim/  →  [ROOT files]  →  macros/training/  →  TMVA models
```

1. DSelectors produce flat ROOT trees with training features
2. `macros/data_prep/unroll_tracks.C` converts event-level → track-level
3. `macros/training/TMVAClassification.C` trains on resulting trees
4. Feature selection controlled by `config/vars.ini`

---

**Questions?** See main [README.md](../README.md) or [QUICKSTART.md](../QUICKSTART.md)
