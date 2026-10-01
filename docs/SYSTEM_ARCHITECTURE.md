# TMVA-HPO System Architecture

This document provides a comprehensive overview of how the TMVA Hyperparameter Optimization system components connect and interact.

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    ORCHESTRATION LAYER                       │
│                   tmva_hpo_manager.py                        │
│  ┌────────────┐  ┌─────────────┐  ┌────────────────────┐   │
│  │ Grid Gen   │  │ Job Runner  │  │ Metrics Extractor  │   │
│  └────────────┘  └─────────────┘  └────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                          ↓
        ┌─────────────────┼─────────────────┐
        ↓                 ↓                 ↓
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ Training     │  │ Data Files   │  │ Result       │
│ Macros       │  │ (ROOT)       │  │ Tracking     │
│ (TMVA C++)   │  │              │  │ (JSON)       │
└──────────────┘  └──────────────┘  └──────────────┘
```

## Core Workflow: Job Lifecycle

### 1. Configuration & Grid Generation

**Input**: Grid configuration JSON file
**Process**: Generate all hyperparameter combinations
**Output**: Job directories with individual configs

```python
# Example: config/grid_configs/minimal.json
{
  "mlp_hidden": ["N+5", "N+10"],      # 2 options
  "mlp_ncycles": ["200"],              # 1 option
  "bdt_ntrees": ["400"],               # 1 option
  "feature_sets": ["fcal_1111111000"] # 1 option
}
# Total jobs: 2 × 1 × 1 × 1 = 2 jobs
```

For each job, the system creates:
```
jobs/job_NNNN/
├── vars.ini              # Feature configuration (from template)
├── run_job.sh            # Job execution script
└── [ready for outputs]
```

### 2. Feature Encoding System

The 10-bit binary feature selection system:

```
Feature Binary Code: fcal_1111111000
Position  Feature                    Status
────────────────────────────────────────────
Bit 0:    EoverPkin_FCAL            ON  (1)
Bit 1:    TrackFCAL_DOCA            ON  (1)
Bit 2:    E9E25_FCAL                ON  (1)
Bit 3:    E1E9_FCAL                 ON  (1)
Bit 4:    SumU_FCAL                 ON  (1)
Bit 5:    SumV_FCAL                 ON  (1)
Bit 6:    UV_Asymmetry_FCAL         ON  (1)
Bit 7:    Saturation_FCAL           OFF (0)
Bit 8:    (Reserved)                OFF (0)
Bit 9:    EoverPmeas_FCAL           OFF (0)

Result: 7 active features for training
```

The system automatically generates `vars.ini` files with:
- `[training_plus]`: Active features for positive tracks
- `[training_minus]`: Active features for negative tracks  
- `[spectators]`: Inactive features (preserved but not trained on)

### 3. Training Execution

**Job Script Flow**:

```bash
#!/bin/bash
# run_job.sh (auto-generated)

# 1. Setup environment
source /group/halld/Software/.../gluex_env_boot_jlab.sh

# 2. Export job configuration
export TMVA_SIGNAL_FILE=signal_combined_4M.root
export TMVA_BACKGROUND_FILE=background_OneModel_4M.root
export TMVA_TRAIN_EVENTS=500000

