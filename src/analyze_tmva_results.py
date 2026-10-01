#!/usr/bin/env python3
"""
Utility script to analyze TMVA hyperparameter optimization results.
"""
import json
import os
import sys
import glob

def load_job_index(jobs_dir="jobs"):
    """Load the job index JSON file."""
    index_path = os.path.join(jobs_dir, "index.json")
    if not os.path.exists(index_path):
        print(f"Error: {index_path} not found")
        return None
    
    with open(index_path, 'r', encoding='utf-8') as f:
        return json.load(f)

def list_output_files(jobs_dir="jobs"):
    """List all output files from TMVA application runs."""
    pattern = os.path.join(jobs_dir, "job_*", "*_with_*_MVA.root")
    return glob.glob(pattern)

def analyze_job_results(job_id=None, jobs_dir="jobs"):
    """Analyze results for a specific job or all jobs."""
    index = load_job_index(jobs_dir)
    if not index:
        return
    
    if job_id is not None:
        # Filter to specific job
        jobs = [j for j in index if j['id'] == job_id]
        if not jobs:
            print(f"Job {job_id} not found")
            return
    else:
        jobs = index
    
    print("=== TMVA Hyperparameter Optimization Results ===")
    print()
    
    for job in jobs:
        print(f"Job {job['id']:04d}:")
        print(f"  Directory: {job['job_dir']}")
        print(f"  MLP: Hidden={job['mlp_hidden']}, NCycles={job['mlp_ncycles']}")
        print(f"  BDT: NTrees={job['bdt_ntrees']}, MaxDepth={job['bdt_maxdepth']}")
        
        # Check for output files
        job_dir = job['job_dir']
        output_files = glob.glob(os.path.join(job_dir, "*_with_*_MVA.root"))
        if output_files:
            print(f"  Output files: {len(output_files)} files")
            for f in output_files:
                basename = os.path.basename(f)
                print(f"    - {basename}")
        else:
            print("  Output files: None found")
        
        print()

def create_analysis_macro(jobs_dir="jobs"):
    """Create a ROOT macro to analyze all results."""
    index = load_job_index(jobs_dir)
    if not index:
        return
    
    macro_content = '''
// Auto-generated ROOT macro to analyze TMVA HPO results
#include <iostream>
#include <vector>
#include <string>
#include "TFile.h"
#include "TTree.h"
#include "TH1F.h"
#include "TCanvas.h"
#include "TLegend.h"

void analyze_tmva_results() {
    std::cout << "=== TMVA Hyperparameter Optimization Analysis ===" << std::endl;
    
    // Job configurations and their output files
'''
    
    for job in index:
        job_id = job['id']
        macro_content += f'''
    // Job {job_id:04d}: MLP({job['mlp_hidden']},{job['mlp_ncycles']}), BDT({job['bdt_ntrees']},{job['bdt_maxdepth']})
'''
        
        # Find output files for this job
        output_files = glob.glob(os.path.join(job['job_dir'], "*_with_*_MVA.root"))
        for output_file in output_files:
            rel_path = os.path.relpath(output_file)
            macro_content += f'''    // Output: {rel_path}
'''
    
    macro_content += '''
    
    std::cout << "Analysis complete. Add your ROOT analysis code here." << std::endl;
}
'''
    
    with open("analyze_results.C", "w", encoding='utf-8') as f:
        f.write(macro_content)
    
    print("Created analyze_results.C - a ROOT macro template for analyzing results")

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python analyze_tmva_results.py list              # List all output files")
        print("  python analyze_tmva_results.py analyze [job_id]  # Analyze results")
        print("  python analyze_tmva_results.py macro             # Create ROOT analysis macro")
        return
    
    command = sys.argv[1]
    
    if command == "list":
        files = list_output_files()
        print(f"Found {len(files)} output files:")
        for f in sorted(files):
            print(f"  {f}")
    
    elif command == "analyze":
        job_id = int(sys.argv[2]) if len(sys.argv) > 2 else None
        analyze_job_results(job_id)
    
    elif command == "macro":
        create_analysis_macro()
    
    else:
        print(f"Unknown command: {command}")

if __name__ == "__main__":
    main()