#!/usr/bin/env python3
"""
Merge TMVA MVA results from individual job result files back into shared data files.
"""
import os
import glob
import json
import argparse
import subprocess
import tempfile

def find_job_results(jobs_dir="jobs"):
    """Find all job result files."""
    pattern = os.path.join(jobs_dir, "job_*", "job_*_mva_results.root")
    return glob.glob(pattern)

def load_job_index(jobs_dir="jobs"):
    """Load the job index to understand job configurations."""
    index_path = os.path.join(jobs_dir, "index.json")
    with open(index_path, 'r', encoding='utf-8') as f:
        return json.load(f)

def create_merge_macro(shared_data_dir, job_results, output_file):
    """Create a ROOT macro to merge MVA results into the shared data file."""
    
    # Group results by input file
    pi0pippim_results = []
    gepem_results = []
    
    for result_file in job_results:
        # Determine which input file this result corresponds to based on job configuration
        # For now, we'll need to check both files - this could be improved
        pi0pippim_results.append(result_file)
        gepem_results.append(result_file)
    
    macro_content = f'''
// Auto-generated ROOT macro to merge MVA results
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"

void merge_mva_results() {{
    std::cout << "=== Merging MVA Results ===" << std::endl;
    
    // List of result files to process
    std::vector<std::string> result_files = {{
'''
    
    for result_file in job_results:
        macro_content += f'        "{result_file}",\n'
    
    macro_content += f'''    }};
    
    // Process pi0pippim file
    std::string pi0pippim_file = "{shared_data_dir}/FlatTree_pi0pippim_2018-01_ver02.root";
    merge_to_target(pi0pippim_file, result_files, "FLAT_pip_TMVA");
    
    // Process gepem file  
    std::string gepem_file = "{shared_data_dir}/gepem_DATA_FlatTrees_pi0CUT.root";
    merge_to_target(gepem_file, result_files, "Flat2018_Data_gepem");
    
    std::cout << "=== Merge Complete ===" << std::endl;
}}

void merge_to_target(const std::string& target_file, const std::vector<std::string>& result_files, const std::string& tree_name) {{
    std::cout << "Processing: " << target_file << std::endl;
    
    // Open target file in UPDATE mode
    TFile* target = TFile::Open(target_file.c_str(), "UPDATE");
    if (!target || target->IsZombie()) {{
        std::cerr << "Error: Could not open target file: " << target_file << std::endl;
        return;
    }}
    
    TTree* target_tree = (TTree*)target->Get(tree_name.c_str());
    if (!target_tree) {{
        std::cerr << "Error: Could not find tree " << tree_name << " in " << target_file << std::endl;
        target->Close();
        return;
    }}
    
    Long64_t n_events = target_tree->GetEntries();
    std::cout << "Target tree has " << n_events << " events" << std::endl;
    
    // Create maps to store all MVA responses
    std::map<std::string, std::vector<Float_t>> mva_responses;
    
    // Process each result file
    for (const auto& result_file : result_files) {{
        TFile* rf = TFile::Open(result_file.c_str(), "READ");
        if (!rf || rf->IsZombie()) continue;
        
        TTree* result_tree = (TTree*)rf->Get("MVAResults");
        if (!result_tree) {{
            rf->Close();
            continue;
        }}
        
        // Check if this result file matches our target (by checking metadata or event count)
        if (result_tree->GetEntries() != n_events) {{
            rf->Close();
            continue;
        }}
        
        std::cout << "  Processing result file: " << result_file << std::endl;
        
        // Read branches from result file
        Int_t event_index;
        Float_t mlp_plus = -999, mlp_minus = -999;
        Float_t bdt_plus = -999, bdt_minus = -999;
        
        result_tree->SetBranchAddress("event_index", &event_index);
        
        // Try to read PLUS and MINUS branches (they may not all exist in every file)
        TBranch* b_mlp_plus = result_tree->GetBranch("MLP_PLUS");
        TBranch* b_mlp_minus = result_tree->GetBranch("MLP_MINUS");
        TBranch* b_bdt_plus = result_tree->GetBranch("BDT_PLUS");
        TBranch* b_bdt_minus = result_tree->GetBranch("BDT_MINUS");
        
        if (b_mlp_plus) result_tree->SetBranchAddress("MLP_PLUS", &mlp_plus);
        if (b_mlp_minus) result_tree->SetBranchAddress("MLP_MINUS", &mlp_minus);
        if (b_bdt_plus) result_tree->SetBranchAddress("BDT_PLUS", &bdt_plus);
        if (b_bdt_minus) result_tree->SetBranchAddress("BDT_MINUS", &bdt_minus);
        
        // Extract job ID from filename for branch naming
        std::string job_id = "unknown";
        size_t pos = result_file.find("job_");
        if (pos != std::string::npos) {{
            size_t end_pos = result_file.find("_", pos + 4);
            if (end_pos != std::string::npos) {{
                job_id = result_file.substr(pos + 4, end_pos - pos - 4);
            }}
        }}
        
        // Initialize response vectors if needed
        if (b_mlp_plus && mva_responses.find("MLP_j" + job_id + "_PLUS") == mva_responses.end()) {{
            mva_responses["MLP_j" + job_id + "_PLUS"].resize(n_events, -999);
        }}
        if (b_mlp_minus && mva_responses.find("MLP_j" + job_id + "_MINUS") == mva_responses.end()) {{
            mva_responses["MLP_j" + job_id + "_MINUS"].resize(n_events, -999);
        }}
        if (b_bdt_plus && mva_responses.find("BDT_j" + job_id + "_PLUS") == mva_responses.end()) {{
            mva_responses["BDT_j" + job_id + "_PLUS"].resize(n_events, -999);
        }}
        if (b_bdt_minus && mva_responses.find("BDT_j" + job_id + "_MINUS") == mva_responses.end()) {{
            mva_responses["BDT_j" + job_id + "_MINUS"].resize(n_events, -999);
        }}
        
        // Read all entries and store responses
        for (Long64_t i = 0; i < result_tree->GetEntries(); ++i) {{
            result_tree->GetEntry(i);
            
            if (event_index >= 0 && event_index < n_events) {{
                if (b_mlp_plus && mlp_plus > -998) {{
                    mva_responses["MLP_j" + job_id + "_PLUS"][event_index] = mlp_plus;
                }}
                if (b_mlp_minus && mlp_minus > -998) {{
                    mva_responses["MLP_j" + job_id + "_MINUS"][event_index] = mlp_minus;
                }}
                if (b_bdt_plus && bdt_plus > -998) {{
                    mva_responses["BDT_j" + job_id + "_PLUS"][event_index] = bdt_plus;
                }}
                if (b_bdt_minus && bdt_minus > -998) {{
                    mva_responses["BDT_j" + job_id + "_MINUS"][event_index] = bdt_minus;
                }}
            }}
        }}
        
        rf->Close();
    }}
    
    // Add new branches to target tree
    std::map<std::string, Float_t*> branch_vars;
    std::map<std::string, TBranch*> new_branches;
    
    for (auto& pair : mva_responses) {{
        const std::string& branch_name = pair.first;
        branch_vars[branch_name] = new Float_t(-999);
        new_branches[branch_name] = target_tree->Branch(branch_name.c_str(), branch_vars[branch_name]);
        std::cout << "  Added branch: " << branch_name << std::endl;
    }}
    
    // Fill new branches
    for (Long64_t i = 0; i < n_events; ++i) {{
        for (auto& pair : mva_responses) {{
            const std::string& branch_name = pair.first;
            *branch_vars[branch_name] = pair.second[i];
        }}
        
        // Fill only the new branches for this entry
        for (auto& pair : new_branches) {{
            pair.second->Fill();
        }}
    }}
    
    // Write and close
    target_tree->Write("", TObject::kOverwrite);
    target->Close();
    
    std::cout << "  Completed merging to: " << target_file << std::endl;
}}
'''
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(macro_content)

