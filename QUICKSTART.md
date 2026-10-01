# TMVA-HPO Quick Start Guide

Get up and running with TMVA hyperparameter optimization in 5 minutes.

## Prerequisites

- Python 3.7+
- ROOT 6.x with TMVA
- 500 MB disk space
- Example training files (or use your own)

## Step 1: Installation (1 minute)

```bash
cd /path/to/your/workspace
git clone https://github.com/yourusername/tmva-hpo.git
cd tmva-hpo
pip install -e .
```

## Step 2: Prepare Training Data (Optional)

If you have your own training ROOT files, skip this step. Otherwise, create minimal test files:

```bash
# Create 10K-event test files
cd macros/data_prep
root -l -b -q 'skim_to_2M.C("your_signal.root", "TreeName", "signal_test.root", 10000)'
root -l -b -q 'skim_to_2M.C("your_background.root", "TreeName", "background_test.root", 10000)'
```

## Step 3: Configure vars.ini (2 minutes)

Edit `config/vars.ini.template` to match your tree structure:

```ini
[training_plus]
# List your discriminating variables
Positive_EoverPkin_FCAL
Positive_TrackFCAL_DOCA
Positive_E9E25_FCAL

[training_minus]
# Same variables for negative tracks
Negative_EoverPkin_FCAL
Negative_TrackFCAL_DOCA
Negative_E9E25_FCAL

[spectators]
# Variables to keep but not train on
RunNumber
EventNumber
EventWeight
```

Save as `config/vars.ini` (remove `.template`).

## Step 4: Generate Jobs (30 seconds)

```bash
# Generate 2 test jobs (minimal configuration)
python bin/tmva-hpo generate --config config/grid_configs/minimal.json

# Check what was created
ls jobs/
# Should see: job_0001/ job_0002/ index.json
```

## Step 5: Run Jobs Locally (5 minutes)

```bash
# Run both jobs
python bin/tmva-hpo run --local

# Monitor progress (in another terminal)
tail -f jobs/job_0001/run.log
```

**Expected output**: 
- Each job trains MLP and BDT models
- Creates `out_mlp_*.root` output files
- Takes ~2-5 minutes per job

## Step 6: Extract Metrics (30 seconds)

```bash
# Extract ROC AUC from TMVA outputs
python bin/tmva-hpo extract-metrics

# View results
python bin/tmva-hpo status
```

**Expected output**:
```
Job Summary:
job_0001: ROC_MLP=0.875, ROC_BDT=0.892 [completed]
job_0002: ROC_MLP=0.881, ROC_BDT=0.895 [completed]

Best MLP: job_0002 (0.881)
Best BDT: job_0002 (0.895)
```

## Step 7: Analyze Results (Optional)

```bash
# Create summary plots
python scripts/analysis/summarize_all_hpo.py

# View index.json for full details
cat jobs/index.json | jq '.jobs[] | {job_id, roc_auc_mlp, roc_auc_bdt}'
```

---

## What Just Happened?

1. **Generated 2 jobs** with different MLP architectures (N+5 vs N+10 hidden neurons)
2. **Trained 4 models**: 2 MLP + 2 BDT (one set per job)
3. **Extracted ROC AUC** from TMVA histogram outputs
4. **Tracked results** in `jobs/index.json` for analysis

## Next Steps

### Scale Up to Medium Test (48 jobs)

```bash
python bin/tmva-hpo generate --config config/grid_configs/medium.json
python bin/tmva-hpo run --local  # Or use --submit for batch farm
```

### Submit to Batch Farm (SWIF2)

```bash
python bin/tmva-hpo submit --workflow my_hpo_study --ram 8GB --time 4h
```

Monitor with:
```bash
swif2 status -workflow my_hpo_study
```

### Customize Your Grid

Edit `config/grid_configs/medium.json`:

```json
{
  "mlp_hidden": ["N+5", "N+10", "N+15", "N+20"],
  "mlp_ncycles": ["100", "200", "400"],
  "bdt_ntrees": ["200", "400", "800"],
  "feature_sets": ["fcal_1111111000", "fcal_1110000000"]
}
```

Total jobs = 4 × 3 × 3 × 2 = 72 jobs

### Analyze Feature Importance

Run comprehensive grid with feature ablation:
```bash
python bin/tmva-hpo generate --config config/grid_configs/comprehensive.json
```

This tests:
- Core 3 features vs all 8 features
- Leave-one-out ablation
- Each feature's individual contribution

---

## Troubleshooting

### "ROOT not found"
```bash
# Setup GlueX environment (if using JLab)
source /group/halld/Software/build_scripts/gluex_env_boot_jlab.sh

# Or setup ROOT manually
source /path/to/root/bin/thisroot.sh
```

### "Training file not found"
- Check paths in `src/tmva_hpo_manager.py` (search for "signal_combined" and "background")
- Update to point to your training files
- Or set environment variables:
  ```bash
  export TMVA_SIGNAL_FILE=/path/to/signal.root
  export TMVA_BACKGROUND_FILE=/path/to/background.root
  ```

### "Job failed with exit code 134"
- Usually means out-of-memory
- Reduce training events in grid config: `"train_events": ["100000"]`
- Or increase RAM allocation: `--ram 16GB`

### "ROC AUC = 0.5 (random)"
- Check that signal and background files are different
- Verify vars.ini has discriminating variables
- Inspect TMVA output plots in `jobs/job_XXXX/datasetPLUS/`

---

## Learn More

- [TUTORIAL.md](docs/TUTORIAL.md) - Detailed walkthrough with explanations
- [SYSTEM_ARCHITECTURE.md](docs/SYSTEM_ARCHITECTURE.md) - How everything connects
- [FAQ.md](docs/FAQ.md) - Common questions and answers
- [BATCH_FARM_USAGE.md](docs/BATCH_FARM_USAGE.md) - Advanced farm submission

## Success Indicators

✅ You successfully completed the quickstart if:
1. `jobs/index.json` was created with 2 jobs
2. Both jobs show `"status": "completed"`
3. ROC AUC values are between 0.5 (random) and 1.0 (perfect)
4. Output ROOT files exist in job directories

Now you're ready for production-scale HPO! 🚀