# 3. Run TMVA training
root -l -b -q '../macros/training/TMVAClassification.C("MLP,BDT")'
```

**TMVA Training Pipeline** (TMVAClassification.C):

1. **Load Data**:
   - Signal: electron-positron events
   - Background: pion events
   - 80/20 train/test split

2. **Parse Configuration**:
   - Read `vars.ini` for feature selection
   - Apply hyperparameters from job config

3. **Create TMVA Factory**:
   ```cpp
   TMVA::Factory *factory = new TMVA::Factory("TMVAClassification", ...);
   TMVA::DataLoader *dataloader = new TMVA::DataLoader("dataset");
   
   // Add variables from [training_plus/minus] sections
   dataloader->AddVariable("EoverPkin_FCAL", ...);
   dataloader->AddVariable("TrackFCAL_DOCA", ...);
   // ... etc
   
   // Add spectators (not used for training)
   dataloader->AddSpectator("RunNumber", ...);
   ```

4. **Train Models**:
   - **MLP**: Multi-layer perceptron with configurable architecture
     ```cpp
     factory->BookMethod(dataloader, TMVA::Types::kMLP, "MLP",
       "H:!V:NeuronType=tanh:NCycles=200:HiddenLayers=N+5:...");
     ```
   
   - **BDT**: Boosted decision trees
     ```cpp
     factory->BookMethod(dataloader, TMVA::Types::kBDT, "BDT",
       "!H:!V:NTrees=400:MaxDepth=3:BoostType=AdaBoost:...");
     ```

5. **Output Results**:
   - Creates ROOT file: `out_mlp_Np5_nc200_bdt400_md3_featfcal_1111111000.root`
   - Contains histograms: `MVA_MLP_S`, `MVA_MLP_B`, `MVA_BDT_S`, `MVA_BDT_B`
   - Stores ROC curves: `MVA_MLP_rejBvsS`, `MVA_BDT_rejBvsS`

### 4. ROC AUC Extraction

**Script**: `src/extract_tmva_metrics.py`

**Process**:
1. Open TMVA output ROOT file
2. Navigate to method directories: `datasetPLUS/Method_MLP/MLP/`
3. Extract histograms:
   ```python
   # Signal and background distributions
   hist_sig = root_file.Get("datasetPLUS/Method_MLP/MLP/MVA_MLP_S")
   hist_bkg = root_file.Get("datasetPLUS/Method_MLP/MLP/MVA_MLP_B")
   
   # Compute ROC AUC via integration
   roc_auc = calculate_auc_from_histograms(hist_sig, hist_bkg)
   ```

4. Calculate additional metrics:
   - Separation: $\int \frac{(S(x) - B(x))^2}{S(x) + B(x)} dx$
   - Significance scans at various background fractions
   - Training time (from log files)

5. Output metrics JSON:
   ```json
   {
     "job_id": "job_0001",
     "roc_auc_mlp": 0.8750,
     "roc_auc_bdt": 0.8920,
     "separation_mlp": 0.4250,
     "separation_bdt": 0.5130,
     "training_time_sec": 127.34,
     "extraction_status": "success"
   }
   ```

### 5. Result Tracking (index.json)

**Central registry** of all jobs and results:

```json
{
  "metadata": {
    "creation_time": "2026-05-29T18:36:00Z",
    "total_jobs": 2,
    "search_type": "grid_search"
  },
  "jobs": [
    {
      "id": 1,
      "job_id": "job_0001",
      "hyperparameters": {
        "mlp_hidden": "N+5",
        "mlp_ncycles": "200",
        "bdt_ntrees": "400",
        "bdt_maxdepth": "3"
      },
      "features": {
        "feature_set": "fcal_1111111000",
        "feature_count": 7
      },
      "results": {
        "roc_auc_mlp": 0.8750,
        "roc_auc_bdt": 0.8920
      },
      "status": "completed"
    }
  ]
}
```

This enables:
- Progress tracking
- Performance ranking
- Hyperparameter analysis
- Result visualization

## Data Flow Diagram

```
RAW DATA (HDDM/ROOT)
        ↓
[DSelector C++ Classes]
  ├─ DSelector_2eMissingProton.C   (signal)
  └─ DSelector_pippim__B4.C        (background)
        ↓
FLAT TREES (event-level)
        ↓
[unroll_tracks.C]
  └─ Convert: event → 2 track entries
        ↓
TRAINING FILES (track-level)
  ├─ signal_combined_4M.root      (4.49M tracks)
  └─ background_OneModel_4M.root  (13.78M tracks)
        ↓
[TMVAClassification.C]
  ├─ Load data (80/20 split)
  ├─ Parse vars.ini
  ├─ Train MLP + BDT
  └─ Write output ROOT file
        ↓
TMVA OUTPUT (per job)
  └─ out_mlp_*.root
     ├─ MVA histograms
     └─ ROC curves
        ↓
[extract_tmva_metrics.py]
  ├─ Parse ROOT file
  ├─ Compute ROC AUC
  └─ Extract metrics
        ↓
RESULTS JSON
  └─ index.json (aggregated)
        ↓
[Analysis Scripts]
  ├─ summarize_all_hpo.py
  ├─ create_parallel_coords.py
  └─ Result tables/plots
