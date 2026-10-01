# TMVA HPO Naming Conventions & Jargon Guide

## ROOT File Naming Pattern

Example: `out_mlp_Np5_nc200_bdt100_md2_featfcal_1111111100_train20000_MINUS.root`

### Component Breakdown

| Component | Example | Meaning |
|-----------|---------|---------|
| **File Prefix** | `out_mlp` | Output file from MLP training |
| **Hidden Layer** | `Np5` | Network architecture: `N+5` = input features + 5 neurons |
| | `Np10` | `N+10` = input features + 10 neurons |
| | `Np15` | `N+15` = input features + 15 neurons |
| | `Np20` | `N+20` = input features + 20 neurons |
| **Training Cycles** | `nc200` | Number of cycles: 200, 300, 400, 600, etc. |
| **BDT Trees** | `bdt100` | BDT configuration: 100, 400, or 800 trees |
| **BDT Max Depth** | `md2` | Max depth: 2 or 3 |
| **Feature Set** | `featfcal_1111111100` | Binary code for FCAL feature selection (see below) |
| **Training Events** | `train20000` | Number of training events used |
| **Charge** | `MINUS` or `PLUS` | Particle charge polarity |

### Neuron Type Codes (nt)

| Code | Activation Function |
|------|---------------------|
| `nttanh` | Hyperbolic tangent |
| `ntsigmoid` | Sigmoid |
| `ntrelu` | ReLU |

### Variable Transform Codes (vt)

| Code | Transformation |
|------|----------------|
| `vtN` | None |
| `vtD` | Decorrelation |
| `vtP` | PCA |
| `vtG` | Gaussian |
| `vtDG` | Decorrelation + Gaussian |

## Feature Binary Codes (10 digits)

The 10-digit binary string (e.g., `1111111100`) indicates which FCAL variables are **enabled** (1) or **disabled** (0):

| Position | Variable | Description |
|----------|----------|-------------|
| **1** | `EoverPkin_FCAL` | **Kinematic fit E/p** - Energy over momentum from kinematic fit |
| **2** | `TrackFCAL_DOCA` | **Track-FCAL DOCA** - Distance of closest approach between track and FCAL |
| **3** | `E9E25_FCAL` | **E9/E25 ratio** - Energy in 3×3 / 5×5 block |
| **4** | `E1E9_FCAL` | **E1/E9 ratio** - Central crystal / 3×3 block energy |
| **5** | `SumU_FCAL` | **U channel sum** - Sum of energy in U readout channels |
| **6** | `SumV_FCAL` | **V channel sum** - Sum of energy in V readout channels |
| **7** | `UV_Asymmetry_FCAL` | **UV asymmetry** - (SumU - SumV) / (SumU + SumV) |
| **8** | `Saturation_FCAL` | **Saturation flag** - Indicator of ADC saturation |
| **9** | *(reserved)* | Not currently used |
| **10** | `EoverPmeas_FCAL` | **Measured E/p** - Energy over momentum from measured track (not kinfit) |

### Example Decodings

- `1111111100` → 8 features: All except reserved & measured E/p
- `1110000000` → 3 features: EoverPkin, DOCA, E9E25 only
- `1111111110` → 9 features: All except measured E/p
- `1111111111` → 10 features: All features enabled (including measured E/p)

## Model Architecture Notation

### MLP (Multi-Layer Perceptron)

- **N+5**: Number of neurons = number of input features + 5
  - Example: 8 features → 8+5 = 13 neurons
- **N+10**: Number of neurons = number of input features + 10
  - Example: 8 features → 8+10 = 18 neurons
- **N+20**: Number of neurons = number of input features + 20
  - Example: 8 features → 8+20 = 28 neurons

### BDT (Boosted Decision Tree)

- **NTrees**: 100, 400, or 800 trees
- **MaxDepth**: 2 or 3 levels deep

## Key Performance Metrics

| Metric | Description | Best = Higher |
|--------|-------------|---------------|
| **ROC AUC** | Area Under ROC Curve | Yes (closer to 1.0) |
| **Separation** | Signal/background separation | Yes (closer to 1.0) |

## Important Variable Distinctions

### Kinematic Fit vs Measured

- **EoverPkin_FCAL** (pos 1): Uses kinematic fit momentum (constrained by physics)
- **EoverPmeas_FCAL** (pos 10): Uses raw measured momentum (unconstrained)

The kinematic fit version generally provides better discrimination because it incorporates conservation laws and detector resolution.

## Best Model Results

From comprehensive HPO across all studies:

- **Best MLP**: ROC AUC = 0.9983
  - Architecture: N+20
  - Activation: sigmoid
  - Transform: G (Gaussian)
  - Cycles: 600
  - Training events: ~1.4M
  
- **Training Data Impact**: Performance increases significantly up to ~500k training events, then plateaus

## File Types

- `*.root` - TMVA output files with histograms and method results
- `*_mva_results.root` - Aggregated MVA results from both PLUS/MINUS
- `TMVApp.root` - TMVA application output
- `*.run_config.json` - Job configuration parameters
- `index.json` - Summary of all job results with extracted metrics