def main():
    parser = argparse.ArgumentParser(description='Merge TMVA MVA results into shared data files')
    parser.add_argument('--jobs-dir', default='jobs', help='Jobs directory')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done without doing it')
    
    args = parser.parse_args()
    
    jobs_dir = args.jobs_dir
    shared_data_dir = os.path.join(jobs_dir, 'shared_data')
    
    if not os.path.exists(shared_data_dir):
        print(f"Error: Shared data directory not found: {shared_data_dir}")
        print("Run setup_shared_data.tcsh first")
        return 1
    
    # Find all job result files
    result_files = find_job_results(jobs_dir)
    if not result_files:
        print("No job result files found")
        return 1
    
    print(f"Found {len(result_files)} job result files:")
    for f in sorted(result_files):
        print(f"  {f}")
    print()
    
    if args.dry_run:
        print("DRY RUN - would merge these files")
        return 0
    
    # Create merge macro
    macro_file = "merge_mva_results.C"
    create_merge_macro(shared_data_dir, result_files, macro_file)
    
    print(f"Created merge macro: {macro_file}")
    print("Running ROOT macro to merge results...")
    
    # Run the ROOT macro
    cmd = ["root", "-l", "-b", "-q", macro_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode == 0:
        print("✓ Merge completed successfully")
        print("\nMerged files available at:")
        merged_files = [
            os.path.join(shared_data_dir, "FlatTree_pi0pippim_2018-01_ver02.root"),
            os.path.join(shared_data_dir, "gepem_DATA_FlatTrees_pi0CUT.root")
        ]
        for f in merged_files:
            if os.path.exists(f):
                print(f"  {f}")
    else:
        print("✗ Merge failed")
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())