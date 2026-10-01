void unroll_tracks(const char* input_file = "signal_skim_2M.root",
                   const char* input_tree = "Flat_FFS1_qDATAq_epemmissp__B2_BestX2",
                   const char* output_file = "signal_combined.root",
                   const char* output_tree = "combined_tracks") {
    
    cout << "==========================================" << endl;
    cout << "Track-Level Unrolling Macro" << endl;
    cout << "==========================================" << endl;
    cout << "Input:  " << input_file << " / " << input_tree << endl;
    cout << "Output: " << output_file << " / " << output_tree << endl;
    cout << endl;
    
    // Open input file
    TFile *f_in = TFile::Open(input_file);
    if (!f_in || f_in->IsZombie()) {
        cout << "ERROR: Cannot open input file!" << endl;
        return;
    }
    
    TTree *t_in = (TTree*)f_in->Get(input_tree);
    if (!t_in) {
        cout << "ERROR: Cannot find input tree!" << endl;
        return;
    }
    
    Long64_t n_events = t_in->GetEntries();
    cout << "Input events:  " << n_events << endl;
    cout << "Output events: " << (2 * n_events) << " (2x unrolled)" << endl;
    cout << endl;
    
    // Input branches (Positive and Negative versions)
    Float_t Positive_EoverPkin_FCAL, Negative_EoverPkin_FCAL;
    Float_t Positive_TrackFCAL_DOCA, Negative_TrackFCAL_DOCA;
    Float_t Positive_E9E25_FCAL, Negative_E9E25_FCAL;
    Float_t Positive_E1E9_FCAL, Negative_E1E9_FCAL;
    Float_t Positive_SumU_FCAL, Negative_SumU_FCAL;
    Float_t Positive_SumV_FCAL, Negative_SumV_FCAL;
    Float_t Positive_UV_Asymmetry_FCAL, Negative_UV_Asymmetry_FCAL;
    Float_t Positive_Saturation_FCAL, Negative_Saturation_FCAL;
    
    t_in->SetBranchAddress("Positive_EoverPkin_FCAL", &Positive_EoverPkin_FCAL);
    t_in->SetBranchAddress("Negative_EoverPkin_FCAL", &Negative_EoverPkin_FCAL);
    t_in->SetBranchAddress("Positive_TrackFCAL_DOCA", &Positive_TrackFCAL_DOCA);
    t_in->SetBranchAddress("Negative_TrackFCAL_DOCA", &Negative_TrackFCAL_DOCA);
    t_in->SetBranchAddress("Positive_E9E25_FCAL", &Positive_E9E25_FCAL);
    t_in->SetBranchAddress("Negative_E9E25_FCAL", &Negative_E9E25_FCAL);
    t_in->SetBranchAddress("Positive_E1E9_FCAL", &Positive_E1E9_FCAL);
    t_in->SetBranchAddress("Negative_E1E9_FCAL", &Negative_E1E9_FCAL);
    t_in->SetBranchAddress("Positive_SumU_FCAL", &Positive_SumU_FCAL);
    t_in->SetBranchAddress("Negative_SumU_FCAL", &Negative_SumU_FCAL);
    t_in->SetBranchAddress("Positive_SumV_FCAL", &Positive_SumV_FCAL);
    t_in->SetBranchAddress("Negative_SumV_FCAL", &Negative_SumV_FCAL);
    t_in->SetBranchAddress("Positive_UV_Asymmetry_FCAL", &Positive_UV_Asymmetry_FCAL);
    t_in->SetBranchAddress("Negative_UV_Asymmetry_FCAL", &Negative_UV_Asymmetry_FCAL);
    t_in->SetBranchAddress("Positive_Saturation_FCAL", &Positive_Saturation_FCAL);
    t_in->SetBranchAddress("Negative_Saturation_FCAL", &Negative_Saturation_FCAL);
    
    // Create output file and tree
    TFile *f_out = new TFile(output_file, "RECREATE");
    TTree *t_out = new TTree(output_tree, "Combined tracks (charge-agnostic)");
    
    // Output branches (no prefix - generic track features)
    Float_t EoverPkin_FCAL;
    Float_t TrackFCAL_DOCA;
    Float_t E9E25_FCAL;
    Float_t E1E9_FCAL;
    Float_t SumU_FCAL;
    Float_t SumV_FCAL;
    Float_t UV_Asymmetry_FCAL;
    Float_t Saturation_FCAL;
    
    t_out->Branch("EoverPkin_FCAL", &EoverPkin_FCAL, "EoverPkin_FCAL/F");
    t_out->Branch("TrackFCAL_DOCA", &TrackFCAL_DOCA, "TrackFCAL_DOCA/F");
    t_out->Branch("E9E25_FCAL", &E9E25_FCAL, "E9E25_FCAL/F");
    t_out->Branch("E1E9_FCAL", &E1E9_FCAL, "E1E9_FCAL/F");
    t_out->Branch("SumU_FCAL", &SumU_FCAL, "SumU_FCAL/F");
    t_out->Branch("SumV_FCAL", &SumV_FCAL, "SumV_FCAL/F");
    t_out->Branch("UV_Asymmetry_FCAL", &UV_Asymmetry_FCAL, "UV_Asymmetry_FCAL/F");
    t_out->Branch("Saturation_FCAL", &Saturation_FCAL, "Saturation_FCAL/F");
    
    // Progress tracking
    Long64_t progress_interval = n_events / 100;
    if (progress_interval < 1) progress_interval = 1;
    
    cout << "Processing..." << endl;
    
    // Loop over input events and write TWO output events per input
    for (Long64_t i = 0; i < n_events; i++) {
        t_in->GetEntry(i);
        
        // First output event: Positive track
        EoverPkin_FCAL = Positive_EoverPkin_FCAL;
        TrackFCAL_DOCA = Positive_TrackFCAL_DOCA;
        E9E25_FCAL = Positive_E9E25_FCAL;
        E1E9_FCAL = Positive_E1E9_FCAL;
        SumU_FCAL = Positive_SumU_FCAL;
        SumV_FCAL = Positive_SumV_FCAL;
        UV_Asymmetry_FCAL = Positive_UV_Asymmetry_FCAL;
        Saturation_FCAL = Positive_Saturation_FCAL;
        t_out->Fill();
        
        // Second output event: Negative track
        EoverPkin_FCAL = Negative_EoverPkin_FCAL;
        TrackFCAL_DOCA = Negative_TrackFCAL_DOCA;
        E9E25_FCAL = Negative_E9E25_FCAL;
        E1E9_FCAL = Negative_E1E9_FCAL;
        SumU_FCAL = Negative_SumU_FCAL;
        SumV_FCAL = Negative_SumV_FCAL;
        UV_Asymmetry_FCAL = Negative_UV_Asymmetry_FCAL;
        Saturation_FCAL = Negative_Saturation_FCAL;
        t_out->Fill();
        
        // Progress
        if (i % progress_interval == 0) {
            cout << "  " << (100 * i / n_events) << "% complete..." << endl;
        }
    }
    
    cout << "  100% complete!" << endl;
    cout << endl;
    
    // Write and close
    f_out->cd();
    t_out->Write();
    
    cout << "==========================================" << endl;
    cout << "Summary:" << endl;
    cout << "  Input events:    " << n_events << endl;
    cout << "  Output events:   " << t_out->GetEntries() << endl;
    cout << "  Output branches: " << t_out->GetNbranches() << endl;
    cout << "  Output file:     " << output_file << endl;
    cout << "  File size:       " << (f_out->GetSize() / 1024.0 / 1024.0) << " MB" << endl;
    cout << "==========================================" << endl;
    
    f_out->Close();
    f_in->Close();
    
    cout << "\nDone!" << endl;
}