```

## Key Files and Their Roles

### Python Orchestration
- **src/tmva_hpo_manager.py**: Main coordinator (2800 lines)
  - `generate_jobs()`: Create job directories
  - `run_jobs()`: Execute training (local or farm)
  - `extract_results()`: Parse TMVA outputs
  - `submit_to_swif2()`: Batch farm submission

- **src/extract_tmva_metrics.py**: ROC AUC calculator
  - `extract_roc_from_root()`: Parse ROOT files
  - `run_significance_scan()`: Compute significance metrics
  - `extract_tmva_metrics()`: Main entry point

### ROOT Macros
- **macros/training/TMVAClassification.C**: TMVA training
  - Configurable via environment variables
  - Supports MLP and BDT methods
  - Outputs MVA histograms and ROC curves

- **macros/training/TMVAClassificationApplication.C**: Apply trained models
  - Loads weight files from training
  - Applies to new data
  - Creates lightweight output

- **macros/data_prep/unroll_tracks.C**: Event → track conversion
  - Doubles entries (one per charge)
  - Creates generic branch names
  - Enables charge-agnostic training

- **macros/data_prep/skim_to_2M.C**: Training file reduction
  - Reduces large files for testing
  - Preserves tree structure
  - Configurable event count

### DSelectors (Data Preparation)
- **data_prep/epem_pippim/DSelector_2eMissingProton.C**: Signal extraction
  - Process: γp → e+e-p
  - Extracts FCAL features for electrons
  - Creates flat training trees

- **data_prep/epem_pippim/DSelector_pippim__B4.C**: Background extraction
  - Process: Hadronic reactions with π+π-
  - Extracts FCAL features for pions
  - Uses real data (not simulation)

- **data_prep/epem_pippim/README.md**: Study-specific documentation
  - Run information (RCDB queries, run ranges)
  - Selection cuts and efficiencies
  - Usage instructions

- **data_prep/README.md**: Data prep framework overview
  - Extensibility for multiple physics studies
  - Guidelines for adding new studies

### Configuration
- **config/vars.ini.template**: Feature configuration template
  - Sections: training_plus, training_minus, spectators
  - Branch names for ROOT trees
  - TMVA method options

- **config/grid_configs/*.json**: Hyperparameter grids
  - minimal.json: 2 jobs (smoke test)
  - medium.json: 48 jobs (validation)
  - comprehensive.json: 8640 jobs (production)

## Execution Modes

### Local Execution
```bash
python bin/tmva-hpo run --local
```
- Runs jobs sequentially on current machine
- Good for testing small grids
- Limited by single-machine resources

### Batch Farm (SWIF2)
```bash
python bin/tmva-hpo submit --workflow my_study --ram 8GB --time 4h
```
- Submits to JLab batch farm
- Parallel execution across many nodes
- Automatic resource management
- Job monitoring via SWIF2 commands

## Performance Considerations

### Memory Usage
- **Training**: 4-16 GB RAM depending on:
  - Training sample size (100K to 1.5M events)
  - Number of features (3 to 10)
  - BDT tree count (400 to 1200)

### Disk Usage
- **Per job**: ~100-500 MB
  - TMVA output ROOT file: ~50-200 MB
  - Weight files: ~1-10 MB
  - Logs: ~1-5 MB

- **Full study (8640 jobs)**: ~1-4 TB total

### Runtime
- **Per job**: 5-30 minutes depending on:
  - Training events: 100K (5 min) to 1.5M (30 min)
  - MLP cycles: 200 (fast) to 600 (slow)
  - BDT trees: 400 (fast) to 1200 (slow)

- **Full study**: 2-8 hours with 500+ parallel farm nodes

## Integration Points

### Extending to New Physics Processes

1. **Create DSelectors** for your signal/background
2. **Modify vars.ini.template** with your feature branches
3. **Update training file paths** in tmva_hpo_manager.py
4. **Generate grid** matching your parameter space
5. **Run and analyze** using existing tools

### Adding New TMVA Methods

1. **Edit TMVAClassification.C** to book new method:
   ```cpp
   factory->BookMethod(dataloader, TMVA::Types::kYourMethod, "YourMethod",
                       "options:here");
   ```

2. **Update extract_tmva_metrics.py** to extract new method metrics

3. **Modify index.json schema** to include new metrics

### Custom Analysis Tools

The `scripts/analysis/` directory accepts custom scripts:
- Read from `jobs/index.json`
- Filter/sort by hyperparameters
- Create custom visualizations
- Export to your preferred format

## Troubleshooting Architecture

### Common Issues

**"Cannot open ROOT file"**
- Check paths in job config
- Verify training files exist
- Ensure ROOT environment loaded

**"ROC AUC extraction failed"**
- TMVA output may be incomplete
- Check job logs for training errors
- Verify ROOT file structure with TBrowser

**"Jobs stuck in running state"**
- Check farm node status (SWIF2)
- Monitor resource usage (RAM, disk)
- Review error logs in job directories

## Further Reading

- [QUICKSTART.md](../QUICKSTART.md) - Get started quickly
- [NAMING_CONVENTIONS.md](../NAMING_CONVENTIONS.md) - File/feature naming
- [TUTORIAL.md](TUTORIAL.md) - Detailed walkthrough
- [BATCH_FARM_USAGE.md](BATCH_FARM_USAGE.md) - SWIF2 details
- [FAQ.md](FAQ.md) - Common questions

---

**Last Updated**: May 29, 2026
