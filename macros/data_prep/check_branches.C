void check_branches() {
    TFile *f = TFile::Open("signal_skim_2M.root");
    TTree *t = (TTree*)f->Get("Flat_FFS1_qDATAq_epemmissp__B2_BestX2");
    
    // Print all branches with "Positive_" or "Negative_" or "FCAL" in name
    TObjArray *branches = t->GetListOfBranches();
    cout << "\n=== Branches with FCAL features ===" << endl;
    for (int i = 0; i < branches->GetEntries(); i++) {
        TString name = branches->At(i)->GetName();
        if (name.Contains("Positive_") || name.Contains("Negative_")) {
            if (name.Contains("FCAL") && 
                (name.Contains("EoverPkin") || name.Contains("DOCA") || 
                 name.Contains("E9E25") || name.Contains("E1E9") ||
                 name.Contains("SumU") || name.Contains("SumV") ||
                 name.Contains("UV_Asymmetry") || name.Contains("Saturation"))) {
                cout << name << endl;
            }
        }
    }
    
    cout << "\nTotal entries: " << t->GetEntries() << endl;
}
