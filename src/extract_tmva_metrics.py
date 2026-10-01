#!/usr/bin/env python3
"""
Extract TMVA evaluation metrics from training output ROOT files.

This script parses TMVA output files to extract key performance metrics:
- ROC AUC (Area Under ROC Curve)
- Separation
- Signal efficiency at background rejection thresholds
- Training time
- Model size

Usage:
    python extract_tmva_metrics.py <output_root_file> [--side PLUS|MINUS]
    python extract_tmva_metrics.py jobs/job_0001/out_*_PLUS.root
"""

import argparse
import json
import sys
from pathlib import Path
import subprocess
import re


def run_significance_scan_on_root(root_file: str, side: str, n_signal: float, bg_fracs: list) -> dict:
    """Run a ROOT macro to compute significance scans for MLP and BDT if histograms exist.

    Returns a dict with entries for 'MLP' and 'BDT' (each a list of scan results).
    """
    results = {}

    # Prepare ROOT macro (simple, explicit C-style code to avoid escaping issues)
    import tempfile
    fracs_str = ",".join([str(x) for x in bg_fracs])
    macro = """
{
    TFile* f = TFile::Open("__ROOT_FILE__");
    if (!f || f->IsZombie()) { printf("SIGSCAN_ERROR:Cannot open file\\n"); return; }
    TDirectory* dataset = (TDirectory*)f->Get("dataset__SIDE__");
    if (!dataset) { printf("SIGSCAN_ERROR:No dataset__SIDE__\\n"); f->Close(); return; }

    // MLP
    TDirectory* mlpdir = (TDirectory*)dataset->Get("Method_MLP/MLP");
    if (mlpdir) {
        TH1* hs = (TH1*)mlpdir->Get("MVA_MLP_S");
        TH1* hb = (TH1*)mlpdir->Get("MVA_MLP_B");
        if (hs && hb) {
            int nb = hs->GetNbinsX();
            double* s = new double[nb];
            double* b = new double[nb];
            double* cent = new double[nb];
            double stot=0, btot=0;
            for (int i=0;i<nb;i++) { s[i]=hs->GetBinContent(i+1); b[i]=hb->GetBinContent(i+1); cent[i]=hs->GetBinCenter(i+1); stot+=s[i]; btot+=b[i]; }
            double nsignal = __NSIGNAL__;
            double fracs[] = { __FRACS_STR__ };
            int nfr = sizeof(fracs)/sizeof(fracs[0]);
            for (int fi=0; fi<nfr; ++fi) {
                double frac = fracs[fi];
                double nbg = nsignal*frac;
                if (stot<=0 || btot<=0) { printf("SIGSCAN_MLP:FRAC=%g:EMPTY\\n", frac); continue; }
                double sf = nsignal / stot;
                double bf = nbg / btot;
                double best_sig=0; double best_cut=0; double bestS=0; double bestB=0;
                double* cumS = new double[nb];
                double* cumB = new double[nb];
                double cs=0, cb=0;
                for (int i=nb-1;i>=0;--i) { cs += s[i]*sf; cb += b[i]*bf; cumS[i]=cs; cumB[i]=cb; }
                for (int i=0;i<nb;i++) { double S=cumS[i]; double B=cumB[i]; double sig=0; if (S+B>0) sig = S / sqrt(S+B); if (sig>best_sig) { best_sig=sig; best_cut=cent[i]; bestS=S; bestB=B; } }
                printf("SIGSCAN_MLP:FRAC=%g:BEST_CUT=%g:BEST_SIG=%g:S=%g:B=%g\\n", frac, best_cut, best_sig, bestS, bestB);
                delete [] cumS; delete [] cumB;
            }
            delete [] s; delete [] b; delete [] cent;
        }
    }

    // BDT
    TDirectory* bdtdir = (TDirectory*)dataset->Get("Method_BDT/BDT");
    if (bdtdir) {
        TH1* hs = (TH1*)bdtdir->Get("MVA_BDT_S");
        TH1* hb = (TH1*)bdtdir->Get("MVA_BDT_B");
        if (hs && hb) {
            int nb = hs->GetNbinsX();
            double* s = new double[nb];
            double* b = new double[nb];
            double* cent = new double[nb];
            double stot=0, btot=0;
            for (int i=0;i<nb;i++) { s[i]=hs->GetBinContent(i+1); b[i]=hb->GetBinContent(i+1); cent[i]=hs->GetBinCenter(i+1); stot+=s[i]; btot+=b[i]; }
            double nsignal = __NSIGNAL__;
            double fracs[] = { __FRACS_STR__ };
            int nfr = sizeof(fracs)/sizeof(fracs[0]);
            for (int fi=0; fi<nfr; ++fi) {
                double frac = fracs[fi];
                double nbg = nsignal*frac;
                if (stot<=0 || btot<=0) { printf("SIGSCAN_BDT:FRAC=%g:EMPTY\\n", frac); continue; }
                double sf = nsignal / stot;
                double bf = nbg / btot;
                double best_sig=0; double best_cut=0; double bestS=0; double bestB=0;
                double* cumS = new double[nb];
                double* cumB = new double[nb];
                double cs=0, cb=0;
                for (int i=nb-1;i>=0;--i) { cs += s[i]*sf; cb += b[i]*bf; cumS[i]=cs; cumB[i]=cb; }
                for (int i=0;i<nb;i++) { double S=cumS[i]; double B=cumB[i]; double sig=0; if (S+B>0) sig = S / sqrt(S+B); if (sig>best_sig) { best_sig=sig; best_cut=cent[i]; bestS=S; bestB=B; } }
                printf("SIGSCAN_BDT:FRAC=%g:BEST_CUT=%g:BEST_SIG=%g:S=%g:B=%g\\n", frac, best_cut, best_sig, bestS, bestB);
                delete [] cumS; delete [] cumB;
            }
            delete [] s; delete [] b; delete [] cent;
        }
    }

    f->Close();
}
"""

    # substitute placeholders (avoid f-string brace escaping complexities)
    macro = macro.replace("__ROOT_FILE__", root_file).replace("__SIDE__", side).replace("__NSIGNAL__", str(float(n_signal))).replace("__FRACS_STR__", fracs_str)

    with tempfile.NamedTemporaryFile(mode='w', suffix='.C', delete=False) as tf:
        tf.write(macro)
        macro_file = tf.name
    # also write a persistent copy for debugging
    try:
        debug_dir = Path(__file__).resolve().parent / 'tmp'
        debug_dir.mkdir(exist_ok=True)
        (debug_dir / 'signif_macro.C').write_text(macro)
    except Exception:
        pass

    try:
        cmd = f"gxenv && root -l -b -q {macro_file}"
        proc = subprocess.run(cmd, shell=True, capture_output=True, text=True, executable='/bin/tcsh')
        if proc.returncode != 0:
            return {"error": proc.stderr}

        # Parse output lines
        out = proc.stdout.splitlines()
        for line in out:
            if line.startswith("SIGSCAN_MLP") or line.startswith("SIGSCAN_BDT"):
                # Format: SIGSCAN_TAG:FRAC=val:BEST_CUT=...:BEST_SIG=...:S=...:B=...
                parts = line.split(':')
                tagpart = parts[0].split('_')[-1]
                # build entry
                entry = {}
                for p in parts[1:]:
                    if '=' in p:
                        k, v = p.split('=', 1)
                        try:
                            entry[k.strip()] = float(v)
                        except Exception:
                            entry[k.strip()] = v
                results.setdefault(tagpart, []).append(entry)

    finally:
        Path(macro_file).unlink(missing_ok=True)

    return results


