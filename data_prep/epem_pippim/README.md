# Electron-Pion Separation Study (e+e- vs π+π-)

This study prepares training data for MVA-based particle identification to separate electron tracks from pion tracks in the GlueX/CPP experiment's Forward Calorimeter (FCAL).

## Physics Motivation

**CPP Experiment Goal**: Measure Charged Pion Polarizability via γp → π+π-p reaction

**Challenge**: Electron contamination from:
- γp → e+e-p (Bethe-Heitler, timelike Compton scattering)
- Pair production in material
- Misidentified electromagnetic showers

**Solution**: Train TMVA classifiers (MLP, BDT) to distinguish e± from π± using FCAL shower properties.

---

## Data Preparation Overview

DSelectors extract training samples from Hall-D REST data, producing flat ROOT trees with FCAL features suitable for TMVA training.

### Data Periods
- **Spring 2018 Production** (Run 40856-41384)
- Beam energy: 8.2-8.8 GeV
- Statistics: ~4M electron tracks + ~4M pion tracks
- See [run_info.txt](run_info.txt) for details

---

## DSelector Classes

### Signal: DSelector_2eMissingProton.C/.h

**Purpose**: Extract electron tracks from exclusive e+e- production

**Reaction**: γp → e+e-p (γp → ee[Recoil Proton])

**Selection Cuts**:
- Exclusivity cuts on missing mass
- Good kinematic fit (confidence level > 0.01)
- FCAL energy > 0.5 GeV
- Both e+ and e- detected

**Output**: ROOT tree with electron track features (`signal_combined_4M.root`)

### Background: DSelector_pippim__B4.C/.h

**Purpose**: Extract pion tracks from hadronic reactions

**Reaction**: Various hadronic channels producing π+π- pairs (primarily γp → π+π-p)

**Selection Cuts**:
- Good kinematic fit
- FCAL energy > 0.5 GeV
- Forward-going tracks
- Charged track PID consistent with pions

**Output**: ROOT tree with pion track features (`background_OneModel_4M.root`)

---

## Output Format

Both DSelectors produce flat ROOT trees with one entry per track (after post-processing with `unroll_tracks.C`).

### FCAL Features Extracted

- **Energy/Momentum**:
  - `EoverPkin_FCAL` - E/p from kinematic fit
  - `EoverPmeas_FCAL` - E/p from measured momentum
  
- **Shower Shape**:
  - `E9E25_FCAL` - Energy ratio (3×3 / 5×5 cells)
  - `E1E9_FCAL` - Energy ratio (1×1 / 3×3 cells)
  - `SumU_FCAL`, `SumV_FCAL` - FCAL coordinates
  - `UV_Asymmetry_FCAL` - Asymmetry in U-V coordinates
  
- **Track Matching**:
  - `TrackFCAL_DOCA` - Distance of closest approach
  
- **Saturation**:
  - `Saturation_FCAL` - High-energy saturation flag

**Note**: DSelectors initially output `Positive_*` and `Negative_*` branches for each charge. Use `macros/data_prep/unroll_tracks.C` to create charge-agnostic training data.

---

## Usage

### Running DSelectors on Hall-D Data

1. **Setup GlueX environment**:
   ```bash
   source /group/halld/Software/build_scripts/gluex_env_boot_jlab.sh
   module load root
   ```

2. **Run DSelector on data files**:
   ```bash
   # Signal (e+e-)
   root -l tree_eMissingProton_file.root
   .x DSelector_2eMissingProton.C+
   .q
   
   # Background (π+π-)
   root -l tree_pippim_file.root
   .x DSelector_pippim__B4.C+
   .q
   ```

3. **Batch processing** (many files):
   - Use SWIF2 workflow system for farm submission
   - Process ~500 signal files + ~800 background files
   - Total wall time: ~8-12 hours

---

## Post-Processing Workflow

After running DSelectors:

1. **Unroll tracks** (event-level → track-level):
   ```bash
   root -l -b -q '../../macros/data_prep/unroll_tracks.C("signal_raw.root", "signal_combined_4M.root")'
   root -l -b -q '../../macros/data_prep/unroll_tracks.C("background_raw.root", "background_OneModel_4M.root")'
   ```

2. **Optional: Skim for testing**:
   ```bash
   root -l -b -q '../../macros/data_prep/skim_to_2M.C("signal_combined_4M.root", "TreeName", "signal_2M.root", 2000000)'
   ```

3. **Configure features** in `config/vars.ini`:
   ```ini
   [training_plus]
   Positive_EoverPkin_FCAL
   Positive_TrackFCAL_DOCA
   Positive_E9E25_FCAL
   ...
   ```

4. **Train TMVA models**:
   ```bash
   root -l -b -q 'macros/training/TMVAClassification.C'
   ```

---

## Known Issues & Notes

- **Charge-dependent branches**: DSelectors output `Positive_*` and `Negative_*` branches. Use `unroll_tracks.C` to create charge-agnostic training data.
- **Missing momentum**: Some tracks fail kinematic fit → `EoverPkin_FCAL = -1` sentinel value
- **FCAL saturation**: High-energy showers may saturate → affects E/p measurements
- **Run-dependent acceptance**: FCAL acceptance varies with run period (see `run_info.txt`)

---

## Alternative Approaches

If this data preparation strategy doesn't meet your needs, consider creating a parallel study directory:

- `data_prep/epem_pippim_tight_cuts/` - More stringent selection cuts
- `data_prep/epem_pippim_loose_cuts/` - Looser cuts for efficiency studies
- `data_prep/epem_pippim_mc/` - Monte Carlo-based training

Each approach gets its own directory with corresponding DSelectors and documentation.

---

**See also**:
- Data prep overview: [../README.md](../README.md)
- Main project README: [../../README.md](../../README.md)
- Run information: [run_info.txt](run_info.txt)
- TMVA training macros: [../../macros/training/](../../macros/training/)
- Feature configuration: [../../config/vars.ini.template](../../config/vars.ini.template)

DSelectors produce flat ROOT trees with:
- One entry per event (event-level features)
- Separate branches for Positive and Negative tracks
- All FCAL features ready for TMVA training

### Post-Processing

After DSelector output, use `unroll_tracks.C` to convert event-level trees to track-level trees:

```bash
root -l -b -q 'unroll_tracks.C("signal_events.root", "TreeName", "signal_tracks.root")'
```

This doubles the entries (one per track) and creates generic branch names for charge-agnostic training.

## Adapting for Your Data

To create DSelectors for your own physics process:

1. **Start from template**: Copy one of the provided DSelectors
2. **Modify event selection**: Update kinematic cuts in `Init()` and `Process()`
3. **Add/remove branches**: Modify `Init()` to create your desired output branches
4. **Fill branches**: Update `Process()` to compute and fill your features
5. **Compile and test**: Run on small sample, verify output tree structure

## Key Methods

- `Init()`: Define output tree branches
- `Process()`: Event loop, apply cuts, fill branches
- `Finalize()`: Close files, print summary

## Tips

- **Memory management**: DSelectors handle large datasets, monitor RAM usage
- **ROOT version**: Use GlueX-compatible ROOT version (6.x)
- **Debugging**: Use `cout` statements in `Process()` to trace execution
- **Performance**: DSelectors parallelize naturally over input files

## Support

For questions about DSelectors in the CPP analysis:
- See GlueX wiki: https://halldweb.jlab.org/wiki/
- Contact: acschick@jlab.org
