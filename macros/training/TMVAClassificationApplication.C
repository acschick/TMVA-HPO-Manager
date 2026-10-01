/// \file
/// \ingroup tutorial_tmva
/// \notebook -nodraw
/// This macro provides a simple example on how to use the trained classifiers
/// within an analysis module
/// - Project   : TMVA - a Root-integrated toolkit for multivariate data analysis
/// - Package   : TMVA
/// - Exectuable: TMVAClassificationApplication
///
/// \macro_output
/// \macro_code
/// \author Andreas Hoecker

#include <cstdlib>
#include <iostream>
#include <map>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TSystem.h"
#include "TROOT.h"
#include "TStopwatch.h"

#include "TMVA/Tools.h"
#include "TMVA/Reader.h"
#include "TMVA/MethodCuts.h"

using namespace TMVA;

enum class Role { Var, Spec };

struct VarSpec {
  std::string expr;   // branch name or "Alias := expression"
  std::string title;  // not used by Reader, but we keep it
  char type;          // 'F'
  Role role;
};


static inline std::string trim(std::string s) {
  auto l = s.find_first_not_of(" \t\r");
  if (l == std::string::npos) return "";
  auto r = s.find_last_not_of(" \t\r");
  return s.substr(l, r - l + 1);
}
static inline bool hasAlias(const std::string& e) { return e.find(":=") != std::string::npos; }
static inline std::string getAlias(const std::string& e) {
  auto p = e.find(":="); return p==std::string::npos ? std::string{} : trim(e.substr(0,p));
}
static inline std::string infer_title(const std::string& expr) {
  auto pos = expr.find(":="); if (pos != std::string::npos) return trim(expr.substr(0, pos));
  return expr;
}

static std::vector<VarSpec> load_vars_ini(const std::string& path, const std::string& side = "") {
   std::ifstream in(path);
   std::vector<VarSpec> out;
   if (!in) { std::cerr << "Could not open " << path << "\n"; return out; }
   enum Sect { NONE, TRAIN, SPEC, TRAIN_PLUS, TRAIN_MINUS } sect = NONE;
  std::string line;
  while (std::getline(in, line)) {
    std::string s = trim(line);
    if (s.empty()) continue;
    if (s[0] == '#') continue;
    if (s.size() >= 2 && s[0] == '/' && s[1] == '/') continue;
      // Check for section headers first (any [xxx])
      if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
        if (s == "[training]")        { sect = TRAIN; continue; }
        if (s == "[training_plus]")   { sect = TRAIN_PLUS; continue; }
        if (s == "[training_minus]")  { sect = TRAIN_MINUS; continue; }
        if (s == "[spectators]")      { sect = SPEC;  continue; }
        // Unknown section - stop processing (don't treat as variable)
        sect = NONE;
        continue;
      }
    if (sect == NONE) { continue; }  // Skip lines outside recognized sections

    // Optional trailing type: "expr | F" or "expr|F"
    char type = 'F';
    auto bar = s.find('|');
    if (bar != std::string::npos) {
      std::string left  = trim(s.substr(0, bar));
      std::string right = trim(s.substr(bar + 1));
      if (!right.empty()) type = std::toupper(right[0]);
      s = left;
    }

      VarSpec v;
      v.expr  = s;                 // can be plain or "Alias := expr"
      v.title = infer_title(s);    // nice label
      v.type  = type;
      // Decide whether this token is a training variable or a spectator depending on
      // the section and the requested side. [training] remains training for both.
      if (sect == TRAIN) v.role = Role::Var;
      else if (sect == TRAIN_PLUS)  v.role = (side == "PLUS" ) ? Role::Var : Role::Spec;
      else if (sect == TRAIN_MINUS) v.role = (side == "MINUS") ? Role::Var : Role::Spec;
      else v.role = Role::Spec;
    out.push_back(std::move(v));
  }
  return out;
}