def run_significance_scan_if_available(job_path: Path) -> dict:
    """Run scans for PLUS and MINUS files in a job dir using default parameters."""
    res = {}
    plus = list(job_path.glob("*_PLUS.root"))
    minus = list(job_path.glob("*_MINUS.root"))
    fracs = [0.01, 0.05, 0.1, 0.2]
    nsignal = 1000.0
    if plus:
        r = run_significance_scan_on_root(str(plus[0]), "PLUS", nsignal, fracs)
        if r:
            res['PLUS'] = r
    if minus:
        r = run_significance_scan_on_root(str(minus[0]), "MINUS", nsignal, fracs)
        if r:
            res['MINUS'] = r
    return res


def extract_metrics_with_root(root_file: str, side: str = "PLUS") -> dict:
    """Extract TMVA metrics using ROOT macro.
    
    Args:
        root_file: Path to TMVA output ROOT file
        side: Which dataset to analyze (PLUS or MINUS)
        
    Returns:
        Dictionary with extracted metrics
    """
    metrics = {
        "root_file": root_file,
        "side": side,
        "roc_auc_mlp": None,
        "roc_auc_bdt": None,
        "separation_mlp": None,
        "separation_bdt": None,
        "extraction_status": "unknown"
    }
    
    # Create ROOT macro to extract metrics
    macro = f"""
    {{
        TFile* f = TFile::Open("{root_file}");
        if (!f || f->IsZombie()) {{
            std::cout << "ERROR:Cannot open file" << std::endl;
            return;
        }}
        
        // Navigate to dataset directory
        TDirectory* dataset = (TDirectory*)f->Get("dataset{side}");
        if (!dataset) {{
            std::cout << "ERROR:No dataset{side} found" << std::endl;
            f->Close();
            return;
        }}
        
        std::cout << "DATASET:{side}" << std::endl;
        
        // Extract MLP metrics
        TDirectory* mlp_dir = (TDirectory*)dataset->Get("Method_MLP/MLP");
        if (mlp_dir) {{
            // Try to get ROC curve
            TH1F* roc_mlp = (TH1F*)mlp_dir->Get("MVA_MLP_rejBvsS");
            if (roc_mlp) {{
                // Calculate AUC = integral of background rejection over signal efficiency
                // rejBvsS has signal efficiency on X-axis, background rejection on Y-axis
                double auc = 0.0;
                int nbins = roc_mlp->GetNbinsX();
                for (int i = 2; i <= nbins; i++) {{
                    double sig_eff_prev = roc_mlp->GetBinCenter(i-1);
                    double sig_eff = roc_mlp->GetBinCenter(i);
                    double bkg_rej = roc_mlp->GetBinContent(i);
                    auc += bkg_rej * (sig_eff - sig_eff_prev);
                }}
                std::cout << "MLP_ROC_AUC:" << auc << std::endl;
            }}
            
            // Try to get separation
            TH1F* sep_mlp = (TH1F*)mlp_dir->Get("MVA_MLP_S");
            TH1F* sep_mlp_b = (TH1F*)mlp_dir->Get("MVA_MLP_B");
            if (sep_mlp && sep_mlp_b) {{
                // Calculate separation
                double separation = 0.0;
                int nbins = sep_mlp->GetNbinsX();
                for (int i = 1; i <= nbins; i++) {{
                    double s = sep_mlp->GetBinContent(i);
                    double b = sep_mlp_b->GetBinContent(i);
                    if (s + b > 0) {{
                        separation += 0.5 * (s - b) * (s - b) / (s + b) * sep_mlp->GetBinWidth(i);
                    }}
                }}
                std::cout << "MLP_SEPARATION:" << separation << std::endl;
            }}
        }}
        
        // Extract BDT metrics
        TDirectory* bdt_dir = (TDirectory*)dataset->Get("Method_BDT/BDT");
        if (bdt_dir) {{
            // Try to get ROC curve
            TH1F* roc_bdt = (TH1F*)bdt_dir->Get("MVA_BDT_rejBvsS");
            if (roc_bdt) {{
                // Calculate AUC = integral of background rejection over signal efficiency
                double auc = 0.0;
                int nbins = roc_bdt->GetNbinsX();
                for (int i = 2; i <= nbins; i++) {{
                    double sig_eff_prev = roc_bdt->GetBinCenter(i-1);
                    double sig_eff = roc_bdt->GetBinCenter(i);
                    double bkg_rej = roc_bdt->GetBinContent(i);
                    auc += bkg_rej * (sig_eff - sig_eff_prev);
                }}
                std::cout << "BDT_ROC_AUC:" << auc << std::endl;
            }}
            
            // Try to get separation
            TH1F* sep_bdt = (TH1F*)bdt_dir->Get("MVA_BDT_S");
            TH1F* sep_bdt_b = (TH1F*)bdt_dir->Get("MVA_BDT_B");
            if (sep_bdt && sep_bdt_b) {{
                // Calculate separation
                double separation = 0.0;
                int nbins = sep_bdt->GetNbinsX();
                for (int i = 1; i <= nbins; i++) {{
                    double s = sep_bdt->GetBinContent(i);
                    double b = sep_bdt_b->GetBinContent(i);
                    if (s + b > 0) {{
                        separation += 0.5 * (s - b) * (s - b) / (s + b) * sep_bdt->GetBinWidth(i);
                    }}
                }}
                std::cout << "BDT_SEPARATION:" << separation << std::endl;
            }}
        }}
        
        std::cout << "EXTRACTION:SUCCESS" << std::endl;
        f->Close();
    }}
    """
    
    # Write macro to temp file
    import tempfile
    with tempfile.NamedTemporaryFile(mode='w', suffix='.C', delete=False) as f:
        f.write(macro)
        macro_file = f.name
    
    try:
        # Run ROOT macro
        cmd = f"gxenv && root -l -b -q {macro_file}"
        result = subprocess.run(
            cmd,
            shell=True,
            capture_output=True,
            text=True,
            executable='/bin/tcsh'
        )
        
        # Parse output
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if ':' in line:
                    key, value = line.split(':', 1)
                    key = key.strip()
                    value = value.strip()
                    
                    if key == "MLP_ROC_AUC":
                        metrics["roc_auc_mlp"] = float(value)
                    elif key == "BDT_ROC_AUC":
                        metrics["roc_auc_bdt"] = float(value)
                    elif key == "MLP_SEPARATION":
                        metrics["separation_mlp"] = float(value)
                    elif key == "BDT_SEPARATION":
                        metrics["separation_bdt"] = float(value)
                    elif key == "EXTRACTION":
                        metrics["extraction_status"] = value
        else:
            metrics["extraction_status"] = "failed"
            metrics["error_message"] = result.stderr
    
    finally:
        # Clean up temp file
        Path(macro_file).unlink(missing_ok=True)
    
    return metrics


