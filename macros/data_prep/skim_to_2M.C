// ROOT macro to skim a ROOT file to 2 million events
// Usage: root -l -b -q 'skim_to_2M.C("input.root", "TreeName", "output_skim.root")'

void skim_to_2M(const char* input_file, const char* tree_name, const char* output_file, Long64_t max_events = 2000000) {
    
    std::cout << "=== ROOT Tree Skimmer ===" << std::endl;
    std::cout << "Input file:   " << input_file << std::endl;
    std::cout << "Tree name:    " << tree_name << std::endl;
    std::cout << "Output file:  " << output_file << std::endl;
    std::cout << "Max events:   " << max_events << std::endl;
    std::cout << std::endl;
    
    // Open input file
    TFile* f_in = TFile::Open(input_file, "READ");
    if (!f_in || f_in->IsZombie()) {
        std::cerr << "ERROR: Cannot open input file: " << input_file << std::endl;
        return;
    }
    
    // Get input tree
    TTree* t_in = (TTree*)f_in->Get(tree_name);
    if (!t_in) {
        std::cerr << "ERROR: Cannot find tree '" << tree_name << "' in file" << std::endl;
        f_in->Close();
        return;
    }
    
    Long64_t total_entries = t_in->GetEntries();
    std::cout << "Input tree has " << total_entries << " entries" << std::endl;
    
    // Determine number of entries to copy
    Long64_t entries_to_copy = TMath::Min(max_events, total_entries);
    std::cout << "Will copy " << entries_to_copy << " entries" << std::endl;
    std::cout << std::endl;
    
    // Create output file
    TFile* f_out = TFile::Open(output_file, "RECREATE");
    if (!f_out || f_out->IsZombie()) {
        std::cerr << "ERROR: Cannot create output file: " << output_file << std::endl;
        f_in->Close();
        return;
    }
    
    // Clone tree structure (but no entries yet)
    TTree* t_out = t_in->CloneTree(0);
    
    // Copy entries with progress indicator
    std::cout << "Copying entries..." << std::endl;
    Long64_t update_interval = entries_to_copy / 20;  // Update every 5%
    if (update_interval == 0) update_interval = 1;
    
    for (Long64_t i = 0; i < entries_to_copy; i++) {
        t_in->GetEntry(i);
        t_out->Fill();
        
        if (i % update_interval == 0) {
            double progress = 100.0 * i / entries_to_copy;
            std::cout << "  Progress: " << std::fixed << std::setprecision(1) 
                     << progress << "% (" << i << "/" << entries_to_copy << ")" << std::endl;
        }
    }
    
    std::cout << "  Progress: 100.0% (" << entries_to_copy << "/" << entries_to_copy << ")" << std::endl;
    std::cout << std::endl;
    
    // Write output tree and get file size
    std::cout << "Writing output file..." << std::endl;
    t_out->Write();
    Long64_t output_entries = t_out->GetEntries();
    
    // Get file sizes
    Long64_t input_size = f_in->GetSize();
    Long64_t output_size = f_out->GetSize();
    
    // Close files
    f_out->Close();
    f_in->Close();
    
    // Print summary
    std::cout << std::endl;
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Input:  " << total_entries << " entries, " 
              << input_size / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Output: " << output_entries << " entries, " 
              << output_size / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Reduction: " << 100.0 * (1.0 - (double)output_entries / total_entries) 
              << "% entries, " 
              << 100.0 * (1.0 - (double)output_size / input_size) << "% file size" << std::endl;
    std::cout << std::endl;
    std::cout << "✓ Skimming complete!" << std::endl;
    
    delete f_out;
    delete f_in;
}
