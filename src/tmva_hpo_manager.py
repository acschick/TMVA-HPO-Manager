#!/usr/bin/env python3
"""
TMVA Hyperparameter Optimization Manager

A central Python script to manage the entire TMVA HPO workflow, replacing
the previous tcsh-based system with a more maintainable and grad-student-friendly
Python approach.

Usage:
    python tmva_hpo_manager.py setup                    # Setup shared data files
    python tmva_hpo_manager.py generate [--grid-size S] # Generate job configurations
    python tmva_hpo_manager.py run [--job-id ID]        # Run jobs (all or specific)
    python tmva_hpo_manager.py status                   # Check job status
    python tmva_hpo_manager.py merge                    # Merge results
    python tmva_hpo_manager.py clean                    # Clean up job directories
    python tmva_hpo_manager.py analyze                  # Analyze results

Author: Generated for improved graduate student workflow
Date: October 2025
"""

import argparse
import json
import os
import shutil
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import glob
import itertools


class TMVAHPOManager:
    """Central manager for TMVA Hyperparameter Optimization workflow."""
    
    @staticmethod
    def get_job_id(job: dict) -> str:
        """Safely extract job_id from a job dict.
        
        Args:
            job: Job dictionary
            
        Returns:
            Job ID string
        """
        if 'job_id' in job:
            return job['job_id']
        elif 'id' in job:
            return f"job_{job['id']:04d}"
        else:
            raise ValueError(f"Job has neither 'job_id' nor 'id' field: {job}")
    
    def __init__(self, base_dir: str = None):
        """Initialize the manager.
        
        Args:
            base_dir: Base directory for the TMVA HPO system. Defaults to current directory.
        """
        self.base_dir = Path(base_dir) if base_dir else Path.cwd()
        self.jobs_dir = self.base_dir / "jobs"
        self.shared_data_dir = self.jobs_dir / "shared_data"
        self.scripts_dir = self.base_dir / "scripts"
        
        # New trial-based organization for future Optuna integration
        self.trials_dir = self.base_dir / "trials"
        self.optuna_studies_dir = self.base_dir / "optuna_studies"
        
        # Trial management
        self.current_trial = 1
        self.trial_mode = False  # False = legacy jobs/, True = trials/
        
        # Data file configurations
        self.data_files = {
            "pi0pippim": {
                "source": "/work/halld/home/acschick/channels/pi0pippim/FlatTree_pi0pippim_2018-01_ver02.root",
                "tree": "FLAT_pip_TMVA"
            },
            "gepem": {
                "source": "/work/halld/home/acschick/channels/gepem/gepem_DATA_FlatTrees_pi0CUT.root",
                "tree": "Flat2018_Data_gepem"
            }
        }
        
        # Training data configurations (signal and background)
        # Phase 4: Using 4M-event skimmed files for deep network testing (up to 3.5M)
        # Original files: signal 4.49M, background 13.78M
        # Skimmed files: signal 4M (~2.4GB), background 4M (~3.4GB)
        # Phase 3: Using 2M skims for faster testing
        # 
        # SINGLE-MODEL MODE (charge-agnostic, combined track-level data):
        # Signal: signal_combined_4M.root (4M tracks from e+e- events, generic branch names)
        # Background: background_OneModel_4M.root (4M tracks from pi+pi- events, generic branch names)
        self.training_files = {
            "signal": {
                "source": "/work/halld/home/acschick/tmva-hpo/signal_combined_4M.root",
                "tree": "Flat_FFS1_1801_BCFFN_epemmissp__B2_BestX2"
            },
            "background": {
                "source": "/work/halld/home/acschick/tmva-hpo/background_OneModel_4M.root",
                "tree": "combined_tracks"
            }
        }
        # Legacy dual-charge mode (2M skims):
        # "signal": {"source": "/work/halld/home/acschick/tmva-hpo/signal_skim_2M.root", "tree": "Flat_FFS1_qDATAq_epemmissp__B2_BestX2"}
        # "background": {"source": "/work/halld/home/acschick/tmva-hpo/background_skim_2M.root", "tree": "Flat_pippim__B4_BestX2"}
        
        # Default hyperparameter grid
        self.default_grid = {
            "mlp_hidden": ["N+5", "N+10", "N+15"],
            "mlp_ncycles": ["50", "100"],
            "mlp_testrate": ["5"],
            "bdt_ntrees": ["50", "100", "200"],
            "bdt_maxdepth": ["2", "3"],
            "feature_sets": ["baseline"],  # Will be generated based on FCAL combinations
            "train_events": ["500000"]  # Number of training events to use
        }
        
        # Training data event counts (from ROOT files)
        # Signal: 4,491,981 total events
        # Background: 13,782,376 total events
        # With 80/20 split:
        #   Signal: 3,593,585 train, 898,396 test
        #   Background: 11,025,901 train, 2,756,475 test
        
        # FCAL Feature Definitions (10-bit encoding)
        # ============================================
        # Feature sets are encoded as 10-bit binary strings (e.g., "fcal_1111111000")
        # where each bit position controls whether that feature is used in training.
        # 
        # Bit positions (read left-to-right):
        #   Bit 0: EoverPkin_FCAL       (E/p kinematic fit)
        #   Bit 1: TrackFCAL_DOCA       (Distance of closest approach)
        #   Bit 2: E9E25_FCAL           (Energy ratio 9/25 crystals)
        #   Bit 3: E1E9_FCAL            (Energy ratio 1/9 crystals)
        #   Bit 4: SumU_FCAL            (Sum U coordinate)
        #   Bit 5: SumV_FCAL            (Sum V coordinate)
        #   Bit 6: UV_Asymmetry_FCAL    (U-V asymmetry)
        #   Bit 7: Saturation_FCAL      (Saturation indicator)
        #   Bit 8: (Reserved/unused)
        #   Bit 9: EoverPmeas_FCAL      (E/p measured - mutually exclusive with bit 0)
        #
        # Baseline configuration: "fcal_1111111000" (7 features: bits 0-6 ON)
        # 
        # Special rules:
        #   - Bit 9 (EoverPmeas) and Bit 0 (EoverPkin) are mutually exclusive
        #   - Energy_FCAL and P3_KinFit_Mag are NEVER used (too unrealistic)
        #   - Features with bit=0 automatically move to spectators
        # ============================================
        
        self.fcal_features = [
            # Bits 0-2: Core features (usually ON in baseline)
            ["Positive_EoverPkin_FCAL", "Negative_EoverPkin_FCAL"],          # Bit 0
            ["Positive_TrackFCAL_DOCA", "Negative_TrackFCAL_DOCA"],          # Bit 1
            ["Positive_E9E25_FCAL", "Negative_E9E25_FCAL"],                  # Bit 2
            # Bits 3-6: Standard optional features
            ["Positive_E1E9_FCAL", "Negative_E1E9_FCAL"],                    # Bit 3
            ["Positive_SumU_FCAL", "Negative_SumU_FCAL"],                    # Bit 4
            ["Positive_SumV_FCAL", "Negative_SumV_FCAL"],                    # Bit 5
            ["Positive_UV_Asymmetry_FCAL", "Negative_UV_Asymmetry_FCAL"],    # Bit 6
            # Bit 7: Additional optional
            ["Positive_Saturation_FCAL", "Negative_Saturation_FCAL"],        # Bit 7
            # Bit 8: Reserved (unused for now)
            None,                                                             # Bit 8
            # Bit 9: Special - EoverPmeas (mutually exclusive with EoverPkin)
            ["Positive_EoverPmeas_FCAL", "Negative_EoverPmeas_FCAL"],        # Bit 9
        ]
        
        # Features that are NEVER used in training (too unrealistic/trivial)
        self.fcal_excluded = [
            ["Positive_Energy_FCAL", "Negative_Energy_FCAL"],
            ["Positive_P3_KinFit_Mag", "Negative_P3_KinFit_Mag"]
        ]
    
    def setup_shared_data(self) -> bool:
        """Setup shared data files by copying to jobs/shared_data/ directory.
        
        Returns:
            True if successful, False otherwise.
        """
        print("=== Setting up shared data files ===")
        
        # Create shared data directory
        self.shared_data_dir.mkdir(parents=True, exist_ok=True)
        print(f"Created directory: {self.shared_data_dir}")
        
        success = True
        for name, config in self.data_files.items():
            source = Path(config["source"])
            target = self.shared_data_dir / source.name
            
            print(f"\nProcessing {name} data file:")
            print(f"  Source: {source}")
            print(f"  Target: {target}")
            
            if not source.exists():
                print(f"  ✗ Source file not found: {source}")
                success = False
                continue
            
            if target.exists():
                print(f"  → Already exists, skipping copy")
            else:
                try:
                    shutil.copy2(source, target)
                    print(f"  ✓ Copy successful")
                except Exception as e:
                    print(f"  ✗ Copy failed: {e}")
                    success = False
        
        if success:
            print("\n✓ Shared data setup completed successfully")
        else:
            print("\n✗ Some data files could not be copied")
        
        return True
    
    def create_trial_structure(self, trial_number: int) -> Path:
        """Create directory structure for a specific trial.
        
        Args:
            trial_number: Trial number to create.
            
        Returns:
            Path to trial directory.
        """
        trial_dir = self.trials_dir / f"trial_{trial_number:03d}"
        trial_dir.mkdir(parents=True, exist_ok=True)
        
        # Create subdirectories
        (trial_dir / "jobs").mkdir(exist_ok=True)
        
        return trial_dir
    
    def save_trial_config(self, trial_number: int, config: Dict):
        """Save trial configuration to JSON file.
        
        Args:
            trial_number: Trial number.
            config: Trial configuration dictionary.
        """
        trial_dir = self.create_trial_structure(trial_number)
        config_path = trial_dir / "trial_config.json"
        
        with open(config_path, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2)
    
    def load_trial_config(self, trial_number: int) -> Optional[Dict]:
        """Load trial configuration from JSON file.
        
        Args:
            trial_number: Trial number.
            
        Returns:
            Trial configuration dictionary or None if not found.
        """
        config_path = self.trials_dir / f"trial_{trial_number:03d}" / "trial_config.json"
        
        if not config_path.exists():
            return None
            
        with open(config_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    
    def get_current_trial_number(self) -> int:
        """Get the current trial number from study metadata.
        
        Returns:
            Current trial number.
        """
        # For now, return stored value. In future, read from Optuna study
        return self.current_trial
    
    def extract_tmva_results(self, job_dir: Path) -> Dict:
        """Extract TMVA results from pre-computed metrics.json file.
        
        This method reads the metrics.json file that was generated by 
        step 7 (metrics extraction and aggregation) of the job workflow.
        
        Args:
            job_dir: Path to job directory.
            
        Returns:
            Dictionary with extracted and averaged metrics.
        """
        metrics_file = job_dir / "metrics.json"
        
        if not metrics_file.exists():
            return {
                "status": "missing",
                "error": "metrics.json not found"
            }
        
        try:
            with open(metrics_file, 'r') as f:
                metrics = json.load(f)
            
            # Return the metrics with status
            metrics["status"] = "complete"
            return metrics
            
        except Exception as e:
            return {
                "status": "error",
                "error": f"Could not read metrics.json: {e}"
            }
    
    def check_trial_completion(self, trial_number: int) -> bool:
        """Check if all jobs in a trial are complete.
        
        Args:
            trial_number: Trial number to check.
            
        Returns:
            True if trial is complete, False otherwise.
        """
        trial_config = self.load_trial_config(trial_number)
        if not trial_config:
            return False
        
        jobs = trial_config.get("jobs", [])
        if not jobs:
            return False
        
        completed_count = 0
        for job in jobs:
            if job.get("status") in ["completed", "failed"]:
                completed_count += 1
        
        return completed_count == len(jobs)
    
    def aggregate_trial_results(self, trial_number: int) -> Dict:
        """Aggregate results from all jobs in a trial.
        
        Args:
            trial_number: Trial number to aggregate.
            
        Returns:
            Aggregated results dictionary.
        """
        trial_config = self.load_trial_config(trial_number)
        if not trial_config:
            return {"error": "Trial config not found"}
        
        jobs = trial_config.get("jobs", [])
        successful_results = []
        
        for job in jobs:
            if job.get("status") == "completed":
                job_dir = Path(job["job_dir"])
                if job_dir.exists():
                    results = self.extract_tmva_results(job_dir)
                    if results.get("status") == "complete":
                        successful_results.append(results)
        
        if not successful_results:
            return {"error": "No successful jobs with results"}
        
        # Calculate aggregated metrics using the averaged values
        roc_auc_mlp_vals = [r["averaged_metrics"]["roc_auc_mlp"] for r in successful_results 
                            if r.get("averaged_metrics", {}).get("roc_auc_mlp") is not None]
        roc_auc_bdt_vals = [r["averaged_metrics"]["roc_auc_bdt"] for r in successful_results 
                            if r.get("averaged_metrics", {}).get("roc_auc_bdt") is not None]
        separation_mlp_vals = [r["averaged_metrics"]["separation_mlp"] for r in successful_results 
                               if r.get("averaged_metrics", {}).get("separation_mlp") is not None]
        separation_bdt_vals = [r["averaged_metrics"]["separation_bdt"] for r in successful_results 
                               if r.get("averaged_metrics", {}).get("separation_bdt") is not None]
        
        aggregated = {
            "trial_number": trial_number,
            "total_jobs": len(jobs),
            "successful_jobs": len(successful_results),
            "failed_jobs": len(jobs) - len(successful_results),
            "best_roc_auc_mlp": max(roc_auc_mlp_vals) if roc_auc_mlp_vals else None,
            "mean_roc_auc_mlp": sum(roc_auc_mlp_vals) / len(roc_auc_mlp_vals) if roc_auc_mlp_vals else None,
            "best_roc_auc_bdt": max(roc_auc_bdt_vals) if roc_auc_bdt_vals else None,
            "mean_roc_auc_bdt": sum(roc_auc_bdt_vals) / len(roc_auc_bdt_vals) if roc_auc_bdt_vals else None,
            "mean_separation_mlp": sum(separation_mlp_vals) / len(separation_mlp_vals) if separation_mlp_vals else None,
            "mean_separation_bdt": sum(separation_bdt_vals) / len(separation_bdt_vals) if separation_bdt_vals else None,
            "aggregation_time": time.strftime('%Y-%m-%d %H:%M:%S')
        }
        
        return aggregated
    
    def collect_metrics(self, job_id: Optional[str] = None) -> Dict:
        """Collect metrics from completed jobs.
        
        This method reads pre-computed metrics JSON files from job directories
        and returns them in an organized structure.
        
        Args:
            job_id: Specific job to collect metrics from. If None, collects from all jobs.
            
        Returns:
            Dictionary with metrics for each job.
        """
        print("=== Collecting metrics from jobs ===")
        
        # Load job index
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("✗ No job index found.")
            return {"error": "No job index found"}
        
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        # Handle both old and new JSON formats
        if isinstance(index_data, list):
            job_index = index_data
        else:
            job_index = index_data.get('jobs', [])
        
        # Filter jobs if specific job_id requested
        if job_id:
            jobs_to_collect = []
            for job in job_index:
                current_job_id = TMVAHPOManager.get_job_id(job)
                if current_job_id == job_id:
                    jobs_to_collect.append(job)
            if not jobs_to_collect:
                print(f"✗ Job {job_id} not found")
                return {"error": f"Job {job_id} not found"}
        else:
            jobs_to_collect = job_index
        
        # Collect metrics from each job
        all_metrics = {}
        successful_count = 0
        failed_count = 0
        
        for job in jobs_to_collect:
            job_id_str = TMVAHPOManager.get_job_id(job)
            job_dir = Path(job['job_dir'])
            
            if not job_dir.exists():
                print(f"  ✗ {job_id_str}: directory not found")
                failed_count += 1
                continue
            
            # Extract metrics using the existing method
            metrics = self.extract_tmva_results(job_dir)
            
            if metrics.get("status") == "complete":
                all_metrics[job_id_str] = {
                    "job_config": {
                        "mlp_hidden": job.get("mlp_hidden"),
                        "mlp_ncycles": job.get("mlp_ncycles"),
                        "bdt_ntrees": job.get("bdt_ntrees"),
                        "bdt_maxdepth": job.get("bdt_maxdepth"),
                        "feature_set": job.get("feature_set")
                    },
                    "metrics": metrics
                }
                successful_count += 1
                print(f"  ✓ {job_id_str}: metrics collected")
            else:
                all_metrics[job_id_str] = {
                    "job_config": {
                        "mlp_hidden": job.get("mlp_hidden"),
                        "mlp_ncycles": job.get("mlp_ncycles"),
                        "bdt_ntrees": job.get("bdt_ntrees"),
                        "bdt_maxdepth": job.get("bdt_maxdepth"),
                        "feature_set": job.get("feature_set")
                    },
                    "metrics": metrics,
                    "error": f"Status: {metrics.get('status')}"
                }
                failed_count += 1
                print(f"  ✗ {job_id_str}: {metrics.get('status')}")
        
        print(f"\nSummary: {successful_count} successful, {failed_count} failed/incomplete")
        
        return {
            "total_jobs": len(jobs_to_collect),
            "successful": successful_count,
            "failed": failed_count,
            "collection_time": time.strftime('%Y-%m-%d %H:%M:%S'),
            "jobs": all_metrics
        }
    
    def update_index_with_results(self) -> bool:
        """Update jobs/index.json with results from completed job metrics.
        
        Reads metrics.json from each job directory and writes averaged_metrics
        back to the index.json results field.
        
        Returns:
            True if successful
        """
        print("=== Updating index.json with results ===")
        
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("✗ No job index found")
            return False
        
        # Load current index
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        jobs = index_data.get('jobs', [])
        updated_count = 0
        
        for job in jobs:
            job_id = TMVAHPOManager.get_job_id(job)
            job_dir = Path(job['job_dir'])
            
            # Check for local .status file first (written by running jobs)
            status_file = job_dir / ".status"
            if status_file.exists():
                try:
                    with open(status_file, 'r') as f:
                        status_data = json.load(f)
                    job['status'] = status_data.get('status', job.get('status'))
                    job['last_updated'] = status_data.get('last_updated', time.strftime('%Y-%m-%d %H:%M:%S'))
                except:
                    pass  # Ignore corrupt .status files
            
            # Extract metrics
            metrics = self.extract_tmva_results(job_dir)
            
            if metrics.get("status") == "complete":
                # Handle both old format (averaged_metrics) and new MINUS-only format (minus_model)
                if "averaged_metrics" in metrics:
                    averaged = metrics.get("averaged_metrics", {})
                    roc_auc_mlp = averaged.get("roc_auc_mlp")
                    roc_auc_bdt = averaged.get("roc_auc_bdt")
                    separation_mlp = averaged.get("separation_mlp")
                    separation_bdt = averaged.get("separation_bdt")
                elif "minus_model" in metrics:
                    # MINUS-only format
                    minus_model = metrics.get("minus_model", {})
                    roc_auc_mlp = minus_model.get("roc_auc_mlp")
                    roc_auc_bdt = None
                    separation_mlp = minus_model.get("separation_mlp")
                    separation_bdt = None
                else:
                    # Unknown format
                    roc_auc_mlp = None
                    roc_auc_bdt = None
                    separation_mlp = None
                    separation_bdt = None
                
                # Update results field in index
                job['results'] = {
                    "roc_auc_mlp": roc_auc_mlp,
                    "roc_auc_bdt": roc_auc_bdt,
                    "separation_mlp": separation_mlp,
                    "separation_bdt": separation_bdt,
                    "signal_efficiency": None,  # Could extract from ROC curve if needed
                    "background_rejection": None,
                    "training_time": None,
                    "model_size": None
                }
                job['status'] = 'completed'
                job['last_updated'] = time.strftime('%Y-%m-%d %H:%M:%S')
                updated_count += 1
                print(f"  ✓ Updated {job_id}")
            else:
                # If no metrics but has .status file, keep that status
                if not status_file.exists():
                    job['status'] = metrics.get('status', 'unknown')
                print(f"  ✗ Skipped {job_id}: {job['status']}")
        
        # Write updated index
        with open(index_path, 'w', encoding='utf-8') as f:
            json.dump(index_data, f, indent=2)
        
        print(f"\n✓ Updated {updated_count} jobs in index.json")
        return True
    
    def export_for_parallel_coords(self, output_file: str = "parallel_coords_data.json") -> bool:
        """Export data formatted for parallel coordinate plots.
        
        Creates separate exports for MLP and BDT with:
        - Decoded feature flags (one column per FCAL feature)
        - All hyperparameters
        - Performance metrics
        
        Args:
            output_file: Base name for output files (will create _mlp.json and _bdt.json)
            
        Returns:
            True if successful
        """
        print("=== Exporting data for parallel coordinate plots ===")
        
        # First update index with latest results
        self.update_index_with_results()
        
        # Load index
        index_path = self.jobs_dir / "index.json"
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        jobs = index_data.get('jobs', [])
        
        # Decode feature sets and build data rows
        mlp_data = []
        bdt_data = []
        
        feature_names = [
            "has_EoverPkin", "has_DOCA", "has_E9E25", "has_E1E9",
            "has_SumU", "has_SumV", "has_UV_Asymmetry", "has_Saturation",
            "reserved", "has_EoverPmeas"
        ]
        
        for job in jobs:
            if job.get('status') != 'completed':
                continue
            
            # Decode feature set
            feature_code = job.get('feature_set', 'fcal_0000000000')
            binary = feature_code.replace('fcal_', '')
            
            # Base row with common data
            base_row = {
                "job_id": job.get('job_id'),
                "num_features": sum(int(b) for b in binary),
            }
            
            # Add binary feature flags
            for i, fname in enumerate(feature_names):
                if i < len(binary):
                    base_row[fname] = int(binary[i])
                else:
                    base_row[fname] = 0
            
            # Add hyperparameters
            base_row["mlp_hidden"] = job.get('mlp_hidden')
            base_row["mlp_ncycles"] = int(job.get('mlp_ncycles', 0))
            base_row["bdt_ntrees"] = int(job.get('bdt_ntrees', 0))
            base_row["bdt_maxdepth"] = int(job.get('bdt_maxdepth', 0))
            
            # MLP row
            results = job.get('results', {})
            if results.get('roc_auc_mlp') is not None:
                mlp_row = base_row.copy()
                mlp_row["roc_auc"] = results['roc_auc_mlp']
                mlp_row["separation"] = results.get('separation_mlp')
                mlp_data.append(mlp_row)
            
            # BDT row
            if results.get('roc_auc_bdt') is not None:
                bdt_row = base_row.copy()
                bdt_row["roc_auc"] = results['roc_auc_bdt']
                bdt_row["separation"] = results.get('separation_bdt')
                bdt_data.append(bdt_row)
        
        # Write separate files for MLP and BDT
        base_path = Path(output_file)
        mlp_file = base_path.parent / f"{base_path.stem}_mlp{base_path.suffix}"
        bdt_file = base_path.parent / f"{base_path.stem}_bdt{base_path.suffix}"
        
        with open(mlp_file, 'w') as f:
            json.dump(mlp_data, f, indent=2)
        print(f"✓ Exported MLP data: {mlp_file} ({len(mlp_data)} jobs)")
        
        with open(bdt_file, 'w') as f:
            json.dump(bdt_data, f, indent=2)
        print(f"✓ Exported BDT data: {bdt_file} ({len(bdt_data)} jobs)")
        
        # Also export as CSV for easier plotting
        import csv
        
        mlp_csv = base_path.parent / f"{base_path.stem}_mlp.csv"
        bdt_csv = base_path.parent / f"{base_path.stem}_bdt.csv"
        
        if mlp_data:
            with open(mlp_csv, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=mlp_data[0].keys())
                writer.writeheader()
                writer.writerows(mlp_data)
            print(f"✓ Exported MLP CSV: {mlp_csv}")
        
        if bdt_data:
            with open(bdt_csv, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=bdt_data[0].keys())
                writer.writeheader()
                writer.writerows(bdt_data)
            print(f"✓ Exported BDT CSV: {bdt_csv}")
        
        return True
    
    def create_trial(self, hyperparameters: Dict) -> int:
        """Create a new trial with given hyperparameters.
        
        Args:
            hyperparameters: Dictionary of hyperparameters for this trial.
            
        Returns:
            Trial number for the created trial.
        """
        # Get next trial number
        trial_number = self.current_trial + 1
        self.current_trial = trial_number
        
        # Create trial configuration
        trial_config = {
            "trial_info": {
                "trial_number": trial_number,
                "creation_time": time.strftime('%Y-%m-%d %H:%M:%S'),
                "status": "created",
                "hyperparameters": hyperparameters
            },
            "jobs": [],
            "results": {
                "extracted": False,
                "aggregated": {}
            }
        }
        
        # Save trial configuration
        self.save_trial_config(trial_number, trial_config)
        
        print(f"Created trial {trial_number} with hyperparameters: {hyperparameters}")
        return trial_number
    
    def submit_trial_jobs(self, trial_number: int) -> bool:
        """Submit all jobs for a trial using existing generate/run workflow.
        
        Args:
            trial_number: Trial number to submit jobs for.
            
        Returns:
            True if all jobs submitted successfully.
        """
        trial_config = self.load_trial_config(trial_number)
        if not trial_config:
            print(f"Trial {trial_number} config not found")
            return False
        
        hyperparams = trial_config["trial_info"]["hyperparameters"]
        
        # Create trial-specific job directory
        trial_dir = self.trials_dir / f"trial_{trial_number:03d}"
        jobs_dir = trial_dir / "jobs"
        
        # Generate jobs with trial-specific hyperparameters
        # This would integrate with existing job generation logic
        print(f"Submitting jobs for trial {trial_number}")
        print(f"Hyperparameters: {hyperparams}")
        print(f"Jobs directory: {jobs_dir}")
        
        # Update trial status
        trial_config["trial_info"]["status"] = "running"
        trial_config["trial_info"]["submission_time"] = time.strftime('%Y-%m-%d %H:%M:%S')
        self.save_trial_config(trial_number, trial_config)
        
        return True
    
    def monitor_trial_completion(self, trial_number: int) -> Dict:
        """Monitor trial completion and extract results when ready.
        
        Args:
            trial_number: Trial number to monitor.
            
        Returns:
            Status dictionary with completion info and results.
        """
        if not self.check_trial_completion(trial_number):
            return {
                "complete": False,
                "message": f"Trial {trial_number} still running"
            }
        
        # Trial is complete - aggregate results
        results = self.aggregate_trial_results(trial_number)
        
        # Update trial config with results
        trial_config = self.load_trial_config(trial_number)
        if trial_config:
            trial_config["trial_info"]["status"] = "completed"
            trial_config["trial_info"]["completion_time"] = time.strftime('%Y-%m-%d %H:%M:%S')
            trial_config["results"]["extracted"] = True
            trial_config["results"]["aggregated"] = results
            self.save_trial_config(trial_number, trial_config)
        
        return {
            "complete": True,
            "message": f"Trial {trial_number} completed",
            "results": results
        }
    
    def list_trials(self) -> List[Dict]:
        """List all trials with their status.
        
        Returns:
            List of trial summaries.
        """
        trials = []
        
        if not self.trials_dir.exists():
            return trials
        
        for trial_dir in sorted(self.trials_dir.glob("trial_*")):
            trial_match = re.match(r'trial_(\d+)', trial_dir.name)
            if trial_match:
                trial_number = int(trial_match.group(1))
                config = self.load_trial_config(trial_number)
                
                if config:
                    trial_info = config.get("trial_info", {})
                    summary = {
                        "trial_number": trial_number,
                        "status": trial_info.get("status", "unknown"),
                        "creation_time": trial_info.get("creation_time"),
                        "hyperparameters": trial_info.get("hyperparameters", {}),
                        "best_roc_auc": None
                    }
                    
                    # Add results if available
                    results = config.get("results", {}).get("aggregated", {})
                    if results and "best_roc_auc" in results:
                        summary["best_roc_auc"] = results["best_roc_auc"]
                    
                    trials.append(summary)
        
        return trials
    
    def get_best_trial(self) -> Optional[Dict]:
        """Get the trial with the best performance.
        
        Returns:
            Best trial summary or None if no completed trials.
        """
        trials = self.list_trials()
        completed_trials = [t for t in trials if t["status"] == "completed" and t.get("best_roc_auc")]
        
        if not completed_trials:
            return None
        
        best_trial = max(completed_trials, key=lambda t: t["best_roc_auc"])
        return best_trial
    
    def configure_training_files(self, signal_file: str, background_file: str,
                               signal_tree: str = "flat_TMVA", background_tree: str = "flat_TMVA"):
        """Configure signal and background files for training.
        
        Args:
            signal_file: Path to signal training file.
            background_file: Path to background training file.
            signal_tree: Name of signal tree in file.
            background_tree: Name of background tree in file.
        """
        self.training_files["signal"]["source"] = signal_file
        self.training_files["signal"]["tree"] = signal_tree
        self.training_files["background"]["source"] = background_file
        self.training_files["background"]["tree"] = background_tree
        
        print(f"Updated training configuration:")
        print(f"  Signal: {signal_file} (tree: {signal_tree})")
        print(f"  Background: {background_file} (tree: {background_tree})")
    
    def generate_jobs(self, grid_config: Dict = None, grid_size: str = "small", start_id: int = None, append: bool = False) -> bool:
        """Generate job configurations for hyperparameter grid.
        
        Args:
            grid_config: Custom grid configuration. If None, uses default.
            grid_size: Size of grid ("small", "medium", "large", "phase2"). Ignored if grid_config provided.
            start_id: Starting job ID number. If None, auto-detects from existing jobs.
            append: If True, append to existing index.json. If False, overwrite.
            
        Returns:
            True if successful, False otherwise.
        """
        print("=== Generating TMVA HPO jobs ===")
        
        # Load existing index if appending
        existing_index = None
        if append or start_id is None:
            index_path = self.jobs_dir / "index.json"
            if index_path.exists():
                with open(index_path, 'r') as f:
                    existing_index = json.load(f)
                print(f"✓ Loaded existing index with {len(existing_index.get('jobs', []))} jobs")
        
        # Determine starting job ID
        if start_id is None:
            if existing_index and existing_index.get('jobs'):
                max_id = max(job['id'] for job in existing_index['jobs'])
                start_id = max_id + 1
                print(f"✓ Auto-detected start_id={start_id} (continuing from job_{max_id:04d})")
            else:
                start_id = 1
                print(f"✓ No existing jobs found, starting from job_0001")
        
        # Setup grid based on size if no custom config provided
        if grid_config is None:
            if grid_size == "small":
                # Test just a few feature combinations
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10"],
                    "mlp_ncycles": ["50"],
                    "mlp_testrate": ["5"],
                    "bdt_ntrees": ["50", "100"],
                    "bdt_maxdepth": ["2"],
                    "feature_sets": self._generate_fcal_feature_sets(num_sets=3),
                    "train_events": ["500000"]
                }
            elif grid_size == "medium":
                # Test moderate number of feature combinations
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10", "N+15"],
                    "mlp_ncycles": ["50", "100"],
                    "mlp_testrate": ["5"],
                    "bdt_ntrees": ["50", "100", "200"],
                    "bdt_maxdepth": ["2", "3"],
                    "feature_sets": self._generate_fcal_feature_sets(num_sets=10),
                    "train_events": ["500000"]
                }
            elif grid_size == "large":
                grid_config = self.default_grid.copy()
                # Generate many feature combinations
                grid_config["feature_sets"] = self._generate_fcal_feature_sets(num_sets=50)
            elif grid_size == "all_features":
                # Generate ALL FCAL feature combinations (32 or 64)
                grid_config = {
                    "mlp_hidden": ["N+10"],
                    "mlp_ncycles": ["100"],
                    "mlp_testrate": ["5"],
                    "bdt_ntrees": ["100"],
                    "bdt_maxdepth": ["2"],
                    "feature_sets": self._generate_all_fcal_combinations(include_eoverp_swap=False),  # 32 combinations
                    "train_events": ["500000"]
                }
            elif grid_size == "all_features_full":
                # ALL combinations including EoverPmeas swaps (64 total)
                grid_config = {
                    "mlp_hidden": ["N+10"],
                    "mlp_ncycles": ["100"],
                    "mlp_testrate": ["5"],
                    "bdt_ntrees": ["100"],
                    "bdt_maxdepth": ["2"],
                    "feature_sets": self._generate_all_fcal_combinations(include_eoverp_swap=True),  # 64 combinations
                    "train_events": ["500000"]
                }
            elif grid_size == "phase2":
                # Phase 2: Full HPO grid (~700 jobs)
                # Based on ChatGPT HPO strategy: Top 10 features × 70 configs
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10", "N+15", "N+20"],
                    "mlp_ncycles": ["200", "300", "400"],
                    "mlp_testrate": ["5", "10"],
                    "bdt_ntrees": ["100", "200", "400", "800"],
                    "bdt_maxdepth": ["2", "3", "4"],
                    "feature_sets": self._get_top_features_from_phase1(n=10),  # Top 10 from Phase 1
                    "train_events": ["1000000"]  # Use full 1M events for Phase 2
                }
                print(f"  Phase 2 grid: {len(grid_config['feature_sets'])} features × ~70 configs = ~700 jobs")
            elif grid_size == "phase3":
                # Phase 3: Full hyperparameter exploration with training sample scaling
                # Focus on winning 8-feature set from Phase 2
                # Test ALL neuron types and other hyperparameters (no independence assumptions)
                # Note: Neuron types are case-sensitive in TMVA
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10", "N+15"],
                    "mlp_ncycles": ["200", "300"],
                    "mlp_testrate": ["5", "10"],
                    "mlp_neuron": ["tanh", "sigmoid", "ReLU", "linear"],  # Case-sensitive neuron types
                    "bdt_ntrees": ["100", "400", "800"],
                    "bdt_maxdepth": ["2", "3"],
                    "feature_sets": ["fcal_1111111100"],  # Winner from Phase 2 (8 features)
                    "train_events": ["20000", "40000", "80000", "160000", 
                                   "320000", "640000", "1000000", "1500000"]  # 8 levels
                }
                hp_configs = (len(grid_config['mlp_hidden']) * len(grid_config['mlp_ncycles']) *
                            len(grid_config['mlp_testrate']) * len(grid_config['mlp_neuron']) *
                            len(grid_config['bdt_ntrees']) * len(grid_config['bdt_maxdepth']))
                total = len(grid_config['train_events']) * len(grid_config['feature_sets']) * hp_configs
                print(f"  Phase 3 grid: {len(grid_config['train_events'])} nTrain × {len(grid_config['feature_sets'])} features × {hp_configs} HP configs = {total} jobs")
                print(f"  Neuron types: {grid_config['mlp_neuron']}")
                print(f"  TestRate: {grid_config['mlp_testrate']} (fully varied)")
                print(f"  nTrain range: 20k → 1.5M (8 levels)")
            elif grid_size == "phase1_full":
                # Phase 1 Full: Complete fresh HPO with 8-var and 3-var feature sets
                # Fixed 600k test events, corrected nTrain parameter
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10", "N+15"],
                    "mlp_ncycles": ["200", "300"],
                    "mlp_testrate": ["5", "10"],
                    "mlp_neuron": ["tanh", "sigmoid", "ReLU", "linear"],
                    "bdt_ntrees": ["100", "400", "800"],
                    "bdt_maxdepth": ["2", "3"],
                    "feature_sets": ["fcal_1111111100", "fcal_1110000000"],  # 8-var + 3-var baseline
                    "train_events": ["20000", "40000", "80000", "160000",
                                   "320000", "640000", "1000000", "1500000"]
                }
                hp_configs = (len(grid_config['mlp_hidden']) * len(grid_config['mlp_ncycles']) *
                            len(grid_config['mlp_testrate']) * len(grid_config['mlp_neuron']) *
                            len(grid_config['bdt_ntrees']) * len(grid_config['bdt_maxdepth']))
                total = len(grid_config['train_events']) * len(grid_config['feature_sets']) * hp_configs
                print(f"  Phase 1 Full grid: {len(grid_config['train_events'])} nTrain × {len(grid_config['feature_sets'])} features × {hp_configs} HP configs = {total} jobs")
                print(f"  Feature sets: 8-var (fcal_1111111100) + 3-var baseline (fcal_11100000)")
                print(f"  Fixed test: 600k events per class")
                print(f"  nTrain range: 20k → 1.5M (8 levels)")
            elif grid_size == "full":
                # Phase 4: Ablation study + variable transformation testing (MLP only)
                # Feature ablation: core 3, +each individual extra, all 8, leave-one-out
                ablation_features = [
                    "fcal_1110000000",  # Core 3: EoverPkin, DOCA, E9E25
                    "fcal_1110001000",  # Core + E1E9
                    "fcal_1110010000",  # Core + SumU
                    "fcal_1110100000",  # Core + SumV
                    "fcal_1111000000",  # Core + UV_Asymmetry
                    "fcal_1110000100",  # Core + Saturation
                    "fcal_1111111100",  # All 8 features
                    "fcal_0111111100",  # All - EoverPkin (keep DOCA, E9E25)
                    "fcal_1011111100",  # All - DOCA
                    "fcal_1101111100",  # All - E9E25
                    "fcal_1110111100",  # All - E1E9
                    "fcal_1111011100",  # All - SumU
                    "fcal_1111101100",  # All - SumV
                    "fcal_1111110100",  # All - UV_Asymmetry
                    "fcal_1111111000",  # All - Saturation
                ]
                grid_config = {
                    "mlp_hidden": ["N+5", "N+10", "N+15", "N+20"],
                    "mlp_ncycles": ["200", "400", "600"],
                    "mlp_testrate": ["5"],
                    "mlp_neuron": ["tanh", "ReLU", "sigmoid", "linear"],
                    "mlp_vartransform": ["D", "G", "D,G"],
                    "bdt_ntrees": ["400"],  # Dummy
                    "bdt_maxdepth": ["3"],  # Dummy
                    "feature_sets": ablation_features,
                    "train_events": ["100000", "500000", "1000000", "1400000"],
                    "skip_bdt": True,
                    "minus_only": True
                }
                total = (len(grid_config['mlp_hidden']) * len(grid_config['mlp_ncycles']) * 
                        len(grid_config['mlp_vartransform']) * len(grid_config['feature_sets']) *
                        len(grid_config['mlp_neuron']) * len(grid_config['train_events']))
                print(f"  Comprehensive grid: {total} jobs")
                print(f"  Features: {len(grid_config['feature_sets'])} ablation combos")
                print(f"  Transforms: {', '.join(grid_config['mlp_vartransform'])}")
                print(f"  Neurons: {', '.join(grid_config['mlp_neuron'])}")
                print(f"  NCycles: {', '.join(grid_config['mlp_ncycles'])}")
                print(f"  Hidden: {', '.join(grid_config['mlp_hidden'])}")
                print(f"  Train events: {', '.join(grid_config['train_events'])}")
                print(f"  MLP ONLY, MINUS ONLY (max efficiency)")
            else:
                print(f"Unknown grid size: {grid_size}")
                return False
        
        # Create jobs directory
        self.jobs_dir.mkdir(parents=True, exist_ok=True)
        
        # Load base vars.ini (use vars_nospect.ini for HPO to reduce memory)
        base_vars_path = self.base_dir / "vars_nospect.ini"
        if not base_vars_path.exists():
            print(f"✗ Required vars_nospect.ini file not found at {base_vars_path}")
            print(f"  Falling back to vars.ini (WARNING: will use 8GB+ RAM)")
            base_vars_path = self.base_dir / "vars.ini"
            if not base_vars_path.exists():
                print(f"✗ No vars file found")
                return False
        
        # Generate all combinations
        # Include mlp_neuron if present in grid (Phase 3+)
        mlp_neurons = grid_config.get("mlp_neuron", ["tanh"])  # Default to tanh
        mlp_testrates = grid_config.get("mlp_testrate", ["5"])
        mlp_vartransforms = grid_config.get("mlp_vartransform", ["N"])
        skip_bdt = grid_config.get("skip_bdt", False)
        minus_only = grid_config.get("minus_only", False)
        
        combinations = list(itertools.product(
            grid_config["mlp_hidden"],
            grid_config["mlp_ncycles"],
            grid_config["bdt_ntrees"],
            grid_config["bdt_maxdepth"],
            grid_config.get("feature_sets", ["fcal_only"]),
            grid_config.get("train_events", ["500000"]),
            mlp_neurons,
            mlp_testrates,
            mlp_vartransforms
        ))
        
        print(f"Generating {len(combinations)} job configurations starting from job_{start_id:04d}...")
        if skip_bdt:
            print(f"  (BDT training SKIPPED)")
        if minus_only:
            print(f"  (MINUS only - PLUS skipped for 2x speedup)")
        
        job_index = []
        for i, (mlp_hidden, mlp_ncycles, bdt_ntrees, bdt_maxdepth, feature_set, train_events, mlp_neuron, mlp_testrate, mlp_vartransform) in enumerate(combinations, start_id):
            job_id = f"job_{i:04d}"
            job_dir = self.jobs_dir / job_id
            job_dir.mkdir(exist_ok=True)
            
            # Create job-specific vars.ini with feature modifications
            job_vars_path = job_dir / "vars.ini"
            self._create_modified_vars_ini(base_vars_path, job_vars_path, feature_set)
            
            # Append hyperparameter configuration
            with open(job_vars_path, 'a', encoding='utf-8') as f:
                f.write('\n# TMVA Configuration (auto-generated)\n')
                f.write('[mlp_options]\n')
                mlp_opts = self._build_mlp_options(mlp_hidden, mlp_ncycles, mlp_testrate, mlp_neuron, mlp_vartransform)
                f.write(mlp_opts + '\n\n')
                f.write('[bdt_options]\n')
                bdt_opts = self._build_bdt_options(bdt_ntrees, bdt_maxdepth)
                f.write(bdt_opts + '\n\n')
                f.write('[mlp_hidden]\n')
                f.write(mlp_hidden + '\n\n')
                f.write('[mlp_ncycles]\n')
                f.write(mlp_ncycles + '\n\n')
                f.write('[mlp_testrate]\n')
                f.write(mlp_testrate + '\n\n')
                f.write('[mlp_neuron]\n')
                f.write(mlp_neuron + '\n\n')
                f.write('[mlp_vartransform]\n')
                f.write(mlp_vartransform + '\n\n')
                f.write('[bdt_ntrees]\n')
                f.write(bdt_ntrees + '\n\n')
                f.write('[bdt_maxdepth]\n')
                f.write(bdt_maxdepth + '\n\n')
                f.write('[feature_set]\n')
                f.write(feature_set + '\n\n')
                f.write('[train_events]\n')
                f.write(train_events + '\n\n')
                f.write('[skip_bdt]\n')
                f.write(('true' if skip_bdt else 'false') + '\n\n')
                f.write('[minus_only]\n')
                f.write(('true' if minus_only else 'false') + '\n\n')
                f.write('[meta]\n')
                outname = f"out_mlp_{mlp_hidden.replace('+','p')}_nc{mlp_ncycles}_nt{mlp_neuron}_vt{mlp_vartransform.replace(',','')}_feat{feature_set}_train{train_events}.root"
                f.write(f'out={outname}\n')
            
            # Create dataset subdirectories
            if not minus_only:
                (job_dir / "datasetPLUS").mkdir(exist_ok=True)
            (job_dir / "datasetMINUS").mkdir(exist_ok=True)
            
            # Add to index
            job_config = {
                'id': i,
                'job_id': job_id,
                'job_dir': str(job_dir),
                'vars_file': str(job_vars_path),
                'mlp_hidden': mlp_hidden,
                'mlp_ncycles': mlp_ncycles,
                'mlp_testrate': mlp_testrate,
                'mlp_neuron': mlp_neuron,
                'mlp_vartransform': mlp_vartransform,
                'bdt_ntrees': bdt_ntrees,
                'bdt_maxdepth': bdt_maxdepth,
                'feature_set': feature_set,
                'train_events': train_events,
                'skip_bdt': skip_bdt,
                'minus_only': minus_only,
                'status': 'generated',
                'hyperparameters': {
                    'mlp_hidden': mlp_hidden,
                    'mlp_ncycles': mlp_ncycles,
                    'mlp_vartransform': mlp_vartransform,
                    'bdt_ntrees': bdt_ntrees,
                    'bdt_maxdepth': bdt_maxdepth,
                    'feature_set': feature_set,
                    'train_events': train_events,
                    'skip_bdt': skip_bdt
                },
                'results': {
                    # Placeholder for future Optuna integration
                    'roc_auc': None,
                    'signal_efficiency': None,
                    'background_rejection': None,
                    'training_time': None,
                    'model_size': None
                },
                'optuna_metadata': {
                    # Placeholder for future Optuna integration
                    'trial_number': 1,
                    'job_index_in_trial': i,
                    'suggestion_algorithm': 'grid_search'
                }
            }
            job_index.append(job_config)
            
            if skip_bdt:
                print(f"  Generated {job_id}: MLP({mlp_hidden},{mlp_ncycles},{mlp_neuron},VarTransform={mlp_vartransform}), Features({feature_set}), Train({train_events})")
            else:
                print(f"  Generated {job_id}: MLP({mlp_hidden},{mlp_ncycles},{mlp_neuron},VarTransform={mlp_vartransform}), BDT({bdt_ntrees},{bdt_maxdepth}), Features({feature_set}), Train({train_events})")
        
        # Create or update enhanced job index
        if append and existing_index:
            # Append to existing index
            enhanced_index = existing_index
            enhanced_index['jobs'].extend(job_index)
            enhanced_index['metadata']['total_jobs'] = len(enhanced_index['jobs'])
            enhanced_index['metadata']['last_updated'] = time.strftime('%Y-%m-%dT%H:%M:%SZ')
            print(f"\n✓ Appending {len(job_index)} jobs to existing {len(existing_index['jobs']) - len(job_index)} jobs")
        else:
            # Create new index
            enhanced_index = {
                "metadata": {
                    "creation_time": time.strftime('%Y-%m-%dT%H:%M:%SZ'),
                    "system_version": "tmva_hpo_manager_v1.0",
                    "search_type": "grid_search",  # Will change to "optuna" in future
                    "total_jobs": len(job_index),
                    "grid_size": grid_size,
                    "features_used": "from_vars_ini"  # Placeholder for future feature tracking
                },
                "search_space": {
                    "mlp_hidden": list(set(job['mlp_hidden'] for job in job_index)),
                    "mlp_ncycles": list(set(job['mlp_ncycles'] for job in job_index)),
                    "bdt_ntrees": list(set(job['bdt_ntrees'] for job in job_index)),
                    "bdt_maxdepth": list(set(job['bdt_maxdepth'] for job in job_index))
                },
                "trial_info": {
                    "current_trial": 1,
                    "trial_type": "initial_grid_search",
                    "jobs_in_trial": len(job_index)
                },
                "jobs": job_index
            }
            print(f"\n✓ Generated {len(combinations)} new jobs")
        
        # Save enhanced job index
        index_path = self.jobs_dir / "index.json"
        with open(index_path, 'w', encoding='utf-8') as f:
            json.dump(enhanced_index, f, indent=2)
        
        print(f"✓ Job index saved to {index_path} (total: {enhanced_index['metadata']['total_jobs']} jobs)")
        return True
    
    def _generate_fcal_feature_sets(self, num_sets: int = 10) -> List[str]:
        """Generate different combinations of FCAL features to test.
        
        Args:
            num_sets: Number of different feature combinations to generate.
            
        Returns:
            List of feature set identifiers (encoded as 10-bit strings).
        """
        import random
        
        feature_sets = []
        
        # ALWAYS start with baseline (current vars.ini: 7 features)
        # Bits: EoverPkin, DOCA, E9E25, E1E9, SumU, SumV, UV_Asymmetry, Sat, reserved, EoverPmeas
        #       1         1     1      1     1     1     1             0    0         0
        feature_sets.append("fcal_1111111000")
        
        # Generate random combinations
        for i in range(min(num_sets - 2, 2**10 - 1)):
            feature_mask = random.randint(0, 2**10 - 1)
            feature_id = f"fcal_{feature_mask:010b}"
            
            # Skip if bit 9 (EoverPmeas) is ON but bit 0 (EoverPkin) is also ON
            # They should be mutually exclusive
            if feature_id[9] == '1' and feature_id[0] == '1':
                continue
                
            if feature_id not in feature_sets:
                feature_sets.append(feature_id)
        
        # Add specific interesting combinations
        # Minimum features (just the core 3)
        if "fcal_1110000000" not in feature_sets:
            feature_sets.append("fcal_1110000000")
        
        # Maximum features (all standard ones, no EoverPmeas swap)
        if "fcal_1111111100" not in feature_sets:
            feature_sets.append("fcal_1111111100")
        
        # EoverPmeas variant of baseline (swap bit 0 OFF, bit 9 ON)
        if "fcal_0111111001" not in feature_sets:
            feature_sets.append("fcal_0111111001")
        
        return feature_sets[:num_sets]
    
    def _generate_all_fcal_combinations(self, include_eoverp_swap: bool = False) -> List[str]:
        """Generate all combinations of FCAL features with bits 0-2 (E/p, DOCA, E9E25) always ON.
        
        Args:
            include_eoverp_swap: If True, also generate combinations with EoverPmeas (bit 9) 
                                instead of EoverPkin (bit 0). This doubles the number of combinations.
        
        Returns:
            List of feature set identifiers (e.g., ['fcal_1110000000', 'fcal_1110001000', ...])
        
        Strategy:
            - Bits 0-2 (EoverPkin, DOCA, E9E25): Always 1 (core features)
            - Bits 3-7 (E1E9, SumU, SumV, UV_Asymmetry, Saturation): All 32 combinations
            - Bit 8: Always 0 (reserved)
            - Bit 9 (EoverPmeas): 0 by default, or swap with bit 0 if include_eoverp_swap=True
            
            Result: 32 combinations (or 64 with EoverPmeas swaps)
        """
        feature_sets = []
        
        # Generate all 32 combinations for bits 3-7 (variable features)
        for variable_bits in range(32):  # 2^5 = 32 combinations
            # Extract individual bit values for bits 3-7
            bit3 = (variable_bits >> 4) & 1  # E1E9
            bit4 = (variable_bits >> 3) & 1  # SumU
            bit5 = (variable_bits >> 2) & 1  # SumV
            bit6 = (variable_bits >> 1) & 1  # UV_Asymmetry
            bit7 = variable_bits & 1         # Saturation
            
            # Standard configuration: EoverPkin ON (bit 0), EoverPmeas OFF (bit 9)
            feature_mask = f"111{bit3}{bit4}{bit5}{bit6}{bit7}00"  # Bits 0-2 always 111, bit 8-9 always 00
            feature_sets.append(f"fcal_{feature_mask}")
            
            # If requested, also generate EoverPmeas variants (bit 0 OFF, bit 9 ON)
            if include_eoverp_swap:
                feature_mask_swap = f"011{bit3}{bit4}{bit5}{bit6}{bit7}01"  # Bit 0 OFF, bits 1-2 ON, bit 9 ON
                feature_sets.append(f"fcal_{feature_mask_swap}")
        
        return feature_sets
    
    def _get_top_features_from_phase1(self, n: int = 10) -> List[str]:
        """Get top N feature sets from Phase 1 results based on ROC AUC.
        
        Args:
            n: Number of top feature sets to return
            
        Returns:
            List of feature set identifiers (e.g., ['fcal_1111111100', ...])
        """
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print(f"⚠ No index.json found, using default top features")
            # Fallback: Use best guesses from Phase 1
            return [
                "fcal_1111111100",  # All features
                "fcal_1111111000",  # Baseline 7 features
                "fcal_1110111100",  # Missing E1E9
                "fcal_1111011100",  # Missing SumU
                "fcal_1111101100",  # Missing SumV
                "fcal_1111110100",  # Missing UV_Asymmetry
                "fcal_1111111000",  # Missing Saturation
                "fcal_1110000000",  # Minimal (E/p, DOCA, E9E25)
                "fcal_1100000000",  # E/p + DOCA only
                "fcal_1010000000",  # E/p + E9E25 only
            ][:n]
        
        # Load index and sort by BDT ROC AUC
        with open(index_path, 'r') as f:
            index_data = json.load(f)
        
        jobs = index_data.get('jobs', [])
        
        # Filter jobs with results
        jobs_with_results = [j for j in jobs if j.get('results', {}).get('roc_auc_bdt') is not None]
        
        if not jobs_with_results:
            print(f"⚠ No jobs with results found, using default top features")
            return self._get_top_features_from_phase1(n)  # Fall back to defaults
        
        # Sort by BDT ROC AUC descending
        sorted_jobs = sorted(jobs_with_results, 
                           key=lambda x: x.get('results', {}).get('roc_auc_bdt', 0), 
                           reverse=True)
        
        # Extract unique feature sets (top N)
        top_features = []
        seen_features = set()
        for job in sorted_jobs:
            feature_set = job.get('feature_set', job.get('hyperparameters', {}).get('feature_set'))
            if feature_set and feature_set not in seen_features:
                top_features.append(feature_set)
                seen_features.add(feature_set)
                if len(top_features) >= n:
                    break
        
        print(f"✓ Selected top {len(top_features)} features from Phase 1 results:")
        for i, fs in enumerate(top_features, 1):
            decoded = self.decode_feature_set(fs)
            print(f"  {i}. {fs}: {', '.join(decoded)}")
        
        return top_features
    
    def _create_modified_vars_ini(self, base_vars_path: Path, job_vars_path: Path, feature_set: str):
        """Create a modified vars.ini with FCAL features based on feature_set specification.
        
        Args:
            base_vars_path: Path to base vars.ini template.
            job_vars_path: Path to output job-specific vars.ini.
            feature_set: Feature set identifier (e.g., 'fcal_1110111100').
        """
        # Read base vars.ini
        with open(base_vars_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        
        # Parse sections - we only care about spectators for this approach
        spectators = []
        current_section = None
        
        for line in lines:
            stripped = line.strip()
            if stripped == '[spectators]':
                current_section = 'spectators'
            elif stripped.startswith('['):
                current_section = None
            elif current_section == 'spectators' and stripped and not stripped.startswith('#'):
                spectators.append(stripped)
        
        # Decode the 10-bit feature mask
        training_plus = []
        training_minus = []
        
        if feature_set.startswith("fcal_"):
            mask_str = feature_set.split("_")[1]
            
            # Process each bit position
            for idx, feature_pair in enumerate(self.fcal_features):
                if feature_pair is None:  # Reserved bit
                    continue
                    
                pos_var, neg_var = feature_pair
                
                # Special handling for bit 9 (EoverPmeas) - mutually exclusive with bit 0 (EoverPkin)
                if idx == 9 and mask_str[idx] == '1':
                    # EoverPmeas is ON, so make sure EoverPkin goes to spectators
                    training_plus.append(pos_var)
                    training_minus.append(neg_var)
                    # Force EoverPkin to spectators
                    eoverp_pair = self.fcal_features[0]
                    if eoverp_pair[0] not in spectators:
                        spectators.append(eoverp_pair[0])
                    if eoverp_pair[1] not in spectators:
                        spectators.append(eoverp_pair[1])
                elif idx == 0 and mask_str[9] == '1':
                    # EoverPmeas is ON (bit 9), so skip EoverPkin (force to spectators)
                    if pos_var not in spectators:
                        spectators.append(pos_var)
                    if neg_var not in spectators:
                        spectators.append(neg_var)
                elif mask_str[idx] == '1':
                    # Normal case: bit is ON, include in training
                    training_plus.append(pos_var)
                    training_minus.append(neg_var)
                else:
                    # Bit is OFF: move to spectators if not already there
                    if pos_var not in spectators:
                        spectators.append(pos_var)
                    if neg_var not in spectators:
                        spectators.append(neg_var)
            
            # Ensure excluded features are always in spectators
            for pos_var, neg_var in self.fcal_excluded:
                if pos_var not in spectators:
                    spectators.append(pos_var)
                if neg_var not in spectators:
                    spectators.append(neg_var)
        
        # Remove training features from spectators (if they were already there)
        for var in training_plus + training_minus:
            if var in spectators:
                spectators.remove(var)
        
        # Write modified vars.ini
        with open(job_vars_path, 'w', encoding='utf-8') as f:
            f.write('[training_plus]\n')
            for var in training_plus:
                f.write(f'{var}\n')
            f.write('\n')
            
            f.write('[training_minus]\n')
            for var in training_minus:
                f.write(f'{var}\n')
            f.write('\n')
            
            # Skip spectators section entirely for HPO (user request: no spectators)
            # f.write('[spectators]\n')
            # for var in spectators:
            #     f.write(f'{var}\n')
    
    
    def _build_mlp_options(self, hidden: str, ncycles: str, testrate: str, neuron: str = "tanh", vartransform: str = "N") -> str:
        """Build MLP options string."""
        return (
            f"H:!V:NeuronType={neuron}:VarTransform={vartransform}:NCycles={ncycles}:"
            f"HiddenLayers={hidden}:TestRate={testrate}:!UseRegulator"
        )
    
    def _build_bdt_options(self, ntrees: str, maxdepth: str) -> str:
        """Build BDT options string."""
        return (
            f"!H:!V:NTrees={ntrees}:MinNodeSize=2.5%:MaxDepth={maxdepth}:"
            f"BoostType=AdaBoost:AdaBoostBeta=0.5:UseBaggedBoost:"
            f"BaggedSampleFraction=0.5:SeparationType=GiniIndex:nCuts=20"
        )
    
    def run_jobs(self, job_id: Optional[str] = None, step: Optional[int] = None, 
                 parallel: bool = False, max_jobs: int = 4, interactive: bool = False,
                 steps_range: Optional[str] = None) -> bool:
        """Run TMVA jobs.
        
        Args:
            job_id: Specific job to run. If None, runs all jobs.
            step: Specific step to run (1-7). If None, runs all steps.
            parallel: Whether to run jobs in parallel.
            max_jobs: Maximum number of parallel jobs.
            interactive: Whether to run in interactive mode with prompts.
            steps_range: Range of steps to run (e.g., "1-7"). Overrides step parameter.
            
        Returns:
            True if successful, False otherwise.
        """
        # Parse steps range if provided
        if steps_range:
            try:
                start, end = map(int, steps_range.split('-'))
                steps_to_run = list(range(start, end + 1))
            except:
                print(f"✗ Invalid steps range: {steps_range}. Use format like '1-7'")
                return False
        elif step:
            steps_to_run = [step]
        else:
            steps_to_run = None  # Run all steps
        
        if interactive:
            return self.run_jobs_interactive(job_id, step)
        print("=== Running TMVA HPO jobs ===")
        
        # Load job index
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("✗ No job index found. Run 'generate' first.")
            return False
        
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        # Handle both old and new JSON formats
        if isinstance(index_data, list):
            job_index = index_data
        else:
            job_index = index_data.get('jobs', [])
        
        # Filter jobs if specific job_id requested
        if job_id:
            jobs_to_run = []
            for job in job_index:
                current_job_id = TMVAHPOManager.get_job_id(job)
                if current_job_id == job_id:
                    jobs_to_run.append(job)
            if not jobs_to_run:
                print(f"✗ Job {job_id} not found")
                return False
        else:
            jobs_to_run = job_index
        
        print(f"Running {len(jobs_to_run)} jobs...")
        
        success = True
        for job in jobs_to_run:
            job_success = self._run_single_job(job, steps_to_run=steps_to_run)
            if not job_success:
                success = False
                if not parallel:  # Stop on first failure if running sequentially
                    break
        
        return success
    
    def run_jobs_interactive(self, job_id: Optional[str] = None, step: Optional[int] = None) -> bool:
        """Run TMVA jobs interactively with user prompts.
        
        Args:
            job_id: Specific job to run. If None, prompts for each job.
            step: Specific step to run (1-6). If None, runs all steps.
            
        Returns:
            True if successful, False otherwise.
        """
        print("=== Running TMVA HPO jobs (INTERACTIVE MODE) ===")
        
        # Load job index
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("✗ No job index found. Run 'generate' first.")
            return False
        
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        # Handle both old and new JSON formats
        if isinstance(index_data, list):
            job_index = index_data
        else:
            job_index = index_data.get('jobs', [])
        
        # Filter jobs if specific job_id requested
        if job_id:
            jobs_to_run = []
            for job in job_index:
                current_job_id = TMVAHPOManager.get_job_id(job)
                if current_job_id == job_id:
                    jobs_to_run.append(job)
            if not jobs_to_run:
                print(f"✗ Job {job_id} not found")
                return False
        else:
            jobs_to_run = job_index
        
        print(f"\nFound {len(jobs_to_run)} jobs to run")
        print("\nJob configurations:")
        for i, job in enumerate(jobs_to_run, 1):
            job_id_str = TMVAHPOManager.get_job_id(job)
            print(f"  {i}. {job_id_str}:")
            print(f"     MLP Hidden: {job['mlp_hidden']}, NCycles: {job['mlp_ncycles']}")
            print(f"     BDT NTrees: {job['bdt_ntrees']}, MaxDepth: {job['bdt_maxdepth']}")
        
        print(f"\nSteps per job: {'All (1-6)' if step is None else f'Step {step} only'}")
        print("\nJobs will run sequentially on this machine.")
        
        # Ask for confirmation to start
        try:
            response = input("\nStart running jobs? [y/N]: ").strip().lower()
            if response not in ['y', 'yes']:
                print("Cancelled by user")
                return False
        except (EOFError, KeyboardInterrupt):
            print("\nCancelled by user")
            return False
        
        # Run jobs one at a time with progress updates
        completed = 0
        failed = 0
        
        for i, job in enumerate(jobs_to_run, 1):
            job_id_str = TMVAHPOManager.get_job_id(job)
            
            print(f"\n{'='*60}")
            print(f"Job {i}/{len(jobs_to_run)}: {job_id_str}")
            print(f"MLP: {job['mlp_hidden']}, {job['mlp_ncycles']} cycles")
            print(f"BDT: {job['bdt_ntrees']} trees, depth {job['bdt_maxdepth']}")
            print(f"{'='*60}")
            
            job_success = self._run_single_job(job, step, interactive=True)
            
            if job_success:
                completed += 1
                print(f"\n✓ Job {i}/{len(jobs_to_run)} completed successfully")
            else:
                failed += 1
                print(f"\n✗ Job {i}/{len(jobs_to_run)} failed")
                
                # Ask whether to continue after failure
                try:
                    response = input("\nContinue with remaining jobs? [y/N]: ").strip().lower()
                    if response not in ['y', 'yes']:
                        print("Stopping...")
                        break
                except (EOFError, KeyboardInterrupt):
                    print("\nStopping...")
                    break
            
            # Show overall progress
            print(f"\nProgress: {completed} completed, {failed} failed, {len(jobs_to_run) - i} remaining")
        
        print(f"\n{'='*60}")
        print(f"Interactive run complete:")
        print(f"  ✓ Completed: {completed}")
        print(f"  ✗ Failed: {failed}")
        print(f"{'='*60}")
        
        return failed == 0
    
    def _run_single_job(self, job: Dict, steps_to_run: Optional[List[int]] = None, interactive: bool = False) -> bool:
        """Run a single TMVA job.
        
        Args:
            job: Job configuration dictionary.
            steps_to_run: List of steps to run. If None, runs all steps.
            interactive: Whether to show real-time output.
            
        Returns:
            True if successful, False otherwise.
        """
        job_dir = Path(job['job_dir'])
        job_id = TMVAHPOManager.get_job_id(job)
        vars_file = job['vars_file']
        
        print(f"\nRunning {job_id}...")
        
        # Change to job directory
        original_cwd = os.getcwd()
        os.chdir(job_dir)
        
        try:
            # Set environment variables (minimal - just ROOT batch mode)
            env = os.environ.copy()
            env['ROOT_BATCH'] = '-b'
            
            if steps_to_run is None:
                steps_to_run = [1, 2, 3, 4, 5, 6, 7]
            
            for step_num in steps_to_run:
                success = self._run_step(step_num, env, job_id, interactive, job)
                if not success:
                    return False
            
            # Update job status
            self._update_job_status(job_id, 'completed')
            print(f"✓ {job_id} completed successfully")
            return True
            
        except Exception as e:
            print(f"✗ {job_id} failed: {e}")
            self._update_job_status(job_id, 'failed')
            return False
        finally:
            os.chdir(original_cwd)
    
    def _run_step(self, step: int, env: Dict, job_id: str, interactive: bool = False, job: Dict = None) -> bool:
        """Run a specific step of a TMVA job.
        
        Args:
            step: Step number (1-7).
            env: Environment variables.
            job_id: Job ID for logging.
            interactive: Whether to show real-time output.
            job: Job configuration dictionary.
            
        Returns:
            True if successful, False otherwise.
        """
        step_descriptions = {
            1: "Training MINUS model",
            2: "Training PLUS model", 
            3: "Applying MINUS to pi0pippim",
            4: "Applying PLUS to pi0pippim",
            5: "Applying MINUS to gepem",
            6: "Applying PLUS to gepem",
            7: "Extracting metrics"
        }
        
        print(f"  [{step}] {step_descriptions[step]}")
        
        # Get absolute paths to ROOT macros in base directory
        tmva_classification_macro = self.base_dir / "TMVAClassification.C"
        tmva_application_macro = self.base_dir / "TMVAClassificationApplication.C"
        
        if step in [1, 2]:
            # Training steps - pass signal and background files as arguments  
            side = "MINUS" if step == 1 else "PLUS"
            sig_file = self.training_files["signal"]["source"]
            sig_tree = self.training_files["signal"]["tree"]
            bg_file = self.training_files["background"]["source"]
            bg_tree = self.training_files["background"]["tree"]
            
            # Check if BDT should be skipped
            skip_bdt = job.get('skip_bdt', False)
            methods = "MLP" if skip_bdt else "MLP,BDT"
            
            # Create a temporary tcsh script to avoid complex quote escaping
            script_content = f"""#!/bin/tcsh
gxenv
root -l -b -q '{tmva_classification_macro}("{methods}", std::map<std::string,std::string>(), "{side}", "{sig_file}", "{sig_tree}", "{bg_file}", "{bg_tree}")'
"""
            script_file = Path(f"./step{step}_train.csh")
            with open(script_file, 'w') as f:
                f.write(script_content)
            script_file.chmod(0o755)
            
            cmd = f'tcsh {script_file}'
        elif step in [3, 4, 5, 6]:
            # Application steps - pass data files as arguments
            side = "MINUS" if step in [3, 5] else "PLUS"
            if step in [3, 4]:
                # pi0pippim file - use absolute path from configuration
                app_file = self.data_files["pi0pippim"]["source"]
                app_tree = self.data_files["pi0pippim"]["tree"]
            else:
                # gepem file - use absolute path from configuration
                app_file = self.data_files["gepem"]["source"]
                app_tree = self.data_files["gepem"]["tree"]
            
            # Check if BDT should be skipped
            skip_bdt = job.get('skip_bdt', False)
            methods = "MLP" if skip_bdt else "MLP,BDT"
            
            # Create a temporary tcsh script to avoid complex quote escaping
            script_content = f"""#!/bin/tcsh
gxenv
root -l -b -q '{tmva_application_macro}("{methods}", "{side}", "{app_file}", "{app_tree}")'
"""
            script_file = Path(f"./step{step}_apply.csh")
            with open(script_file, 'w') as f:
                f.write(script_content)
            script_file.chmod(0o755)
            
            cmd = f'tcsh {script_file}'
        elif step == 7:
            # Metrics extraction and aggregation step
            extraction_script = self.scripts_dir / "extract_tmva_metrics.py"
            
            # Find the output ROOT files for PLUS and MINUS
            plus_files = list(Path('.').glob('out_*_PLUS.root'))
            minus_files = list(Path('.').glob('out_*_MINUS.root'))
            
            if not plus_files or not minus_files:
                print(f"    ✗ Missing output files (PLUS: {len(plus_files)}, MINUS: {len(minus_files)})")
                return False
            
            metrics_data = {"plus": None, "minus": None}
            
            # Extract metrics for both sides
            for root_file, side in [(plus_files[0], 'PLUS'), (minus_files[0], 'MINUS')]:
                temp_json = f'metrics_{side}_temp.json'
                metrics_cmd = f'python {extraction_script} {root_file} --side {side} --output {temp_json}'
                
                try:
                    with open('run.log', 'a') as log_file:
                        log_file.write(f"\n=== Extracting {side} metrics ===\n")
                        log_file.write(f"Command: {metrics_cmd}\n")
                        log_file.flush()
                        
                        result = subprocess.run(
                            metrics_cmd,
                            shell=True,
                            stdout=log_file,
                            stderr=subprocess.STDOUT,
                            text=True
                        )
                        
                        if result.returncode != 0:
                            print(f"    ✗ {side} metrics extraction failed")
                            return False
                        
                        # Read extracted metrics
                        temp_path = Path(temp_json)
                        if temp_path.exists():
                            with open(temp_path, 'r') as f:
                                metrics_data[side.lower()] = json.load(f)
                            temp_path.unlink()  # Remove temp file
                        
                except Exception as e:
                    print(f"    ✗ {side} metrics extraction failed: {e}")
                    return False
            
            # Create aggregated metrics JSON
            import time
            aggregated = {
                "job_dir": str(Path.cwd()),
                "extraction_time": time.strftime('%Y-%m-%d %H:%M:%S'),
                "plus_model": metrics_data["plus"],
                "minus_model": metrics_data["minus"],
                "averaged_metrics": {}
            }
            
            # Calculate averages if both sides exist
            if metrics_data["plus"] and metrics_data["minus"]:
                metric_keys = ['roc_auc_mlp', 'roc_auc_bdt', 'separation_mlp', 'separation_bdt']
                
                for key in metric_keys:
                    plus_val = metrics_data["plus"].get(key)
                    minus_val = metrics_data["minus"].get(key)
                    if plus_val is not None and minus_val is not None:
                        aggregated["averaged_metrics"][key] = (plus_val + minus_val) / 2.0
            
            # Write single aggregated JSON
            with open('metrics.json', 'w') as f:
                json.dump(aggregated, f, indent=2)
            
            print(f"    ✓ Step 7 completed (metrics aggregated in metrics.json)")
            return True
        
        # Run the command
        try:
            with open('run.log', 'a') as log_file:
                log_file.write(f"\n=== Step {step}: {step_descriptions[step]} ===\n")
                log_file.write(f"Command: {cmd}\n")
                log_file.flush()
                
                if interactive:
                    # Interactive mode: show output in real-time and also log to file
                    import sys
                    process = subprocess.Popen(
                        cmd,
                        shell=True,
                        env=env,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1
                    )
                    
                    # Stream output line by line
                    for line in process.stdout:
                        sys.stdout.write(line)
                        sys.stdout.flush()
                        log_file.write(line)
                        log_file.flush()
                    
                    process.wait()
                    result_code = process.returncode
                else:
                    # Batch mode: just log to file
                    result = subprocess.run(
                        cmd, 
                        shell=True, 
                        env=env,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        text=True
                    )
                    result_code = result.returncode
                
                log_file.write(f"Exit code: {result_code}\n")
            
            # Clean up environment variables after command execution
            self._cleanup_tmva_env_vars(env)
            
            if result_code == 0:
                print(f"    ✓ Step {step} completed")
                return True
            else:
                print(f"    ✗ Step {step} failed (exit code: {result_code})")
                return False
                
        except Exception as e:
            print(f"    ✗ Step {step} failed with exception: {e}")
            return False
    
    def _cleanup_tmva_env_vars(self, env: Dict):
        """Remove TMVA-specific environment variables after use.
        
        Args:
            env: Environment dictionary to clean up.
        """
        tmva_vars = [
            'TMVA_SIGNAL_INPUT',
            'TMVA_SIGNAL_TREE', 
            'TMVA_BACKGROUND_INPUT',
            'TMVA_BACKGROUND_TREE',
            'TMVA_APP_INPUT',
            'TMVA_APP_TREE'
        ]
        
        for var in tmva_vars:
            env.pop(var, None)  # Remove if present, ignore if not
    
    def _update_job_status(self, job_id: str, status: str):
        """Update job status in a local .status file to avoid race conditions.
        
        When jobs run in parallel on the farm, updating the centralized index.json
        causes race conditions and file corruption. Instead, write status to a
        job-local file that can be collected later by update_index_with_results().
        """
        job_dir = self.jobs_dir / job_id
        if not job_dir.exists():
            return
        
        # Write status to local file (safe from race conditions)
        status_file = job_dir / ".status"
        status_data = {
            'job_id': job_id,
            'status': status,
            'last_updated': time.strftime('%Y-%m-%d %H:%M:%S')
        }
        
        with open(status_file, 'w', encoding='utf-8') as f:
            json.dump(status_data, f, indent=2)
    
    def show_status(self) -> bool:
        """Show status of all jobs."""
        print("=== TMVA HPO Job Status ===")
        
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("✗ No job index found. Run 'generate' first.")
            return False
        
        with open(index_path, 'r', encoding='utf-8') as f:
            index_data = json.load(f)
        
        # Handle both old and new JSON formats
        if isinstance(index_data, list):
            # Old format: direct list of jobs
            job_index = index_data
            metadata = {}
        else:
            # New format: structured with metadata
            job_index = index_data.get('jobs', [])
            metadata = index_data.get('metadata', {})
        
        print(f"\nTotal jobs: {len(job_index)}")
        if metadata:
            print(f"Search type: {metadata.get('search_type', 'unknown')}")
            print(f"Created: {metadata.get('creation_time', 'unknown')}")
        print()
        
        status_counts = {}
        for job in job_index:
            status = job.get('status', 'unknown')
            status_counts[status] = status_counts.get(status, 0) + 1
            
            # Handle both old and new job index formats
            job_id = TMVAHPOManager.get_job_id(job)
            print(f"{job_id}: {status}")
            print(f"  MLP: Hidden={job['mlp_hidden']}, NCycles={job['mlp_ncycles']}")
            print(f"  BDT: NTrees={job['bdt_ntrees']}, MaxDepth={job['bdt_maxdepth']}")
            
            # Check for output files
            job_dir = Path(job['job_dir'])
            log_file = job_dir / "run.log"
            result_files = list(job_dir.glob("*_mva_results.root"))
            
            if log_file.exists():
                print(f"  Log: {log_file}")
            if result_files:
                print(f"  Results: {len(result_files)} files")
            
            if 'last_updated' in job:
                print(f"  Updated: {job['last_updated']}")
            print()
        
        print("Status summary:")
        for status, count in status_counts.items():
            print(f"  {status}: {count}")
        
        return True
    
    def merge_results(self) -> bool:
        """Merge job results back to shared data files."""
        print("=== Merging TMVA HPO results ===")
        
        # Check if shared data exists
        if not self.shared_data_dir.exists():
            print("✗ Shared data directory not found. Run 'setup' first.")
            return False
        
        # Find result files
        result_files = list(self.jobs_dir.glob("job_*/job_*_mva_results.root"))
        if not result_files:
            print("✗ No job result files found")
            return False
        
        print(f"Found {len(result_files)} result files")
        
        # Use the existing merge_results.py script
        merge_script = self.scripts_dir / "merge_results.py"
        if merge_script.exists():
            try:
                cmd = f"python {merge_script} --jobs-dir {self.jobs_dir}"
                result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
                
                if result.returncode == 0:
                    print("✓ Results merged successfully")
                    print(result.stdout)
                    return True
                else:
                    print("✗ Merge failed")
                    print(result.stderr)
                    return False
            except Exception as e:
                print(f"✗ Error running merge script: {e}")
                return False
        else:
            print("✗ Merge script not found")
            return False
    
    def clean_jobs(self, confirm: bool = False) -> bool:
        """Clean up job directories."""
        if not confirm:
            response = input("This will delete all job directories. Continue? (y/N): ")
            if response.lower() != 'y':
                print("Cancelled")
                return False
        
        print("=== Cleaning job directories ===")
        
        if self.jobs_dir.exists():
            shutil.rmtree(self.jobs_dir)
            print(f"✓ Removed {self.jobs_dir}")
        
        return True
    
    def decode_feature_set(self, feature_code: str) -> List[str]:
        """Decode FCAL feature set from binary encoding.
        
        Args:
            feature_code: Feature set code (e.g., 'fcal_1110000000')
            
        Returns:
            List of enabled feature names
        """
        # Extract binary part (remove 'fcal_' prefix)
        binary = feature_code.replace('fcal_', '')
        
        enabled_features = []
        for i, bit in enumerate(binary):
            if bit == '1' and i < len(self.fcal_features) and self.fcal_features[i]:
                # Get first feature name (positive side) and strip prefix
                feature_name = self.fcal_features[i][0].replace('Positive_', '')
                enabled_features.append(feature_name)
        
        return enabled_features
    
    def analyze_results(self, sort_by: str = "roc_auc_bdt", top_n: int = 10, 
                       output_file: Optional[str] = None) -> bool:
        """Analyze and rank job results.
        
        Args:
            sort_by: Metric to sort by ('roc_auc_mlp', 'roc_auc_bdt', 'separation_mlp', 'separation_bdt')
            top_n: Number of top results to display
            output_file: Optional CSV file to save results
            
        Returns:
            True if successful
        """
        print(f"=== Analyzing TMVA HPO Results ===")
        print(f"Sorting by: {sort_by}, Top {top_n}")
        print()
        
        # Collect metrics from all jobs
        metrics_data = self.collect_metrics()
        
        if "error" in metrics_data:
            print(f"✗ Error collecting metrics: {metrics_data['error']}")
            return False
        
        jobs = metrics_data.get("jobs", {})
        if not jobs:
            print("✗ No job metrics found")
            return False
        
        # Build ranking list
        ranking = []
        for job_id, job_data in jobs.items():
            metrics = job_data.get("metrics", {})
            averaged = metrics.get("averaged_metrics", {})
            
            if not averaged:
                continue
            
            config = job_data.get("job_config", {})
            feature_set = config.get("feature_set", "unknown")
            
            ranking.append({
                "job_id": job_id,
                "feature_set": feature_set,
                "features": self.decode_feature_set(feature_set),
                "roc_auc_mlp": averaged.get("roc_auc_mlp", 0),
                "roc_auc_bdt": averaged.get("roc_auc_bdt", 0),
                "separation_mlp": averaged.get("separation_mlp", 0),
                "separation_bdt": averaged.get("separation_bdt", 0),
                "mlp_hidden": config.get("mlp_hidden"),
                "mlp_ncycles": config.get("mlp_ncycles"),
                "bdt_ntrees": config.get("bdt_ntrees"),
                "bdt_maxdepth": config.get("bdt_maxdepth")
            })
        
        # Sort by selected metric
        ranking.sort(key=lambda x: x.get(sort_by, 0), reverse=True)
        
        # Display top N results
        print(f"Top {top_n} Feature Combinations (by {sort_by}):")
        print("=" * 120)
        print(f"{'Rank':<5} {'Job':<10} {'ROC_AUC_BDT':<12} {'ROC_AUC_MLP':<12} {'Sep_BDT':<10} {'Sep_MLP':<10} {'Features':<50}")
        print("=" * 120)
        
        for rank, result in enumerate(ranking[:top_n], 1):
            features_str = ', '.join(result['features'][:5])  # Show first 5 features
            if len(result['features']) > 5:
                features_str += f" +{len(result['features'])-5} more"
            
            print(f"{rank:<5} {result['job_id']:<10} {result['roc_auc_bdt']:<12.6f} {result['roc_auc_mlp']:<12.6f} "
                  f"{result['separation_bdt']:<10.6f} {result['separation_mlp']:<10.6f} {features_str:<50}")
        
        print("=" * 120)
        print()
        
        # Show feature set details for top 10
        print(f"Detailed Feature Sets (Top {min(top_n, len(ranking))}):")
        print("-" * 120)
        for rank, result in enumerate(ranking[:top_n], 1):
            print(f"\n{rank}. {result['job_id']} - {result['feature_set']} (ROC AUC BDT: {result['roc_auc_bdt']:.6f})")
            print(f"   Features ({len(result['features'])}): {', '.join(result['features'])}")
        print()
        
        # Save to CSV if requested
        if output_file:
            import csv
            with open(output_file, 'w', newline='') as csvfile:
                fieldnames = ['rank', 'job_id', 'feature_set', 'num_features', 'features', 
                             'roc_auc_bdt', 'roc_auc_mlp', 'separation_bdt', 'separation_mlp',
                             'mlp_hidden', 'mlp_ncycles', 'bdt_ntrees', 'bdt_maxdepth']
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()
                
                for rank, result in enumerate(ranking, 1):
                    writer.writerow({
                        'rank': rank,
                        'job_id': result['job_id'],
                        'feature_set': result['feature_set'],
                        'num_features': len(result['features']),
                        'features': '; '.join(result['features']),
                        'roc_auc_bdt': result['roc_auc_bdt'],
                        'roc_auc_mlp': result['roc_auc_mlp'],
                        'separation_bdt': result['separation_bdt'],
                        'separation_mlp': result['separation_mlp'],
                        'mlp_hidden': result['mlp_hidden'],
                        'mlp_ncycles': result['mlp_ncycles'],
                        'bdt_ntrees': result['bdt_ntrees'],
                        'bdt_maxdepth': result['bdt_maxdepth']
                    })
            
            print(f"✓ Results saved to: {output_file}")
        
        return True
    
    def show_config(self) -> bool:
        """Show current configuration."""
        print("=== TMVA HPO Configuration ===")
        print()
        print("Training Files:")
        print(f"  Signal:     {self.training_files['signal']['source']}")
        print(f"  Signal Tree: {self.training_files['signal']['tree']}")
        print(f"  Background: {self.training_files['background']['source']}")
        print(f"  Background Tree: {self.training_files['background']['tree']}")
        print()
        print("Application Files:")
        for name, config in self.data_files.items():
            print(f"  {name}: {config['source']}")
            print(f"  {name} Tree: {config['tree']}")
        print()
        print("Directories:")
        print(f"  Base: {self.base_dir}")
        print(f"  Jobs: {self.jobs_dir}")
        print(f"  Shared Data: {self.shared_data_dir}")
        return True
    
    def create_job_wrapper_script(self, job_id: str) -> Path:
        """Create a wrapper script for farm execution of a specific job.
        
        Args:
            job_id: Job identifier (e.g., 'job_0001')
            
        Returns:
            Path to the created wrapper script
        """
        job_dir = self.jobs_dir / job_id
        wrapper_script = job_dir / "run_job.sh"
        
        # Get job config to determine steps
        job_config = {}
        if self.index and 'jobs' in self.index:
            # Find job in list by job_id
            for job in self.index['jobs']:
                if job.get('job_id') == job_id:
                    job_config = job
                    break
        minus_only = job_config.get('minus_only', False)
        steps = "1-1" if minus_only else "1-7"
        
        script_content = f"""#!/bin/tcsh
# TMVA HPO Farm Job Wrapper Script
# Job: {job_id}
# Auto-generated by tmva_hpo_manager.py

# Change to job directory
cd {job_dir}

# Setup ROOT environment
gxenv

# Run TMVA workflow steps for this job
python {self.base_dir / 'tmva_hpo_manager.py'} --base-dir {self.base_dir} run --job-id {job_id} --steps {steps}

# Exit with the status of the last command
exit $status
"""
        
        with open(wrapper_script, 'w') as f:
            f.write(script_content)
        
        # Make executable
        wrapper_script.chmod(0o755)
        
        return wrapper_script
    
    def submit_to_swif2(self, workflow_name: str = None, account: str = "halld", 
                       partition: str = "production", os_type: str = "el9",
                       cores: int = 1, ram: str = "16GB", disk: str = "20GB", 
                       time_limit: str = "6h", job_ids: List[str] = None, 
                       resume: bool = False) -> bool:
        """Submit jobs to the batch farm using swif2.
        
        Args:
            workflow_name: Name for the swif2 workflow (default: tmva_hpo_<timestamp>)
            account: Batch account (default: halld)
            partition: Batch partition (default: production)
            os_type: OS requirement (default: el9)
            cores: Number of cores per job (default: 1)
            ram: RAM per job (default: 16GB)
            disk: Disk space per job (default: 20GB)
            time_limit: Time limit per job (default: 6h)
            job_ids: List of specific job IDs to submit (default: all jobs)
            resume: If True, only submit jobs with status='pending' or 'failed' (default: False)
            
        Returns:
            True if submission successful, False otherwise
        """
        print("=== Submitting TMVA HPO jobs to swif2 ===")
        
        # Auto-update index with completed job results if resuming
        if resume:
            print("\n✓ Resume mode: updating index with completed job results...")
            self.update_index_with_results()
            print()
        
        # Generate workflow name if not provided
        if not workflow_name:
            timestamp = time.strftime('%Y%m%d_%H%M%S')
            workflow_name = f"tmva_hpo_{timestamp}"
        
        print(f"Workflow: {workflow_name}")
        
        # Get list of jobs to submit
        index_path = self.jobs_dir / "index.json"
        if not index_path.exists():
            print("\u2717 No job index found. Run 'generate' first.")
            return False
        
        with open(index_path, 'r') as f:
            index_data = json.load(f)
        
        # Store index for use in create_job_wrapper_script
        self.index = index_data
        
        # Get all jobs from index
        if isinstance(index_data, list):
            all_jobs = index_data
        else:
            all_jobs = index_data.get('jobs', [])
        
        # Filter based on resume mode and job_ids
        if job_ids:
            # Submit only specified job IDs
            jobs_to_submit = [job for job in all_jobs 
                            if TMVAHPOManager.get_job_id(job) in job_ids]
        elif resume:
            # Resume mode: only submit jobs that don't have completed results
            # Check if job directory exists and if results files exist
            jobs_to_submit = []
            for job in all_jobs:
                job_id = TMVAHPOManager.get_job_id(job)
                job_dir = self.jobs_dir / job_id
                
                # Skip if job directory doesn't exist
                if not job_dir.exists():
                    continue
                
                # Check if results exist (TMVA output file)
                results_file = job_dir / "TMVApp.root"
                if not results_file.exists():
                    # No results yet - submit this job
                    jobs_to_submit.append(job)
            
            skipped_count = len(all_jobs) - len(jobs_to_submit)
            if skipped_count > 0:
                print(f"✓ Skipping {skipped_count} completed jobs (resume mode)")
        else:
            # Submit all jobs
            jobs_to_submit = all_jobs
        
        if not jobs_to_submit:
            print("✗ No jobs to submit (all completed or no jobs match criteria)")
            return False
        
        # Extract job IDs from job objects
        job_ids_to_submit = [TMVAHPOManager.get_job_id(job) for job in jobs_to_submit]
        
        print(f"Submitting {len(job_ids_to_submit)} jobs...")
        
        # Create workflow
        try:
            cmd = ["swif2", "create", "-workflow", workflow_name]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"✗ Failed to create workflow: {result.stderr}")
                return False
            print(f"✓ Created workflow: {workflow_name}")
        except Exception as e:
            print(f"✗ Error creating workflow: {e}")
            return False
        
        # Create wrapper scripts and add jobs to workflow
        farm_out = self.jobs_dir / "farm_logs"
        farm_out.mkdir(exist_ok=True)
        
        for job_id in job_ids_to_submit:
            job_dir = self.jobs_dir / job_id
            if not job_dir.exists():
                print(f"  ✗ Job directory not found: {job_id}")
                continue
            
            # Create wrapper script
            wrapper_script = self.create_job_wrapper_script(job_id)
            
            # Setup log files
            stdout_log = farm_out / f"{job_id}_stdout.log"
            stderr_log = farm_out / f"{job_id}_stderr.log"
            
            # Add job to workflow
            try:
                cmd = [
                    "swif2", "add-job",
                    "-workflow", workflow_name,
                    "-name", job_id,
                    "-account", account,
                    "-partition", partition,
                    "-os", os_type,
                    "-cores", str(cores),
                    "-ram", ram,
                    "-disk", disk,
                    "-time", time_limit,
                    "-stdout", str(stdout_log),
                    "-stderr", str(stderr_log),
                    str(wrapper_script)
                ]
                
                result = subprocess.run(cmd, capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"  ✗ Failed to add job {job_id}: {result.stderr}")
                else:
                    print(f"  ✓ Added job: {job_id}")
            except Exception as e:
                print(f"  ✗ Error adding job {job_id}: {e}")
        
        # Run the workflow
        try:
            cmd = ["swif2", "run", "-workflow", workflow_name]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"✗ Failed to run workflow: {result.stderr}")
                return False
            print(f"✓ Started workflow: {workflow_name}")
            print(f"\nMonitor with: swif2 status -workflow {workflow_name}")
        except Exception as e:
            print(f"✗ Error running workflow: {e}")
            return False
        
        return True


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='TMVA Hyperparameter Optimization Manager')
    parser.add_argument('--base-dir', help='Base directory for TMVA HPO system')
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # Setup command
    subparsers.add_parser('setup', help='Setup shared data files')
    
    # Config command
    subparsers.add_parser('config', help='Show current configuration')
    
    # Generate command
    gen_parser = subparsers.add_parser('generate', help='Generate job configurations')
    gen_parser.add_argument('--grid-size', choices=['full'], 
                          default='full', help='Comprehensive HPO grid (default: full)')
    gen_parser.add_argument('--signal-file', help='Path to signal training file')
    gen_parser.add_argument('--background-file', help='Path to background training file')
    gen_parser.add_argument('--signal-tree', default='flat_TMVA', help='Signal tree name')
    gen_parser.add_argument('--background-tree', default='flat_TMVA', help='Background tree name')
    gen_parser.add_argument('--start-id', type=int, help='Starting job ID (default: auto-detect from existing jobs)')
    gen_parser.add_argument('--append', action='store_true', help='Append to existing index.json instead of overwriting')
    
    # Run command
    run_parser = subparsers.add_parser('run', help='Run jobs')
    run_parser.add_argument('--job-id', help='Specific job to run')
    run_parser.add_argument('--step', type=int, choices=[1,2,3,4,5,6,7], help='Specific step to run')
    run_parser.add_argument('--parallel', action='store_true', help='Run jobs in parallel')
    run_parser.add_argument('--max-jobs', type=int, default=4, help='Maximum parallel jobs')
    run_parser.add_argument('--steps', help='Range of steps to run (e.g., "1-7")')
    
    # Submit command (swif2 batch farm submission)
    submit_parser = subparsers.add_parser('submit', help='Submit jobs to swif2 batch farm')
    submit_parser.add_argument('--workflow', help='Workflow name (default: auto-generated)')
    submit_parser.add_argument('--job-id', action='append', help='Specific job(s) to submit (can specify multiple)')
    submit_parser.add_argument('--account', default='halld', help='Batch account (default: halld)')
    submit_parser.add_argument('--partition', default='production', help='Batch partition (default: production)')
    submit_parser.add_argument('--os', default='el9', help='OS requirement (default: el9)')
    submit_parser.add_argument('--cores', type=int, default=1, help='Cores per job (default: 1)')
    submit_parser.add_argument('--ram', default='16GB', help='RAM per job (default: 16GB)')
    submit_parser.add_argument('--disk', default='10GB', help='Disk per job (default: 10GB)')
    submit_parser.add_argument('--time', default='6h', help='Time limit per job (default: 6h)')
    submit_parser.add_argument('--resume', action='store_true', help='Resume mode: only submit pending/failed jobs, skip completed')
    run_parser.add_argument('--interactive', action='store_true', help='Run jobs interactively with prompts')
    
    # Status command
    subparsers.add_parser('status', help='Show job status')
    
    # Merge command
    subparsers.add_parser('merge', help='Merge results')
    
    # Clean command
    clean_parser = subparsers.add_parser('clean', help='Clean up job directories')
    clean_parser.add_argument('--yes', action='store_true', help='Skip confirmation')
    
    # Collect metrics command
    collect_parser = subparsers.add_parser('collect', help='Collect metrics from jobs')
    collect_parser.add_argument('--job-id', help='Specific job to collect from')
    collect_parser.add_argument('--output', help='Output JSON file for metrics')
    
    # Analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze and rank results')
    analyze_parser.add_argument('--sort-by', 
                               choices=['roc_auc_mlp', 'roc_auc_bdt', 'separation_mlp', 'separation_bdt'],
                               default='roc_auc_bdt',
                               help='Metric to sort by (default: roc_auc_bdt)')
    analyze_parser.add_argument('--top', type=int, default=10,
                               help='Number of top results to display (default: 10)')
    analyze_parser.add_argument('--output', help='Save results to CSV file')
    
    # Update index command
    subparsers.add_parser('update-index', help='Update index.json with results from completed jobs')
    
    # Export for visualization
    export_parser = subparsers.add_parser('export-parallel-coords', 
                                          help='Export data for parallel coordinate plots')
    export_parser.add_argument('--output', default='parallel_coords_data.json',
                              help='Output file base name (default: parallel_coords_data.json)')
    
    # Trial management commands
    create_trial_parser = subparsers.add_parser('create-trial', help='Create new trial')
    create_trial_parser.add_argument('--hyperparams', required=True,
                                   help='JSON string with hyperparameters')
    
    submit_trial_parser = subparsers.add_parser('submit-trial', help='Submit trial jobs')
    submit_trial_parser.add_argument('--trial', type=int, required=True,
                                   help='Trial number to submit')
    
    monitor_trial_parser = subparsers.add_parser('monitor-trial', help='Monitor trial')
    monitor_trial_parser.add_argument('--trial', type=int, required=True,
                                    help='Trial number to monitor')
    
    list_trials_parser = subparsers.add_parser('list-trials', help='List all trials')
    
    best_trial_parser = subparsers.add_parser('best-trial', help='Get best trial')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    # Create manager instance
    manager = TMVAHPOManager(args.base_dir)
    
    # Execute command
    if args.command == 'setup':
        success = manager.setup_shared_data()
    elif args.command == 'generate':
        # Configure training files if specified
        if args.signal_file or args.background_file:
            signal_file = args.signal_file or manager.training_files["signal"]["source"]
            background_file = args.background_file or manager.training_files["background"]["source"]
            manager.configure_training_files(signal_file, background_file, 
                                           args.signal_tree, args.background_tree)
        success = manager.generate_jobs(grid_size=args.grid_size, start_id=args.start_id, append=args.append)
    elif args.command == 'run':
        success = manager.run_jobs(args.job_id, args.step, args.parallel, args.max_jobs, args.interactive, steps_range=args.steps)
    elif args.command == 'submit':
        # Submit jobs to swif2 batch farm
        success = manager.submit_to_swif2(
            workflow_name=args.workflow,
            account=args.account,
            partition=args.partition,
            os_type=args.os,
            cores=args.cores,
            ram=args.ram,
            disk=args.disk,
            time_limit=args.time,
            job_ids=args.job_id,
            resume=args.resume
        )
    elif args.command == 'status':
        success = manager.show_status()
    elif args.command == 'merge':
        success = manager.merge_results()
    elif args.command == 'clean':
        success = manager.clean_jobs(args.yes)
    elif args.command == 'collect':
        metrics = manager.collect_metrics(args.job_id)
        if args.output:
            with open(args.output, 'w') as f:
                json.dump(metrics, f, indent=2)
            print(f"\n✓ Metrics written to {args.output}")
        else:
            print("\n=== Collected Metrics ===")
            print(json.dumps(metrics, indent=2))
        success = metrics.get("error") is None
    elif args.command == 'analyze':
        success = manager.analyze_results(
            sort_by=args.sort_by,
            top_n=args.top,
            output_file=args.output
        )
    elif args.command == 'update-index':
        success = manager.update_index_with_results()
    elif args.command == 'export-parallel-coords':
        success = manager.export_for_parallel_coords(args.output)
    elif args.command == 'config':
        success = manager.show_config()
    elif args.command == 'create-trial':
        try:
            hyperparams = json.loads(args.hyperparams)
            trial_number = manager.create_trial(hyperparams)
            print(f"✓ Created trial {trial_number}")
            success = True
        except json.JSONDecodeError as e:
            print(f"✗ Invalid JSON hyperparameters: {e}")
            success = False
        except Exception as e:
            print(f"✗ Error creating trial: {e}")
            success = False
    elif args.command == 'submit-trial':
        success = manager.submit_trial_jobs(args.trial)
    elif args.command == 'monitor-trial':
        result = manager.monitor_trial_completion(args.trial)
        print(f"Trial {args.trial}: {result['message']}")
        if result['complete'] and 'results' in result:
            print(f"Results: {result['results']}")
        success = True
    elif args.command == 'list-trials':
        trials = manager.list_trials()
        if trials:
            print("=== All Trials ===")
            for trial in trials:
                status_icon = "✓" if trial['status'] == 'completed' else "⋯"
                roc_info = f" (ROC AUC: {trial['best_roc_auc']:.3f})" if trial['best_roc_auc'] else ""
                print(f"{status_icon} Trial {trial['trial_number']}: {trial['status']}{roc_info}")
                print(f"   Created: {trial['creation_time']}")
                print(f"   Hyperparameters: {trial['hyperparameters']}")
                print()
        else:
            print("No trials found")
        success = True
    elif args.command == 'best-trial':
        best = manager.get_best_trial()
        if best:
            print("=== Best Trial ===")
            print(f"Trial {best['trial_number']}: ROC AUC = {best['best_roc_auc']:.3f}")
            print(f"Created: {best['creation_time']}")
            print(f"Hyperparameters: {best['hyperparameters']}")
        else:
            print("No completed trials found")
        success = True
    else:
        print(f"Unknown command: {args.command}")
        return 1
    
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())