def extract_metrics_from_log(log_file: str) -> dict:
    """Extract metrics from training log file as fallback.
    
    Args:
        log_file: Path to training log file
        
    Returns:
        Dictionary with extracted metrics (limited info from log)
    """
    metrics = {
        "training_time_seconds": None,
        "n_signal_events": None,
        "n_background_events": None
    }
    
    if not Path(log_file).exists():
        return metrics
    
    try:
        with open(log_file, 'r') as f:
            content = f.read()
            
            # Extract training time
            time_match = re.search(r'Factory\s+:\s+Training.*took\s+([\d.]+)\s+sec', content)
            if time_match:
                metrics["training_time_seconds"] = float(time_match.group(1))
            
            # Extract event counts
            sig_match = re.search(r'Signal.*:\s+(\d+)\s+events', content)
            if sig_match:
                metrics["n_signal_events"] = int(sig_match.group(1))
                
            bkg_match = re.search(r'Background.*:\s+(\d+)\s+events', content)
            if bkg_match:
                metrics["n_background_events"] = int(bkg_match.group(1))
    
    except Exception as e:
        metrics["log_parse_error"] = str(e)
    
    return metrics


def extract_job_metrics(job_dir: str) -> dict:
    """Extract all metrics for a completed job.
    
    Args:
        job_dir: Path to job directory
        
    Returns:
        Complete metrics dictionary
    """
    job_path = Path(job_dir)
    
    # Find TMVA output files (with _PLUS and _MINUS suffixes)
    plus_files = list(job_path.glob("*_PLUS.root"))
    minus_files = list(job_path.glob("*_MINUS.root"))
    
    metrics = {
        "job_dir": str(job_dir),
        "plus_model": None,
        "minus_model": None,
        "log_metrics": None
    }
    
    # Extract PLUS model metrics
    if plus_files:
        metrics["plus_model"] = extract_metrics_with_root(str(plus_files[0]), "PLUS")
    
    # Extract MINUS model metrics  
    if minus_files:
        metrics["minus_model"] = extract_metrics_with_root(str(minus_files[0]), "MINUS")
    
    # Extract log metrics
    log_file = job_path / "run.log"
    if log_file.exists():
        metrics["log_metrics"] = extract_metrics_from_log(str(log_file))
    
    # Calculate combined scores
    if metrics["plus_model"] and metrics["minus_model"]:
        # Average ROC AUC across both models and both methods
        roc_aucs = []
        for model in [metrics["plus_model"], metrics["minus_model"]]:
            if model.get("roc_auc_mlp"):
                roc_aucs.append(model["roc_auc_mlp"])
            if model.get("roc_auc_bdt"):
                roc_aucs.append(model["roc_auc_bdt"])
        
        if roc_aucs:
            metrics["average_roc_auc"] = sum(roc_aucs) / len(roc_aucs)
            metrics["best_roc_auc"] = max(roc_aucs)
            metrics["worst_roc_auc"] = min(roc_aucs)

    # Perform significance scan (defaults) if histograms are available
    try:
        sig_scan = run_significance_scan_if_available(job_path)
        if sig_scan:
            metrics["significance_scan"] = sig_scan
    except Exception:
        pass
    
    return metrics