void TMVAClassificationApplication( TString myMethodList = "" , 
                                    const std::string& side = "",
                                    const std::string& input_file_arg = "",
                                    const std::string& input_tree_arg = "" )
{

   //---------------------------------------------------------------
   // This loads the library
   TMVA::Tools::Instance();

   // Default MVA methods to be trained + tested
   std::map<std::string,int> Use;

   // Cut optimisation
   Use["Cuts"]            = 0;
   Use["CutsD"]           = 0;
   Use["CutsPCA"]         = 0;
   Use["CutsGA"]          = 0;
   Use["CutsSA"]          = 0;
   //
   // 1-dimensional likelihood ("naive Bayes estimator")
   Use["Likelihood"]      = 0;
   Use["LikelihoodD"]     = 0; // the "D" extension indicates decorrelated input variables (see option strings)
   Use["LikelihoodPCA"]   = 0; // the "PCA" extension indicates PCA-transformed input variables (see option strings)
   Use["LikelihoodKDE"]   = 0;
   Use["LikelihoodMIX"]   = 0;
   //
   // Mutidimensional likelihood and Nearest-Neighbour methods
   Use["PDERS"]           = 0;
   Use["PDERSD"]          = 0;
   Use["PDERSPCA"]        = 0;
   Use["PDEFoam"]         = 0;
   Use["PDEFoamBoost"]    = 0; // uses generalised MVA method boosting
   Use["KNN"]             = 0; // k-nearest neighbour method
   //
   // Linear Discriminant Analysis
   Use["LD"]              = 0; // Linear Discriminant identical to Fisher
   Use["Fisher"]          = 0;
   Use["FisherG"]         = 0;
   Use["BoostedFisher"]   = 0; // uses generalised MVA method boosting
   Use["HMatrix"]         = 0;
   //
   // Function Discriminant analysis
   Use["FDA_GA"]          = 0; // minimisation of user-defined function using Genetics Algorithm
   Use["FDA_SA"]          = 0;
   Use["FDA_MC"]          = 0;
   Use["FDA_MT"]          = 0;
   Use["FDA_GAMT"]        = 0;
   Use["FDA_MCMT"]        = 0;
   //
   // Neural Networks (all are feed-forward Multilayer Perceptrons)
   Use["MLP"]             = 0; // Recommended ANN
   Use["MLPBFGS"]         = 0; // Recommended ANN with optional training method
   Use["MLPBNN"]          = 0; // Recommended ANN with BFGS training method and bayesian regulator
   Use["CFMlpANN"]        = 0; // Depreciated ANN from ALEPH
   Use["TMlpANN"]         = 0; // ROOT's own ANN
   Use["DNN"]             = 0; // improved implementation of a NN
   //
   // Support Vector Machine
   Use["SVM"]             = 0;
   //
   // Boosted Decision Trees
   Use["BDT"]             = 1; // uses Adaptive Boost
   Use["BDTG"]            = 0; // uses Gradient Boost
   Use["BDTB"]            = 0; // uses Bagging
   Use["BDTD"]            = 0; // decorrelation + Adaptive Boost
   Use["BDTF"]            = 0; // allow usage of fisher discriminant for node splitting
   //
   // Friedman's RuleFit method, ie, an optimised series of cuts ("rules")
   Use["RuleFit"]         = 0;
   // ---------------------------------------------------------------
   Use["Plugin"]          = 0;
   Use["Category"]        = 0;
   Use["SVM_Gauss"]       = 0;
   Use["SVM_Poly"]        = 0;
   Use["SVM_Lin"]         = 0;

   std::cout << std::endl;
   std::cout << "==> Start TMVAClassificationApplication" << std::endl;

   // Select methods (don't look at this code - not of interest)
   if (myMethodList != "") {
      for (std::map<std::string,int>::iterator it = Use.begin(); it != Use.end(); it++) it->second = 0;

      std::vector<TString> mlist = gTools().SplitString( myMethodList, ',' );
      for (UInt_t i=0; i<mlist.size(); i++) {
         std::string regMethod(mlist[i]);

         if (Use.find(regMethod) == Use.end()) {
            std::cout << "Method \"" << regMethod
                      << "\" not known in TMVA under this name. Choose among the following:" << std::endl;
            for (std::map<std::string,int>::iterator it = Use.begin(); it != Use.end(); it++) {
               std::cout << it->first << " ";
            }
            std::cout << std::endl;
            return;
         }
         Use[regMethod] = 1;
      }
   }

   // --------------------------------------------------------------------------------------------------






   
   TFile *input(0);

   // Determine input file and tree name. Use function arguments if provided,
   // otherwise fall back to environment variables, then to default HTTP example.
   std::string input_file = input_file_arg;
   
   // Fall back to environment variables if argument not provided
   if (input_file.empty()) {
      if (const char* e = std::getenv("TMVA_APP_INPUT")) input_file = std::string(e);
      else if (const char* e2 = std::getenv("DATA_INPUT_FILE")) input_file = std::string(e2);
   }

   TString fname(input_file.c_str());
   if (!gSystem->AccessPathName( fname )) {
      input = TFile::Open( fname ); // local file
   } else if (!input_file.empty()) {
      // requested file not found
      std::cerr << "Requested input file '" << input_file << "' not found locally; trying default HTTP example" << std::endl;
      input = TFile::Open( "http://root.cern.ch/files/tmva_class_example.root" );
   } else {
      input = TFile::Open( "http://root.cern.ch/files/tmva_class_example.root" );
   }

   if (!input) {
      std::cout << "ERROR: could not open data file" << std::endl;
      exit(1);
   }
   std::cout << "--- TMVAClassificationApp    : Using input file: " << input->GetName() << std::endl;


   // Helper to fetch a TTree by preferred name then fall back to the first TTree
   auto fetch_tree = [&](TFile* f, const std::string& prefer)->TTree* {
      if (!f) return nullptr;
      if (!prefer.empty()) {
         TTree* t = (TTree*)f->Get(prefer.c_str());
         if (t) return t;
      }
      // try some common names
      TTree* t = (TTree*)f->Get("flat_TMVA"); if (t) return t;
      t = (TTree*)f->Get("Flat2018_MC_Electrons"); if (t) return t;
      t = (TTree*)f->Get("FLAT_pip_TMVA"); if (t) return t;
      t = (TTree*)f->Get("Flat2018_Data_gepem"); if (t) return t;
      // fallback: first TTree in file
      TKey* key = nullptr;
      TIter next(f->GetListOfKeys());
      while ((key = (TKey*)next())) {
         TObject* obj = key->ReadObj();
         if (obj && obj->InheritsFrom("TTree")) return (TTree*)obj;
      }
      return nullptr;
   };

   // Preferred tree name from function argument or environment variable
   std::string prefer_tree = input_tree_arg;
   
   // Fall back to environment variables if argument not provided
   if (prefer_tree.empty()) {
      if (const char* tenv = std::getenv("TMVA_APP_TREE")) prefer_tree = std::string(tenv);
      else if (const char* t2 = std::getenv("DATA_TREE_NAME")) prefer_tree = std::string(t2);
   }

   TTree* theTree = fetch_tree(input, prefer_tree);





 
  
      // Determine vars file to use. Look in current directory (job manager cd's there first).
   // Priority: vars.ini, vars_plus.ini, vars_minus.ini
   std::string varfile = "vars.ini";
   if (!gSystem->AccessPathName("vars.ini")) {
      varfile = "vars.ini";
   } else if (!gSystem->AccessPathName("vars_plus.ini")) {
      varfile = "vars_plus.ini";
   } else if (!gSystem->AccessPathName("vars_minus.ini")) {
      varfile = "vars_minus.ini";
   }

   std::cout << "TMVApp: Using vars file: " << varfile << " (full path: " << gSystem->WorkingDirectory() << "/" << varfile << ")" << std::endl;

   // Determine side: prefer TMVA_APP_SIDE env, then function parameter 'side'
   std::string app_side = side;
   if (const char* senv = std::getenv("TMVA_APP_SIDE")) app_side = std::string(senv);
   
   std::cout << "TMVApp: Loading vars with side='" << app_side << "'" << std::endl;

   auto VARS = load_vars_ini(varfile, app_side);
   
   // Count variables vs spectators for debugging
   int var_count = 0, spec_count = 0;
   for (const auto& v : VARS) {
      if (v.role == Role::Var) var_count++;
      else spec_count++;
   }
   std::cout << "TMVApp: Loaded " << var_count << " variables and " << spec_count << " spectators from " << varfile << std::endl;
   
   // Print first few variables for debugging
   std::cout << "TMVApp: Variables:" << std::endl;
   int printed_vars = 0;
   for (const auto& v : VARS) {
      if (v.role == Role::Var) {
         std::cout << "  - " << v.expr << std::endl;
         if (++printed_vars >= 10) { std::cout << "  ... (" << (var_count - 10) << " more)" << std::endl; break; }
      }
   }
   
   // Print ALL spectators in comma-separated list
   std::cout << "TMVApp: All Spectators: ";
   bool first = true;
   for (const auto& v : VARS) {
      if (v.role == Role::Spec) {
         if (!first) std::cout << ", ";
         std::cout << v.expr;
         first = false;
      }
   }
   std::cout << std::endl;
      // Try to detect dataset name and job directory. If vars file contains an explicit
      // line like 'dataset=datasetPLUS' (in a [meta] section) we will prefer it; otherwise
      // infer from the varfile name (plus/minus) as a fallback.
   std::string datasetName = "dataset";
      // quick scan for dataset= in the file
      std::ifstream vf(varfile);
      if (vf) {
         std::string line;
         while (std::getline(vf, line)) {
            std::string s = trim(line);
            if (s.size() >= 8) {
               std::string low = s;
               for (auto &c: low) c = std::tolower((unsigned char)c);
               if (low.rfind("dataset=", 0) == 0) {
                  datasetName = trim(s.substr(8));
                  break;
               }
            }
         }
         vf.close();
      }
      if (datasetName == "dataset") {
         std::string lf = varfile;
         for (auto &c : lf) c = std::tolower((unsigned char)c);
         if (lf.find("plus") != std::string::npos) datasetName = "datasetPLUS";
         else if (lf.find("minus") != std::string::npos) datasetName = "datasetMINUS";
         // prefer explicit app_side if set
         if (!app_side.empty()) {
            if (app_side == "PLUS") datasetName = "datasetPLUS";
            else if (app_side == "MINUS") datasetName = "datasetMINUS";
         }
      }

      // Determine job directory (directory part of varfile if provided), else '.'
      std::string job_dir = ".";
      size_t p = varfile.find_last_of("/\\");
      if (p != std::string::npos) job_dir = varfile.substr(0,p);

      // Build weight directory path. TMVA may write weights directly to datasetName/
      // or to datasetName/weights/ depending on configuration. We'll check both.
      std::string weight_dir = job_dir;
      if (!weight_dir.empty() && weight_dir.back() != '/' && weight_dir.back() != '\\') weight_dir += "/";
      weight_dir += datasetName;
      
      // Check if weights are in datasetName/weights/ (old behavior) or datasetName/ (new behavior)
      std::string dir_with_weights = weight_dir + "/weights/";
      std::string dir_without_weights = weight_dir + "/";
      
      // Determine which directory to use by checking for a sample weight file
      std::string test_file = dir_without_weights + "TMVAClassification_MLP.weights.xml";
      bool weights_in_root = (gSystem->AccessPathName(test_file.c_str()) == 0);
      
      std::string dir = weights_in_root ? dir_without_weights : dir_with_weights;
      
      std::cout << "Looking for weights in: " << dir << std::endl;
   
   // Load vars.ini as before -> std::vector<VarSpec> VARS
   auto hasAlias = [](const std::string& e){ return e.find(":=")!=std::string::npos; };
   auto getAlias = [](const std::string& e){
     auto p=e.find(":="); return p==std::string::npos ? std::string{} : trim(e.substr(0,p));
   };
   
   // Storage for all tokens (both vars and specs)
   std::map<std::string, Float_t> value;
   
   // Create the reader
   TMVA::Reader *reader = new TMVA::Reader("!Color:!Silent");
   
   // Register exactly the same *training* variables used in training
   // For variables: use token (alias if present, else full expr)
   // For spectators: use full expr to match what Training does
   for (const auto& v : VARS) {
     const bool aliased = hasAlias(v.expr);
     const std::string token = aliased ? getAlias(v.expr) : v.expr;
     
     if (v.role == Role::Var) {
       reader->AddVariable(token.c_str(), &value[token]);   // <-- only training vars here
     } else {
       // For spectators, use the full expression (matching Training's dataloader->AddSpectator)
       reader->AddSpectator(v.expr.c_str(), &value[token]);
     }
   }
   
   // CRITICAL: Connect tree branches to the value map so data is actually loaded!
   // This tells the tree to fill our value map when we call GetEntry()
   for (const auto& v : VARS) {
     const bool aliased = hasAlias(v.expr);
     const std::string token = aliased ? getAlias(v.expr) : v.expr;
     
     // Check if branch exists in the tree
     if (theTree->GetBranch(token.c_str())) {
       theTree->SetBranchAddress(token.c_str(), &value[token]);
     } else {
       std::cerr << "WARNING: Branch '" << token << "' not found in tree!" << std::endl;
     }
   }
   
   

   
   // Book the MVA methods
   TString dirT(dir.c_str());
   TString prefix = "TMVAClassification";

   // Book method(s)
   for (std::map<std::string,int>::iterator it = Use.begin(); it != Use.end(); it++) {
      if (it->second) {
         TString methodName = TString(it->first) + TString(" method");
         TString weightfile = TString(dir) + prefix + TString("_") + TString(it->first) + TString(".weights.xml");
         reader->BookMVA( methodName, weightfile );
      }
   }

   // Book output histograms
   UInt_t nbin = 100;
   TH1F   *histLk(0), *histLkD(0), *histLkPCA(0), *histLkKDE(0), *histLkMIX(0), *histPD(0), *histPDD(0);
   TH1F   *histPDPCA(0), *histPDEFoam(0), *histPDEFoamErr(0), *histPDEFoamSig(0), *histKNN(0), *histHm(0);
   TH1F   *histFi(0), *histFiG(0), *histFiB(0), *histLD(0), *histNn(0),*histNnbfgs(0),*histNnbnn(0);
   TH1F   *histNnC(0), *histNnT(0), *histNdn(0), *histBdt(0), *histBdtG(0), *histBdtB(0), *histBdtD(0);
   TH1F   *histBdtF(0), *histRf(0), *histSVMG(0), *histSVMP(0), *histSVML(0), *histFDAMT(0), *histFDAGA(0);
   TH1F   *histCat(0), *histPBdt(0);

   if (Use["Likelihood"])    histLk      = new TH1F( "MVA_Likelihood",    "MVA_Likelihood",    nbin, -1, 1 );
   if (Use["LikelihoodD"])   histLkD     = new TH1F( "MVA_LikelihoodD",   "MVA_LikelihoodD",   nbin, -1, 0.9999 );
   if (Use["LikelihoodPCA"]) histLkPCA   = new TH1F( "MVA_LikelihoodPCA", "MVA_LikelihoodPCA", nbin, -1, 1 );
   if (Use["LikelihoodKDE"]) histLkKDE   = new TH1F( "MVA_LikelihoodKDE", "MVA_LikelihoodKDE", nbin,  -0.00001, 0.99999 );
   if (Use["LikelihoodMIX"]) histLkMIX   = new TH1F( "MVA_LikelihoodMIX", "MVA_LikelihoodMIX", nbin,  0, 1 );
   if (Use["PDERS"])         histPD      = new TH1F( "MVA_PDERS",         "MVA_PDERS",         nbin,  0, 1 );
   if (Use["PDERSD"])        histPDD     = new TH1F( "MVA_PDERSD",        "MVA_PDERSD",        nbin,  0, 1 );
   if (Use["PDERSPCA"])      histPDPCA   = new TH1F( "MVA_PDERSPCA",      "MVA_PDERSPCA",      nbin,  0, 1 );
   if (Use["KNN"])           histKNN     = new TH1F( "MVA_KNN",           "MVA_KNN",           nbin,  0, 1 );
   if (Use["HMatrix"])       histHm      = new TH1F( "MVA_HMatrix",       "MVA_HMatrix",       nbin, -0.95, 1.55 );
   if (Use["Fisher"])        histFi      = new TH1F( "MVA_Fisher",        "MVA_Fisher",        nbin, -4, 4 );
   if (Use["FisherG"])       histFiG     = new TH1F( "MVA_FisherG",       "MVA_FisherG",       nbin, -1, 1 );
   if (Use["BoostedFisher"]) histFiB     = new TH1F( "MVA_BoostedFisher", "MVA_BoostedFisher", nbin, -2, 2 );
   if (Use["LD"])            histLD      = new TH1F( "MVA_LD",            "MVA_LD",            nbin, -2, 2 );
   if (Use["MLP"])           histNn      = new TH1F( "MVA_MLP",           "MVA_MLP",           nbin, -1.25, 1.5 );
   if (Use["MLPBFGS"])       histNnbfgs  = new TH1F( "MVA_MLPBFGS",       "MVA_MLPBFGS",       nbin, -1.25, 1.5 );
   if (Use["MLPBNN"])        histNnbnn   = new TH1F( "MVA_MLPBNN",        "MVA_MLPBNN",        nbin, -1.25, 1.5 );
   if (Use["CFMlpANN"])      histNnC     = new TH1F( "MVA_CFMlpANN",      "MVA_CFMlpANN",      nbin,  0, 1 );
   if (Use["TMlpANN"])       histNnT     = new TH1F( "MVA_TMlpANN",       "MVA_TMlpANN",       nbin, -1.3, 1.3 );
   if (Use["DNN"])           histNdn     = new TH1F( "MVA_DNN",           "MVA_DNN",           nbin, -0.1, 1.1 );
   if (Use["BDT"])           histBdt     = new TH1F( "MVA_BDT",           "MVA_BDT",           nbin, -0.8, 0.8 );
   if (Use["BDTG"])          histBdtG    = new TH1F( "MVA_BDTG",          "MVA_BDTG",          nbin, -1.0, 1.0 );
   if (Use["BDTB"])          histBdtB    = new TH1F( "MVA_BDTB",          "MVA_BDTB",          nbin, -1.0, 1.0 );
   if (Use["BDTD"])          histBdtD    = new TH1F( "MVA_BDTD",          "MVA_BDTD",          nbin, -0.8, 0.8 );
   if (Use["BDTF"])          histBdtF    = new TH1F( "MVA_BDTF",          "MVA_BDTF",          nbin, -1.0, 1.0 );
   if (Use["RuleFit"])       histRf      = new TH1F( "MVA_RuleFit",       "MVA_RuleFit",       nbin, -2.0, 2.0 );
   if (Use["SVM_Gauss"])     histSVMG    = new TH1F( "MVA_SVM_Gauss",     "MVA_SVM_Gauss",     nbin,  0.0, 1.0 );
   if (Use["SVM_Poly"])      histSVMP    = new TH1F( "MVA_SVM_Poly",      "MVA_SVM_Poly",      nbin,  0.0, 1.0 );
   if (Use["SVM_Lin"])       histSVML    = new TH1F( "MVA_SVM_Lin",       "MVA_SVM_Lin",       nbin,  0.0, 1.0 );
   if (Use["FDA_MT"])        histFDAMT   = new TH1F( "MVA_FDA_MT",        "MVA_FDA_MT",        nbin, -2.0, 3.0 );
   if (Use["FDA_GA"])        histFDAGA   = new TH1F( "MVA_FDA_GA",        "MVA_FDA_GA",        nbin, -2.0, 3.0 );
   if (Use["Category"])      histCat     = new TH1F( "MVA_Category",      "MVA_Category",      nbin, -2., 2. );
   if (Use["Plugin"])        histPBdt    = new TH1F( "MVA_PBDT",          "MVA_BDT",           nbin, -0.8, 0.8 );

   // PDEFoam also returns per-event error, fill in histogram, and also fill significance
   if (Use["PDEFoam"]) {
      histPDEFoam    = new TH1F( "MVA_PDEFoam",       "MVA_PDEFoam",              nbin,  0, 1 );
      histPDEFoamErr = new TH1F( "MVA_PDEFoamErr",    "MVA_PDEFoam error",        nbin,  0, 1 );
      histPDEFoamSig = new TH1F( "MVA_PDEFoamSig",    "MVA_PDEFoam significance", nbin,  0, 10 );
   }

   // Book example histogram for probability (the other methods are done similarly)
   TH1F *probHistFi(0), *rarityHistFi(0);
   if (Use["Fisher"]) {
      probHistFi   = new TH1F( "MVA_Fisher_Proba",  "MVA_Fisher_Proba",  nbin, 0, 1 );
      rarityHistFi = new TH1F( "MVA_Fisher_Rarity", "MVA_Fisher_Rarity", nbin, 0, 1 );
   }

   
   
   // Event loop

   // Prepare the event tree
   // - Here the variable names have to corresponds to your tree
   // - You can use the same variables as above which is slightly faster,
   //   but of course you can use different ones and copy the values inside the event loop
   //
   std::cout << "--- Select signal sample" << std::endl;


   // Create lightweight results file with just MVA responses for this job
   // Extract job ID from current directory name if job_dir is "."
   std::string job_id = "unknown";
   std::string check_dir = job_dir;
   if (check_dir == ".") {
       // Get current directory name
       check_dir = gSystem->WorkingDirectory();
   }
   size_t job_pos = check_dir.find("job_");
   if (job_pos != std::string::npos) {
       job_id = check_dir.substr(job_pos + 4);  // Skip "job_" prefix
       // Remove any trailing path separators
       size_t sep_pos = job_id.find_first_of("/\\");
       if (sep_pos != std::string::npos) {
           job_id = job_id.substr(0, sep_pos);
       }
   }
   
   // Determine dataset identifier from input file
   std::string dataset_id = "unknown";
   std::string input_path = input_file_arg;
   if (input_path.find("pi0pippim") != std::string::npos) dataset_id = "pi0pippim";
   else if (input_path.find("gepem") != std::string::npos) dataset_id = "gepem";
   
   // Create results file name: job_XXXX_mva_results.root (single file for all datasets/sides)
   std::string outname = job_dir;
   if (!outname.empty() && outname.back() != '/' && outname.back() != '\\') outname += "/";
   outname += "job_" + job_id + "_mva_results.root";
   
   // Check if file exists - if so, UPDATE; otherwise RECREATE
   bool file_exists = (gSystem->AccessPathName(outname.c_str()) == 0);
   const char* file_mode = file_exists ? "UPDATE" : "RECREATE";
   
   std::cout << "--- Opening MVA results file: " << outname << " (mode: " << file_mode << ")" << std::endl;
   
   // Create/update output file with MVA responses
   TFile *outTarget = new TFile(outname.c_str(), file_mode);
   
   // Create tree name that includes side and dataset: MVAResults_MINUS_pi0pippim
   std::string tree_name = "MVAResults_" + app_side + "_" + dataset_id;
   std::string tree_title = "MVA Results for " + app_side + " " + dataset_id + " (Job " + job_id + ")";
   
   // Create a tree with event index and MVA responses
   TTree *outTree = new TTree(tree_name.c_str(), tree_title.c_str());
   
   // Add metadata about this job to the output file (unique name per side/dataset to avoid cycles)
   std::string metadata_name = "job_info_" + app_side + "_" + dataset_id;
   TNamed *job_metadata = new TNamed(metadata_name.c_str(), 
                                     (std::string("Job ID: ") + job_id + 
                                      ", Dataset: " + datasetName + 
                                      ", Vars file: " + varfile + 
                                      ", Input: " + input_file).c_str());
   job_metadata->Write();

   // Create branches for lightweight results file
   Int_t event_index;     // To match back to original events
   Float_t MLP_response;
   Float_t BDT_response;
   
   // Determine side suffix for branch naming
   std::string side_suffix;
   if (datasetName.find("PLUS") != std::string::npos) side_suffix = "PLUS";
   else if (datasetName.find("MINUS") != std::string::npos) side_suffix = "MINUS";
   
   // Simple branch names for this job's results
   std::string mname = "MLP_" + side_suffix;
   std::string bname = "BDT_" + side_suffix;
   
   std::cout << "--- Adding MVA response branches: " << mname << ", " << bname << std::endl;
   
   outTree->Branch("event_index", &event_index);
   outTree->Branch(mname.c_str(), &MLP_response);
   outTree->Branch(bname.c_str(), &BDT_response);
  


   // Efficiency calculator for cut method
   Int_t    nSelCutsGA = 0;
   Double_t effS       = 0.7;

   std::vector<Float_t> vecVar(4); // vector for EvaluateMVA tests

   std::cout << "--- Processing: " << theTree->GetEntries() << " events to add MVA responses" << std::endl;
   TStopwatch sw;
   sw.Start();
   
   // Now loop through to compute MVA responses for each event
   for (Long64_t ievt=0; ievt<theTree->GetEntries();ievt++) {

      if (ievt%1000 == 0) std::cout << "--- ... Processing event: " << ievt << std::endl;

      // Get entry from original tree (this loads all branch values)
      theTree->GetEntry(ievt);
      
      // Set event index for matching
      event_index = (Int_t)ievt;

      // Return the MVA outputs and fill into histograms
      if (Use["CutsGA"]) {
         // Cuts is a special case: give the desired signal efficienciy
         Bool_t passed = reader->EvaluateMVA( "CutsGA method", effS );
         if (passed) nSelCutsGA++;
      }

      if (Use["Likelihood"   ])   histLk     ->Fill( reader->EvaluateMVA( "Likelihood method"    ) );
      if (Use["LikelihoodD"  ])   histLkD    ->Fill( reader->EvaluateMVA( "LikelihoodD method"   ) );
      if (Use["LikelihoodPCA"])   histLkPCA  ->Fill( reader->EvaluateMVA( "LikelihoodPCA method" ) );
      if (Use["LikelihoodKDE"])   histLkKDE  ->Fill( reader->EvaluateMVA( "LikelihoodKDE method" ) );
      if (Use["LikelihoodMIX"])   histLkMIX  ->Fill( reader->EvaluateMVA( "LikelihoodMIX method" ) );
      if (Use["PDERS"        ])   histPD     ->Fill( reader->EvaluateMVA( "PDERS method"         ) );
      if (Use["PDERSD"       ])   histPDD    ->Fill( reader->EvaluateMVA( "PDERSD method"        ) );
      if (Use["PDERSPCA"     ])   histPDPCA  ->Fill( reader->EvaluateMVA( "PDERSPCA method"      ) );
      if (Use["KNN"          ])   histKNN    ->Fill( reader->EvaluateMVA( "KNN method"           ) );
      if (Use["HMatrix"      ])   histHm     ->Fill( reader->EvaluateMVA( "HMatrix method"       ) );
      if (Use["Fisher"       ])   histFi     ->Fill( reader->EvaluateMVA( "Fisher method"        ) );
      if (Use["FisherG"      ])   histFiG    ->Fill( reader->EvaluateMVA( "FisherG method"       ) );
      if (Use["BoostedFisher"])   histFiB    ->Fill( reader->EvaluateMVA( "BoostedFisher method" ) );
      if (Use["LD"           ])   histLD     ->Fill( reader->EvaluateMVA( "LD method"            ) );
      if (Use["MLP"          ])   histNn     ->Fill( reader->EvaluateMVA( "MLP method"           ) );
      if (Use["MLPBFGS"      ])   histNnbfgs ->Fill( reader->EvaluateMVA( "MLPBFGS method"       ) );
      if (Use["MLPBNN"       ])   histNnbnn  ->Fill( reader->EvaluateMVA( "MLPBNN method"        ) );
      if (Use["CFMlpANN"     ])   histNnC    ->Fill( reader->EvaluateMVA( "CFMlpANN method"      ) );
      if (Use["TMlpANN"      ])   histNnT    ->Fill( reader->EvaluateMVA( "TMlpANN method"       ) );
      if (Use["DNN"          ])   histNdn    ->Fill( reader->EvaluateMVA( "DNN method"           ) );
      if (Use["BDT"          ])   histBdt    ->Fill( reader->EvaluateMVA( "BDT method"           ) );
      if (Use["BDTG"         ])   histBdtG   ->Fill( reader->EvaluateMVA( "BDTG method"          ) );
      if (Use["BDTB"         ])   histBdtB   ->Fill( reader->EvaluateMVA( "BDTB method"          ) );
      if (Use["BDTD"         ])   histBdtD   ->Fill( reader->EvaluateMVA( "BDTD method"          ) );
      if (Use["BDTF"         ])   histBdtF   ->Fill( reader->EvaluateMVA( "BDTF method"          ) );
      if (Use["RuleFit"      ])   histRf     ->Fill( reader->EvaluateMVA( "RuleFit method"       ) );
      if (Use["SVM_Gauss"    ])   histSVMG   ->Fill( reader->EvaluateMVA( "SVM_Gauss method"     ) );
      if (Use["SVM_Poly"     ])   histSVMP   ->Fill( reader->EvaluateMVA( "SVM_Poly method"      ) );
      if (Use["SVM_Lin"      ])   histSVML   ->Fill( reader->EvaluateMVA( "SVM_Lin method"       ) );
      if (Use["FDA_MT"       ])   histFDAMT  ->Fill( reader->EvaluateMVA( "FDA_MT method"        ) );
      if (Use["FDA_GA"       ])   histFDAGA  ->Fill( reader->EvaluateMVA( "FDA_GA method"        ) );
      if (Use["Category"     ])   histCat    ->Fill( reader->EvaluateMVA( "Category method"      ) );
      if (Use["Plugin"       ])   histPBdt   ->Fill( reader->EvaluateMVA( "P_BDT method"         ) );

      // Compute and store MVA responses for this event
      MLP_response = reader->EvaluateMVA( "MLP method" );
      BDT_response = reader->EvaluateMVA( "BDT method" );
      
      // Fill the lightweight results tree (just event index + MVA responses)
      outTree->Fill();

      // Retrieve also per-event error
      if (Use["PDEFoam"]) {
         Double_t val = reader->EvaluateMVA( "PDEFoam method" );
         Double_t err = reader->GetMVAError();
         histPDEFoam   ->Fill( val );
         histPDEFoamErr->Fill( err );
         if (err>1.e-50) histPDEFoamSig->Fill( val/err );
      }

      // Retrieve probability instead of MVA output
      if (Use["Fisher"])   {
         probHistFi  ->Fill( reader->GetProba ( "Fisher method" ) );
         rarityHistFi->Fill( reader->GetRarity( "Fisher method" ) );
      }
   }

   // bbrau Write outTree and close the file it is in
   outTree->Write();
   outTarget->Close();
   
   
   // Get elapsed time
   sw.Stop();
   std::cout << "--- End of event loop: "; sw.Print();

   // Get efficiency for cuts classifier
   if (Use["CutsGA"]) std::cout << "--- Efficiency for CutsGA method: " << double(nSelCutsGA)/theTree->GetEntries()
                                << " (for a required signal efficiency of " << effS << ")" << std::endl;

   if (Use["CutsGA"]) {

      // test: retrieve cuts for particular signal efficiency
      // CINT ignores dynamic_casts so we have to use a cuts-secific Reader function to acces the pointer
      // TMVA::MethodCuts* mcuts = reader->FindCutsMVA( "CutsGA method" ) ;  // FindCutsMVA doesn't exist in modern TMVA

      TMVA::MethodCuts* mcuts = nullptr;  // Disable cuts method check
      if (mcuts) {
         std::vector<Double_t> cutsMin;
         std::vector<Double_t> cutsMax;
         mcuts->GetCuts( 0.7, cutsMin, cutsMax );
         std::cout << "--- -------------------------------------------------------------" << std::endl;
         std::cout << "--- Retrieve cut values for signal efficiency of 0.7 from Reader" << std::endl;
         for (UInt_t ivar=0; ivar<cutsMin.size(); ivar++) {
            std::cout << "... Cut: "
                      << cutsMin[ivar]
                      << " < \""
                      << mcuts->GetInputVar(ivar)
                      << "\" <= "
                      << cutsMax[ivar] << std::endl;
         }
         std::cout << "--- -------------------------------------------------------------" << std::endl;
      }
   }

   // Write histograms

   TFile *target  = new TFile( "TMVApp.root","RECREATE" );
   if (Use["Likelihood"   ])   histLk     ->Write();
   if (Use["LikelihoodD"  ])   histLkD    ->Write();
   if (Use["LikelihoodPCA"])   histLkPCA  ->Write();
   if (Use["LikelihoodKDE"])   histLkKDE  ->Write();
   if (Use["LikelihoodMIX"])   histLkMIX  ->Write();
   if (Use["PDERS"        ])   histPD     ->Write();
   if (Use["PDERSD"       ])   histPDD    ->Write();
   if (Use["PDERSPCA"     ])   histPDPCA  ->Write();
   if (Use["KNN"          ])   histKNN    ->Write();
   if (Use["HMatrix"      ])   histHm     ->Write();
   if (Use["Fisher"       ])   histFi     ->Write();
   if (Use["FisherG"      ])   histFiG    ->Write();
   if (Use["BoostedFisher"])   histFiB    ->Write();
   if (Use["LD"           ])   histLD     ->Write();
   if (Use["MLP"          ])   histNn     ->Write();
   if (Use["MLPBFGS"      ])   histNnbfgs ->Write();
   if (Use["MLPBNN"       ])   histNnbnn  ->Write();
   if (Use["CFMlpANN"     ])   histNnC    ->Write();
   if (Use["TMlpANN"      ])   histNnT    ->Write();
   if (Use["DNN"          ])   histNdn    ->Write();
   if (Use["BDT"          ])   histBdt    ->Write();
   if (Use["BDTG"         ])   histBdtG   ->Write();
   if (Use["BDTB"         ])   histBdtB   ->Write();
   if (Use["BDTD"         ])   histBdtD   ->Write();
   if (Use["BDTF"         ])   histBdtF   ->Write();
   if (Use["RuleFit"      ])   histRf     ->Write();
   if (Use["SVM_Gauss"    ])   histSVMG   ->Write();
   if (Use["SVM_Poly"     ])   histSVMP   ->Write();
   if (Use["SVM_Lin"      ])   histSVML   ->Write();
   if (Use["FDA_MT"       ])   histFDAMT  ->Write();
   if (Use["FDA_GA"       ])   histFDAGA  ->Write();
   if (Use["Category"     ])   histCat    ->Write();
   if (Use["Plugin"       ])   histPBdt   ->Write();

   // Write also error and significance histos
   if (Use["PDEFoam"]) { histPDEFoam->Write(); histPDEFoamErr->Write(); histPDEFoamSig->Write(); }

   // Write also probability hists
   if (Use["Fisher"]) { if (probHistFi != 0) probHistFi->Write(); if (rarityHistFi != 0) rarityHistFi->Write(); }

   target->Close();
   outTarget->Close();

   std::cout << "--- Created root file: \"TMVApp.root\" containing the MVA output histograms" << std::endl;

   delete reader;

   std::cout << "==> TMVAClassificationApplication is done!" << std::endl << std::endl;
}

int main( int argc, char** argv )
{
   TString methodList;
   for (int i=1; i<argc; i++) {
      TString regMethod(argv[i]);
      if(regMethod=="-b" || regMethod=="--batch") continue;
      if (!methodList.IsNull()) methodList += TString(",");
      methodList += regMethod;
   }
   TMVAClassificationApplication(methodList);
   return 0;
}