def main():
    parser = argparse.ArgumentParser(description='Extract TMVA metrics from output files')
    parser.add_argument('root_file', help='TMVA output ROOT file')
    parser.add_argument('--side', choices=['PLUS', 'MINUS'], default='PLUS',
                       help='Which dataset to analyze')
    parser.add_argument('--output', help='Output JSON file (default: print to stdout)')
    
    parser.add_argument('--signif-scan', action='store_true',
                       help='Also compute significance scan for several background fractions')
    parser.add_argument('--n-signal', type=float, default=1000.0,
                       help='Number of signal events to assume for significance scan')
    parser.add_argument('--bg-fracs', default='0.01,0.05,0.1,0.2',
                       help='Comma-separated background fractions (relative to signal) to scan')

    args = parser.parse_args()

    metrics = extract_metrics_with_root(args.root_file, args.side)

    # Optional significance scan
    if args.signif_scan:
        try:
            fracs = [float(x) for x in args.bg_fracs.split(',') if x]
            sigs = run_significance_scan_on_root(args.root_file, args.side, args.n_signal, fracs)
            if sigs:
                metrics['significance_scan'] = sigs
        except Exception as e:
            metrics['signif_scan_error'] = str(e)
    
    # Pretty print
    print(json.dumps(metrics, indent=2))
    
    # Save to file if requested
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(metrics, f, indent=2)
        print(f"\nMetrics saved to {args.output}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
