// \file
/// \ingroup tutorial_tmva
/// \notebook -nodraw
/// This macro provides examples for the training and testing of the
/// TMVA classifiers.
///
/// As input data is used a toy-MC sample consisting of four Gaussian-distributed
/// and linearly correlated input variables.
/// The methods to be used can be switched on and off by means of booleans, or
/// via the prompt command, for example:
///
///     root -l ./TMVAClassification.C\(\"Fisher,Likelihood\"\)
///
/// (note that the backslashes are mandatory)
/// If no method given, a default set of classifiers is used.
/// The output file "TMVA.root" can be analysed with the use of dedicated
/// macros (simply say: root -l <macro.C>), which can be conveniently
/// invoked through a GUI that will appear at the end of the run of this macro.
/// Launch the GUI via the command:
///
///     root -l ./TMVAGui.C
///
/// You can also compile and run the example with the following commands
///
///     make
///     ./TMVAClassification <Methods>
///
/// where: `<Methods> = "method1 method2"` are the TMVA classifier names
/// example:
///
///     ./TMVAClassification Fisher LikelihoodPCA BDT
///
/// If no method given, a default set is of classifiers is used
///
/// - Project   : TMVA - a ROOT-integrated toolkit for multivariate data analysis
/// - Package   : TMVA
/// - Root Macro: TMVAClassification
///
/// \macro_output
/// \macro_code
/// \author Andreas Hoecker


#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <fstream>

#include "TChain.h"
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TObjString.h"
#include "TSystem.h"
#include "TROOT.h"

#include "TMVA/Factory.h"
#include "TMVA/DataLoader.h"
#include "TMVA/Tools.h"
#include "TMVA/TMVAGui.h"





// -------------------------------------------------------------
// Variable list definition (global to this file)
// -------------------------------------------------------------
enum class Role { Var, Spec };


struct VarSpec {
  std::string expr;   // branch OR "Alias := expression"
  std::string title;  // pretty label (usually the alias)
  char type;          // 'F'
  Role role;          // Var / Spec
};


static inline std::string trim(std::string s) {
  auto l = s.find_first_not_of(" \t\r");
  if (l == std::string::npos) return "";
  auto r = s.find_last_not_of(" \t\r");
  return s.substr(l, r - l + 1);
}

// Infer a nice title: if "Alias := expr" use "Alias", else use the expr itself
static inline std::string infer_title(const std::string& expr) {
  auto pos = expr.find(":=");
  if (pos != std::string::npos) return trim(expr.substr(0, pos));
  return expr;
}

// Simple JSON string escaper for small config dumps
static inline std::string json_escape(const std::string &s) {
   std::string out;
   out.reserve(s.size());
   for (char c : s) {
      switch (c) {
         case '\\': out += "\\\\"; break;
         case '"':  out += "\\\""; break;
         case '\b': out += "\\b"; break;
         case '\f': out += "\\f"; break;
         case '\n': out += "\\n"; break;
         case '\r': out += "\\r"; break;
         case '\t': out += "\\t"; break;
         default:
            if ((unsigned char)c < 0x20) {
               // control char -> unicode escape
               char buf[8];
               sprintf(buf, "\\u%04x", (unsigned char)c);
               out += buf;
            } else out += c;
      }
   }
   return out;
}


// Load one INI with sections [training] and [spectators].
// Each non-empty, non-comment line inside a section is one variable/expression.
// Supports both plain names and "Alias := expression". Type defaults to 'F'.
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

// Parse additional sections in the vars INI that are not [training] or [spectators].
// Each section's non-empty, non-comment lines are joined by commas and stored in
// the map under a key derived from the section name (underscores -> hyphens).
static std::map<std::string,std::string> parse_hparams_from_ini(const std::string& path) {
   std::map<std::string,std::string> out;
   std::ifstream in(path);
   if (!in) return out;

   std::string line;
   std::string curSection;
   std::vector<std::string> values;
   auto flush = [&]() {
      if (curSection.empty()) return;
      std::string sec = curSection;
      // normalize section name to key form: replace '_' with '-'
      for (auto &c : sec) if (c=='_') c='-';
      // join values by comma
      std::string joined;
      for (size_t i=0;i<values.size();++i) {
         if (i) joined += ",";
         joined += values[i];
      }
      if (!joined.empty()) out[sec] = joined;
      values.clear();
   };

   while (std::getline(in, line)) {
      std::string s = trim(line);
      if (s.empty()) continue;
      if (s[0] == '#') continue;
      if (s.size() >= 2 && s[0] == '/' && s[1] == '/') continue;
      if (s.size() >= 2 && s.front()=='[' && s.back()==']') {
         // new section
         // flush previous
         flush();
         curSection = s.substr(1, s.size()-2);
         // skip the known sections that hold variables
         if (curSection == "training" || curSection == "spectators") {
            // mark curSection empty so flush does nothing until next non-special section
            curSection.clear();
         }
         continue;
      }
      if (curSection.empty()) continue; // ignore lines in training/spectator or before section
      // optional trailing type after |; remove it
      size_t bar = s.find('|');
      if (bar != std::string::npos) s = trim(s.substr(0, bar));
      values.push_back(s);
   }
   flush();
   return out;
}




int TMVAClassification( TString myMethodList = "", 
                        const std::map<std::string,std::string>& hparams = std::map<std::string,std::string>(), 
                        const std::string& side = "",
                        const std::string& signal_file = "",
                        const std::string& signal_tree = "",
                        const std::string& background_file = "",
                        const std::string& background_tree = "" )
{
   // The explicit loading of the shared libTMVA is done in TMVAlogon.C, defined in .rootrc
   // if you use your private .rootrc, or run from a different directory, please copy the
   // corresponding lines from .rootrc

   // Methods to be processed can be given as an argument; use format:
   //
   //     mylinux~> root -l TMVAClassification.C\(\"myMethod1,myMethod2,myMethod3\"\)

   //---------------------------------------------------------------
   // This loads the library
   TMVA::Tools::Instance();

   // Default MVA methods to be trained + tested
   std::map<std::string,int> Use;

   //below is a bunch of classifications that ultimately get used to differentiate between a signal event and background event. 
   //THESE ARE ALL DIFFERENT TOOLS that will achieve the same thing--separating signal and background provided you have some training data that you can train them on.


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
   Use["LD"]              = 0; // = 1; // Linear Discriminant identical to Fisher.   performs some linear combinations with weighting--can understand from the weights it assigns, which variables it thinks are the most important versus not so useful
   Use["Fisher"]          = 0;
   Use["FisherG"]         = 0;
   Use["BoostedFisher"]   = 0; // uses generalised MVA method boosting
   Use["HMatrix"]         = 0;
   //
   // Function Discriminant Analysis                 
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
   Use["DNN"]             = 0; // Deep Neural Network
   Use["DNN_GPU"]         = 0; // CUDA-accelerated DNN training.
   Use["DNN_CPU"]         = 0; // Multi-core accelerated DNN.
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

   std::cout << std::endl;
   std::cout << "==> Start TMVAClassification" << std::endl;

   // Select methods (don't look at this code - not of interest)
   if (myMethodList != "") {
      for (std::map<std::string,int>::iterator it = Use.begin(); it != Use.end(); it++) it->second = 0;

      std::vector<TString> mlist = TMVA::gTools().SplitString( myMethodList, ',' );
      for (UInt_t i=0; i<mlist.size(); i++) {
         std::string regMethod(mlist[i]);

         if (Use.find(regMethod) == Use.end()) {
            std::cout << "Method \"" << regMethod << "\" not known in TMVA under this name. Choose among the following:" << std::endl;
            for (std::map<std::string,int>::iterator it = Use.begin(); it != Use.end(); it++) std::cout << it->first << " ";
            std::cout << std::endl;
            return 1;
         }
         Use[regMethod] = 1;
      }
   }

   // --------------------------------------------------------------------------------------------------
   // Read training and test data
   
   // Determine signal and background files. Use function arguments if provided,
   // otherwise fall back to environment variables, then to default values.
   std::string sig_file = signal_file;
   std::string bg_file = background_file;
   std::string sig_tree_name = signal_tree;
   std::string bg_tree_name = background_tree;
   
   // Fall back to environment variables if arguments not provided
   if (sig_file.empty()) {
      if (const char* e = std::getenv("TMVA_SIGNAL_INPUT")) sig_file = std::string(e);
   }
   if (bg_file.empty()) {
      if (const char* e = std::getenv("TMVA_BACKGROUND_INPUT")) bg_file = std::string(e);
   }
   if (sig_tree_name.empty()) {
      if (const char* e = std::getenv("TMVA_SIGNAL_TREE")) sig_tree_name = std::string(e);
   }
   if (bg_tree_name.empty()) {
      if (const char* e = std::getenv("TMVA_BACKGROUND_TREE")) bg_tree_name = std::string(e);
   }
   
   // Default fallback values if still not set
   if (sig_file.empty()) sig_file = "/work/halld/home/acschick/AIworkshop2025_TMVA/trainingSamples/Flat_eeRBHG_train_RADON_Anaprop_corAVGALL.root";
   if (bg_file.empty()) bg_file = "/work/halld/home/acschick/AIworkshop2025_TMVA/trainingSamples/Flat_rho0p_2018-01_BASE_TrMCSIZE.root";
   if (sig_tree_name.empty()) sig_tree_name = "flat_TMVA";
   if (bg_tree_name.empty()) bg_tree_name = "flat_TMVA";
   
   TString fname(sig_file.c_str());
   TString background_fname(bg_file.c_str());
   
   TFile *input = TFile::Open( fname );
   TFile *backgroundInput = TFile::Open( background_fname );

   std::cout << "--- TMVAClassification       : Using signal file: " << input->GetName() << std::endl;
   std::cout << "--- TMVAClassification       : Using background file: " << backgroundInput->GetName() << std::endl;

    // Helper to fetch a TTree from a file: try environment variable name first, then common names, then fall back to the first TTree
    auto fetch_tree = [&](TFile* f, const std::string& preferred_name)->TTree* {
       if (!f) return nullptr;
       // First try the preferred name (from environment variable)
       if (!preferred_name.empty()) {
          TTree* t = (TTree*)f->Get(preferred_name.c_str());
          if (t) return t;
       }
       // Fallback to common names
       TTree* t = (TTree*)f->Get("flat_TMVA");
       if (t) return t;
       t = (TTree*)f->Get("Flat2018_MC_Electrons");
       if (t) return t;
       // Last resort: find first object that inherits from TTree
       TKey* key = nullptr;
       TIter next(f->GetListOfKeys());
       while ((key = (TKey*)next())) {
          TObject* obj = key->ReadObj();
          if (obj && obj->InheritsFrom("TTree")) {
             return (TTree*)obj;
          }
       }
       return nullptr;
    };

    // Register the training and test trees
    TTree *signalTree = fetch_tree(input, sig_tree_name);
    TTree *background = fetch_tree(backgroundInput, bg_tree_name);
  
   

    // Allow overriding filenames and algorithm hyperparameters via hparams map.
    // Also support passing hyperparameters via environment variable TMVA_HPARAMS
    // Format (semicolon separated): key1=val1;key2=val2
    // Example: export TMVA_HPARAMS="mlp-hidden=N+5;bdt-ntrees=500"
    std::map<std::string,std::string> merged_hparams = hparams; // start with passed-in map
    if (const char* env = std::getenv("TMVA_HPARAMS")) {
       std::string s(env);
       size_t start = 0;
       while (start < s.size()) {
          size_t semi = s.find(';', start);
          std::string pair = (semi==std::string::npos) ? s.substr(start) : s.substr(start, semi-start);
          size_t eq = pair.find('=');
          if (eq != std::string::npos) {
             std::string k = trim(pair.substr(0, eq));
             std::string v = trim(pair.substr(eq+1));
             if (!k.empty()) merged_hparams[k] = v;
          }
          if (semi==std::string::npos) break;
          start = semi + 1;
       }
    }

    auto getp = [&](const std::string& k, const std::string& def)->std::string {
       auto it = merged_hparams.find(k);
       return (it==merged_hparams.end()) ? def : it->second;
    };

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
    std::cout << "Using vars file: " << varfile << " (full path: " << gSystem->WorkingDirectory() << "/" << varfile << ")" << std::endl;

    // Parse variables and also any hyperparameter-like sections embedded in the vars file.
   auto VARS = load_vars_ini(varfile, side);
   
   // Count variables vs spectators for debugging
   int var_count = 0, spec_count = 0;
   for (const auto& v : VARS) {
      if (v.role == Role::Var) var_count++;
      else spec_count++;
   }
   std::cout << "Training: Loaded " << var_count << " variables and " << spec_count << " spectators (side='" << side << "')" << std::endl;
   
   // Print first few variables for debugging
   std::cout << "Training: Variables:" << std::endl;
   int printed_vars = 0;
   for (const auto& v : VARS) {
      if (v.role == Role::Var) {
         std::cout << "  - " << v.expr << std::endl;
         if (++printed_vars >= 10) { std::cout << "  ... (" << (var_count - 10) << " more)" << std::endl; break; }
      }
   }
   
   // Print ALL spectators in comma-separated list
   std::cout << "Training: All Spectators: ";
   bool first = true;
   for (const auto& v : VARS) {
      if (v.role == Role::Spec) {
         if (!first) std::cout << ", ";
         std::cout << v.expr;
         first = false;
      }
   }
   std::cout << std::endl;
   
   auto file_hparams = parse_hparams_from_ini(varfile);
    // Merge file-defined hyperparameters into merged_hparams, letting file overwrite previous values
    for (const auto &kv : file_hparams) merged_hparams[kv.first] = kv.second;
    // file_hparams entries may contain key=value pairs (for example a [meta]
    // section with lines like "out=..." or "dataset=datasetPLUS"). If so,
    // extract those pairs and store them directly into merged_hparams as
    // individual keys (so getp("out") works).
    for (const auto &kv : file_hparams) {
       const std::string &v = kv.second; // comma-joined values from section
       size_t start = 0;
       while (start < v.size()) {
          size_t comma = v.find(',', start);
          std::string piece = (comma==std::string::npos) ? v.substr(start) : v.substr(start, comma-start);
          size_t eq = piece.find('=');
          if (eq != std::string::npos) {
             std::string k = trim(piece.substr(0, eq));
             std::string val = trim(piece.substr(eq+1));
             if (!k.empty()) merged_hparams[k] = val;
          }
          if (comma==std::string::npos) break;
          start = comma + 1;
       }
    }

    // Create a ROOT output file where TMVA will store ntuples, histograms, etc.
    std::string outfileNameStr = getp("out", "ep_pip_TMVA.root");
    
    // Append side (PLUS/MINUS) to filename to avoid overwriting when training both models
    if (!side.empty()) {
        // Insert side before .root extension: "file.root" -> "file_PLUS.root"
        size_t dotpos = outfileNameStr.rfind(".root");
        if (dotpos != std::string::npos) {
            outfileNameStr = outfileNameStr.substr(0, dotpos) + "_" + side + ".root";
        } else {
            outfileNameStr += "_" + side;
        }
    }
    
    TString outfileName( outfileNameStr.c_str() );
    TFile* outputFile = TFile::Open( outfileName, "RECREATE" );

   // Create the factory object. Later you can choose the methods
   // whose performance you'd like to investigate. The factory is
   // the only TMVA object you have to interact with
   //
   // The first argument is the base of the name of all the
   // weightfiles in the directory weight/
   //
   // The second argument is the output file for the training results
   // All TMVA output can be suppressed by removing the "!" (not) in
   // front of the "Silent" argument in the option string
   TMVA::Factory *factory = new TMVA::Factory( "TMVAClassification", outputFile,
                                               "!V:!Silent:Color:DrawProgressBar:Transformations=I;D;G,D:AnalysisType=Classification" );
   // If you wish to modify default settings
   // (please check "src/Config.h" to see all available global options)
   //
   //    (TMVA::gConfig().GetVariablePlotting()).fTimesRMS = 8.0;
   //    (TMVA::gConfig().GetIONames()).fWeightFileDir = "myWeightDirectory";

   // Define the input variables that shall be used for the MVA training
   // note that you may also use variable expressions, such as: "3*var1/var2*abs(var3)"
   // [all types of expressions that can also be parsed by TTree::Draw( "expression" )]


   

  

      // Now that we know varfile and merged_hparams, write a small run_config JSON
      {
         std::string outfn = outfileNameStr + ".run_config.json";
         std::ofstream jf(outfn.c_str());
         if (!jf) { std::cerr << "Could not write run config to " << outfn << "\n"; }
         else {
            jf << "{\n";
            jf << "  \"methods\": \"" << json_escape(std::string(myMethodList.Data())) << "\"," << "\n";
            jf << "  \"vars_file\": \"" << json_escape(varfile) << "\"," << "\n";
            jf << "  \"out\": \"" << json_escape(outfileNameStr) << "\"," << "\n";
            jf << "  \"hparams\": {\n";
            size_t cnt = 0;
            for (const auto &kv : merged_hparams) {
               jf << "    \"" << json_escape(kv.first) << "\": \"" << json_escape(kv.second) << "\"";
               if (++cnt < merged_hparams.size()) jf << ",";
               jf << "\n";
            }
            jf << "  }\n";
            jf << "}\n";
            jf.close();
            std::cout << "Wrote run config: " << outfn << std::endl;
         }
      }
         // Determine a sensible job directory and dataset name so that weight files
         // and other outputs are placed inside the per-job folder. Preference order:
         //  1) directory containing vars file (if vars file contains a path)
         //  2) directory part of the output filename (out)
         //  3) current directory (default, since job manager cd's to job dir first)
         std::string job_dir = ".";
         size_t p = varfile.find_last_of("/\\");
         if (p != std::string::npos) {
            job_dir = varfile.substr(0,p);
         } else {
            size_t q = outfileNameStr.find_last_of("/\\");
            if (q != std::string::npos) job_dir = outfileNameStr.substr(0,q);
         }

         // Determine dataset subfolder name. Priority:
         // 1) explicit 'dataset' key in merged_hparams
         // 2) infer from varfile name containing 'plus' or 'minus'
         // 3) default to "dataset"
         std::string datasetName = getp("dataset", "");
         // If side is provided, prefer it for dataset name
         if (datasetName.empty() && !side.empty()) {
            if (side == "PLUS") datasetName = "datasetPLUS";
            else if (side == "MINUS") datasetName = "datasetMINUS";
         }
         if (datasetName.empty()) {
            std::string lf = varfile;
            for (auto &c : lf) c = std::tolower((unsigned char)c);
            if (lf.find("plus") != std::string::npos) datasetName = "datasetPLUS";
            else if (lf.find("minus") != std::string::npos) datasetName = "datasetMINUS";
            else datasetName = "dataset";
         }

         // DataLoader will create datasetPLUS/datasetMINUS folder with weights/ subfolder
         // Don't set fWeightFileDir - let TMVA use its default behavior to create weights/ subdirectory
         std::cout << "Using job_dir='" << job_dir << "' datasetName='" << datasetName << "'\n";
         TMVA::DataLoader *dataloader = new TMVA::DataLoader( datasetName.c_str() );
   // Register variables & spectators with TMVA
   for (const auto& v : VARS) {
     if (v.role == Role::Var)
       dataloader->AddVariable(v.expr.c_str(), v.title.c_str(), v.type);
     else
       dataloader->AddSpectator(v.expr.c_str(), v.title.c_str());
   }

   std::vector<std::string> cuts;
   auto addCut  = [&](const std::string& c){ if(!c.empty()) cuts.push_back(c); };

   auto hasAlias = [](const std::string& e){ return e.find(":=") != std::string::npos; };
   auto getAlias = [](const std::string& e){
     auto p = e.find(":="); 
     return p==std::string::npos ? std::string{} : trim(e.substr(0,p));
   };

   // NEW guard
   auto addGuard = [&](const VarSpec& v){
     const std::string token = hasAlias(v.expr) ? getAlias(v.expr) : v.expr;
     cuts.push_back(Form("(%1$s==%1$s) && (%1$s>-1e30) && (%1$s<1e30)", token.c_str()));
   };

   // Guards for training vars
   for (const auto& v : VARS)
     if (v.role == Role::Var) addGuard(v);

   // Explicit offenders (training vars)
   //   addCut("(Negative_Beta_Timing==Negative_Beta_Timing) && (Negative_Beta_Timing>-1e30) && (Negative_Beta_Timing<1e30)");
   //   addCut("(Positive_Beta_Timing==Positive_Beta_Timing) && (Positive_Beta_Timing>-1e30) && (Positive_Beta_Timing<1e30)");
   //   addCut("(Negative_X4_KinFit_T==Negative_X4_KinFit_T) && (Negative_X4_KinFit_T>-1e30) && (Negative_X4_KinFit_T<1e30)");
   //   addCut("(Positive_X4_KinFit_T==Positive_X4_KinFit_T) && (Positive_X4_KinFit_T>-1e30) && (Positive_X4_KinFit_T<1e30)");
   
   /*

   // (2) Beam energy window
   // addCut(“ComboBeam__P4_KinFit_E > 8.2 && ComboBeam__P4_KinFit_E < 8.8”);
   // (3) φ → K⁺K⁻ mass with **kaon** energies from 3-momenta
   // p magnitudes (squared) from the branches:
   const std::string p2plus = 
     "pow(Positive_P4_Measured_X,2)+pow(Positive_P4_Measured_Y,2)+pow(Positive_P4_Measured_Z,2)";
   const std::string p2minus = 
     "pow(Negative_P4_Measured_X,2)+pow(Negative_P4_Measured_Y,2)+pow(Negative_P4_Measured_Z,2)";
   
   // kaon energies: E = sqrt(p^2 + mK^2)
   const std::string EKplus  = Form("sqrt(%s + %.6f*%.6f)", p2plus.c_str(),  M_K, M_K);
   const std::string EKminus = Form("sqrt(%s + %.6f*%.6f)", p2minus.c_str(), M_K, M_K);
   
   // vector sums
   const std::string Px = "Positive_P4_Measured_X + Negative_P4_Measured_X";
   const std::string Py = "Positive_P4_Measured_Y + Negative_P4_Measured_Y";
   const std::string Pz = "Positive_P4_Measured_Z + Negative_P4_Measured_Z";
   const std::string E  = Form("(%s + %s)", EKplus.c_str(), EKminus.c_str());
  
   const double M_K   = 0.493677;   // charged kaon mass
   const double M_PHI = 1.019461;   // PDG phi mass
   const double SIGMA_PHI = 0.100;  // 

   // invariant mass with kaon energies
   const std::string mKK =
     Form("sqrt( pow(%s,2) - pow(%s,2) - pow(%s,2) - pow(%s,2) )",
	  E.c_str(), Px.c_str(), Py.c_str(), Pz.c_str());
   
   // 1σ window around phi peak
   //   addCut(Form("abs((%s) - %.6f) < %.6f", mKK.c_str(), M_PHI, SIGMA_PHI));

   */
   // (4) Join & use
   std::string cutStr;
   for (size_t i=0;i<cuts.size();++i){ if(i) cutStr += " && "; cutStr += cuts[i]; }
   TCut mycut = cutStr.c_str();

   // Get training size from hparams, default to 20000 if not specified
   // Check both "nTrain" and "train-events" for compatibility
   // Note: INI section names are normalized (underscores -> hyphens) by parse_hparams_from_ini()
   int nTrain = 20000;
   auto it_ntrain = merged_hparams.find("nTrain");
   if (it_ntrain != merged_hparams.end()) {
      nTrain = std::stoi(it_ntrain->second);
   } else {
      auto it_train_events = merged_hparams.find("train-events");  // Normalized from "train_events"
      if (it_train_events != merged_hparams.end()) {
         nTrain = std::stoi(it_train_events->second);
      }
   }
   
   // Fix test set size at 600k per class (30% of 2M available events)
   // This ensures all models are evaluated on the same test set for fair comparison
   int nTest = 600000;
   
   TString trainOptions = Form("nTrain_Signal=%d:nTrain_Background=%d:nTest_Signal=%d:nTest_Background=%d:SplitMode=Random:NormMode=NumEvents:!V",
                                nTrain, nTrain, nTest, nTest);
   
   std::cout << "=== Training Configuration ===" << std::endl;
   std::cout << "Training events per class: " << nTrain << std::endl;
   std::cout << "Test events per class: " << nTest << std::endl;
   std::cout << "Total training events: " << (2 * nTrain) << std::endl;
   std::cout << "Total test events: " << (2 * nTest) << std::endl;
   std::cout << "Options: " << trainOptions << std::endl;
   std::cout << "==============================" << std::endl;
   
   dataloader->PrepareTrainingAndTestTree(mycut, trainOptions);
   
   /* Commented out - old NaN guard code
   // Build a TCut that rejects NaN/Inf for all branches you care about
   std::string cutStr_old;
   auto addGuard = [&](const char* vname) {
     if (!cutStr_old.empty()) cutStr_old += " && ";
     cutStr_old += Form("(%s==%s) && abs(%s) < 1e9", vname, vname, vname);
   };

   // Guard *variables* (and optionally spectators if you want TMVA to stop warning)
   for (const auto& v : VARS) {
     // If spectators have NaNs, TMVA will still warn; include them if you want silence:
     // if (v.role == Role::Var || v.role == Role::Spec) addGuard(v.name);
     if (v.role == Role::Var) addGuard(v.name);
   }
   */
   
   // You can add so-called "Spectator variables", which are not used in the MVA training,
   // but will appear in the final "TestTree" produced by TMVA. This TestTree will contain the
   // input variables, the response values of all trained MVAs, and the spectator variables

   //dataloader->AddSpectator( "spec1 := var1*2",  "Spectator 1", "units", 'F' );
   //dataloader->AddSpectator( "spec2 := var1*3",  "Spectator 2", "units", 'F' );


   // You can add an arbitrary number of signal or background trees
   dataloader->AddSignalTree    ( signalTree,     1.0 );
   dataloader->AddBackgroundTree( background, 1.0 );

   // To give different trees for training and testing, do as follows:
   //
   //     dataloader->AddSignalTree( signalTrainingTree, signalTrainWeight, "Training" );
   //     dataloader->AddSignalTree( signalTestTree,     signalTestWeight,  "Test" );

   // Use the following code instead of the above two or four lines to add signal and background
   // training and test events "by hand"
   // NOTE that in this case one should not give expressions (such as "var1+var2") in the input
   //      variable definition, but simply compute the expression before adding the event
   // ```cpp
   // // --- begin ----------------------------------------------------------
   // std::vector<Double_t> vars( 4 ); // vector has size of number of input variables
   // Float_t  treevars[4], weight;
   //
   // // Signal
   // for (UInt_t ivar=0; ivar<4; ivar++) signalTree->SetBranchAddress( Form( "var%i", ivar+1 ), &(treevars[ivar]) );
   // for (UInt_t i=0; i<signalTree->GetEntries(); i++) {
   //    signalTree->GetEntry(i);
   //    for (UInt_t ivar=0; ivar<4; ivar++) vars[ivar] = treevars[ivar];
   //    // add training and test events; here: first half is training, second is testing
   //    // note that the weight can also be event-wise
   //    if (i < signalTree->GetEntries()/2.0) dataloader->AddSignalTrainingEvent( vars, signalWeight );
   //    else                              dataloader->AddSignalTestEvent    ( vars, signalWeight );
   // }
   //
   // // Background (has event weights)
   // background->SetBranchAddress( "weight", &weight );
   // for (UInt_t ivar=0; ivar<4; ivar++) background->SetBranchAddress( Form( "var%i", ivar+1 ), &(treevars[ivar]) );
   // for (UInt_t i=0; i<background->GetEntries(); i++) {
   //    background->GetEntry(i);
   //    for (UInt_t ivar=0; ivar<4; ivar++) vars[ivar] = treevars[ivar];
   //    // add training and test events; here: first half is training, second is testing
   //    // note that the weight can also be event-wise
   //    if (i < background->GetEntries()/2) dataloader->AddBackgroundTrainingEvent( vars, backgroundWeight*weight );
   //    else                                dataloader->AddBackgroundTestEvent    ( vars, backgroundWeight*weight );
   // }
   // // --- end ------------------------------------------------------------
   // ```
   // End of tree registration

   // Set individual event weights (the variables must exist in the original TTree)
   // -  for signal    : `dataloader->SetSignalWeightExpression    ("RSND*20");`  //in order to get a weight of order unity when its a really favorable situation
   //he's multiplying it by 20
   // -  for background: `dataloader->SetBackgroundWeightExpression("RSND*-20");`
   

   //dataloader->SetBackgroundWeightExpression( "TMND*-20" );
   //dataloader->SetSignalWeightExpression("TMND*20");

   // Apply additional cuts on the signal and background samples (can be different)
   // so in this code we're going to do a string comparison as our cut
   //TCut mycuts = "TMND > 0"; // for example: TCut mycuts = "abs(var1)<0.5 && abs(var2-0.5)<1"; s stands for signal
   //TCut mycutb = "TMND < 0"; // for example: TCut mycutb = "abs(var1)<0.5"; b stands for background

//if you don't use it as a weight, you don't need to add it as a spectator variable. Because it's available in the scope of the classifier, but its not available 
   //in the scope of the descriminant. 
//So the signal events are going to require the event has the S characterization for strike. And that the background events have "B". 

   // Tell the dataloader how to use the training and testing events
   //
   // If no numbers of events are given, half of the events in the tree are used
   // for training, and the other half for testing:
   //
   //    dataloader->PrepareTrainingAndTestTree( mycut, "SplitMode=random:!V" );
   //
   // To also specify the number of testing events, use:
   //
   //    dataloader->PrepareTrainingAndTestTree( mycut,
   //         "NSigTrain=3000:NBkgTrain=3000:NSigTest=3000:NBkgTest=3000:SplitMode=Random:!V" );

  
   //   TCut mycut = "";
   //   dataloader->PrepareTrainingAndTestTree( mycut,
					   //                                        "nTrain_Signal=20000:nTrain_Background=20000:SplitMode=Random:NormMode=NumEvents:!V" );

   //   TCut mycut = cutStr.c_str();
   
   //   dataloader->PrepareTrainingAndTestTree(mycut,
   //					  "nTrain_Signal=0:nTrain_Background=0:SplitMode=Random:NormMode=NumEvents:!V");
   // ### Book MVA methods
   //
   // Please lookup the various method configuration options in the corresponding cxx files, eg:
   // src/MethoCuts.cxx, etc, or here: http://tmva.sourceforge.net/optionRef.html
   // it is possible to preset ranges in the option string in which the cut optimisation should be done:
   // "...:CutRangeMin[2]=-1:CutRangeMax[2]=1"...", where [2] is the third input variable

   // Cut optimisation
   if (Use["Cuts"])
      factory->BookMethod( dataloader, TMVA::Types::kCuts, "Cuts",
                           "!H:!V:FitMethod=MC:EffSel:SampleSize=200000:VarProp=FSmart" );

   if (Use["CutsD"])
      factory->BookMethod( dataloader, TMVA::Types::kCuts, "CutsD",
                           "!H:!V:FitMethod=MC:EffSel:SampleSize=200000:VarProp=FSmart:VarTransform=Decorrelate" );

   if (Use["CutsPCA"])
      factory->BookMethod( dataloader, TMVA::Types::kCuts, "CutsPCA",
                           "!H:!V:FitMethod=MC:EffSel:SampleSize=200000:VarProp=FSmart:VarTransform=PCA" );

   if (Use["CutsGA"])
      factory->BookMethod( dataloader, TMVA::Types::kCuts, "CutsGA",
                           "H:!V:FitMethod=GA:CutRangeMin[0]=-10:CutRangeMax[0]=10:VarProp[1]=FMax:EffSel:Steps=30:Cycles=3:PopSize=400:SC_steps=10:SC_rate=5:SC_factor=0.95" );

   if (Use["CutsSA"])
      factory->BookMethod( dataloader, TMVA::Types::kCuts, "CutsSA",
                           "!H:!V:FitMethod=SA:EffSel:MaxCalls=150000:KernelTemp=IncAdaptive:InitialTemp=1e+6:MinTemp=1e-6:Eps=1e-10:UseDefaultScale" );

   // Likelihood ("naive Bayes estimator")
   if (Use["Likelihood"])
      factory->BookMethod( dataloader, TMVA::Types::kLikelihood, "Likelihood",
                           "H:!V:TransformOutput:PDFInterpol=Spline2:NSmoothSig[0]=20:NSmoothBkg[0]=20:NSmoothBkg[1]=10:NSmooth=1:NAvEvtPerBin=50" );

   // Decorrelated likelihood
   if (Use["LikelihoodD"])
      factory->BookMethod( dataloader, TMVA::Types::kLikelihood, "LikelihoodD",
                           "!H:!V:TransformOutput:PDFInterpol=Spline2:NSmoothSig[0]=20:NSmoothBkg[0]=20:NSmooth=5:NAvEvtPerBin=50:VarTransform=Decorrelate" );

   // PCA-transformed likelihood
   if (Use["LikelihoodPCA"])
      factory->BookMethod( dataloader, TMVA::Types::kLikelihood, "LikelihoodPCA",
                           "!H:!V:!TransformOutput:PDFInterpol=Spline2:NSmoothSig[0]=20:NSmoothBkg[0]=20:NSmooth=5:NAvEvtPerBin=50:VarTransform=PCA" );

   // Use a kernel density estimator to approximate the PDFs
   if (Use["LikelihoodKDE"])
      factory->BookMethod( dataloader, TMVA::Types::kLikelihood, "LikelihoodKDE",
                           "!H:!V:!TransformOutput:PDFInterpol=KDE:KDEtype=Gauss:KDEiter=Adaptive:KDEFineFactor=0.3:KDEborder=None:NAvEvtPerBin=50" );

   // Use a variable-dependent mix of splines and kernel density estimator
   if (Use["LikelihoodMIX"])
      factory->BookMethod( dataloader, TMVA::Types::kLikelihood, "LikelihoodMIX",
                           "!H:!V:!TransformOutput:PDFInterpolSig[0]=KDE:PDFInterpolBkg[0]=KDE:PDFInterpolSig[1]=KDE:PDFInterpolBkg[1]=KDE:PDFInterpolSig[2]=Spline2:PDFInterpolBkg[2]=Spline2:PDFInterpolSig[3]=Spline2:PDFInterpolBkg[3]=Spline2:KDEtype=Gauss:KDEiter=Nonadaptive:KDEborder=None:NAvEvtPerBin=50" );

   // Test the multi-dimensional probability density estimator
   // here are the options strings for the MinMax and RMS methods, respectively:
   //
   //      "!H:!V:VolumeRangeMode=MinMax:DeltaFrac=0.2:KernelEstimator=Gauss:GaussSigma=0.3" );
   //      "!H:!V:VolumeRangeMode=RMS:DeltaFrac=3:KernelEstimator=Gauss:GaussSigma=0.3" );
   if (Use["PDERS"])
      factory->BookMethod( dataloader, TMVA::Types::kPDERS, "PDERS",
                           "!H:!V:NormTree=T:VolumeRangeMode=Adaptive:KernelEstimator=Gauss:GaussSigma=0.3:NEventsMin=400:NEventsMax=600" );

   if (Use["PDERSD"])
      factory->BookMethod( dataloader, TMVA::Types::kPDERS, "PDERSD",
                           "!H:!V:VolumeRangeMode=Adaptive:KernelEstimator=Gauss:GaussSigma=0.3:NEventsMin=400:NEventsMax=600:VarTransform=Decorrelate" );

   if (Use["PDERSPCA"])
      factory->BookMethod( dataloader, TMVA::Types::kPDERS, "PDERSPCA",
                           "!H:!V:VolumeRangeMode=Adaptive:KernelEstimator=Gauss:GaussSigma=0.3:NEventsMin=400:NEventsMax=600:VarTransform=PCA" );

   // Multi-dimensional likelihood estimator using self-adapting phase-space binning
   if (Use["PDEFoam"])
      factory->BookMethod( dataloader, TMVA::Types::kPDEFoam, "PDEFoam",
                           "!H:!V:SigBgSeparate=F:TailCut=0.001:VolFrac=0.0666:nActiveCells=500:nSampl=2000:nBin=5:Nmin=100:Kernel=None:Compress=T" );

   if (Use["PDEFoamBoost"])
      factory->BookMethod( dataloader, TMVA::Types::kPDEFoam, "PDEFoamBoost",
                           "!H:!V:Boost_Num=30:Boost_Transform=linear:SigBgSeparate=F:MaxDepth=4:UseYesNoCell=T:DTLogic=MisClassificationError:FillFoamWithOrigWeights=F:TailCut=0:nActiveCells=500:nBin=20:Nmin=400:Kernel=None:Compress=T" );

   // K-Nearest Neighbour classifier (KNN)
   if (Use["KNN"])
      factory->BookMethod( dataloader, TMVA::Types::kKNN, "KNN",
                           "H:nkNN=20:ScaleFrac=0.8:SigmaFact=1.0:Kernel=Gaus:UseKernel=F:UseWeight=T:!Trim" );

   // H-Matrix (chi2-squared) method
   if (Use["HMatrix"])
      factory->BookMethod( dataloader, TMVA::Types::kHMatrix, "HMatrix", "!H:!V:VarTransform=None" );

   // Linear discriminant (same as Fisher discriminant)
   if (Use["LD"])
      factory->BookMethod( dataloader, TMVA::Types::kLD, "LD", "H:!V:VarTransform=None:CreateMVAPdfs:PDFInterpolMVAPdf=Spline2:NbinsMVAPdf=50:NsmoothMVAPdf=10" );

   // Fisher discriminant (same as LD)
   if (Use["Fisher"])
      factory->BookMethod( dataloader, TMVA::Types::kFisher, "Fisher", "H:!V:Fisher:VarTransform=None:CreateMVAPdfs:PDFInterpolMVAPdf=Spline2:NbinsMVAPdf=50:NsmoothMVAPdf=10" );

   // Fisher with Gauss-transformed input variables
   if (Use["FisherG"])
      factory->BookMethod( dataloader, TMVA::Types::kFisher, "FisherG", "H:!V:VarTransform=Gauss" );

   // Composite classifier: ensemble (tree) of boosted Fisher classifiers
   if (Use["BoostedFisher"])
      factory->BookMethod( dataloader, TMVA::Types::kFisher, "BoostedFisher",
                           "H:!V:Boost_Num=20:Boost_Transform=log:Boost_Type=AdaBoost:Boost_AdaBoostBeta=0.2:!Boost_DetailedMonitoring" );

   // Function discrimination analysis (FDA) -- test of various fitters - the recommended one is Minuit (or GA or SA)
   if (Use["FDA_MC"])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_MC",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=MC:SampleSize=100000:Sigma=0.1" );

   if (Use["FDA_GA"]) // can also use Simulated Annealing (SA) algorithm (see Cuts_SA options])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_GA",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=GA:PopSize=300:Cycles=3:Steps=20:Trim=True:SaveBestGen=1" );

   if (Use["FDA_SA"]) // can also use Simulated Annealing (SA) algorithm (see Cuts_SA options])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_SA",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=SA:MaxCalls=15000:KernelTemp=IncAdaptive:InitialTemp=1e+6:MinTemp=1e-6:Eps=1e-10:UseDefaultScale" );

   if (Use["FDA_MT"])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_MT",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=MINUIT:ErrorLevel=1:PrintLevel=-1:FitStrategy=2:UseImprove:UseMinos:SetBatch" );

   if (Use["FDA_GAMT"])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_GAMT",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=GA:Converger=MINUIT:ErrorLevel=1:PrintLevel=-1:FitStrategy=0:!UseImprove:!UseMinos:SetBatch:Cycles=1:PopSize=5:Steps=5:Trim" );

   if (Use["FDA_MCMT"])
      factory->BookMethod( dataloader, TMVA::Types::kFDA, "FDA_MCMT",
                           "H:!V:Formula=(0)+(1)*x0+(2)*x1+(3)*x2+(4)*x3:ParRanges=(-1,1);(-10,10);(-10,10);(-10,10);(-10,10):FitMethod=MC:Converger=MINUIT:ErrorLevel=1:PrintLevel=-1:FitStrategy=0:!UseImprove:!UseMinos:SetBatch:SampleSize=20" );

   // TMVA ANN: MLP (recommended ANN) -- all ANNs in TMVA are Multilayer Perceptrons
   if (Use["MLP"]) {
      // If the vars INI provides a full options string in section [mlp_options]
      // it will be parsed as key "mlp-options" and we use it directly.
      std::string mlpOptions = getp("mlp-options", "");
      if (!mlpOptions.empty()) {
        factory->BookMethod( dataloader, TMVA::Types::kMLP, "MLP", mlpOptions.c_str() );
      } else {
        // Build option string from granular hyperparameters (if provided)
        std::string mlpNeuron = getp("mlp-neuron", "tanh");
        std::string mlpVarTrans = getp("mlp-varTransform", "N");
        std::string mlpNCycles = getp("mlp-ncycles", "600");
        std::string mlpHidden = getp("mlp-hidden", "N+5");
        std::string mlpTestRate = getp("mlp-testrate", "5");
        std::string mlpTrainingMethod = getp("mlp-trainingMethod", "");
        std::string mlpUseReg = getp("mlp-useRegulator", "false");

        std::ostringstream mo;
        mo << "H:!V:NeuronType=" << mlpNeuron
           << ":VarTransform=" << mlpVarTrans
           << ":NCycles=" << mlpNCycles
           << ":HiddenLayers=" << mlpHidden
           << ":TestRate=" << mlpTestRate;
        if (!mlpTrainingMethod.empty()) mo << ":TrainingMethod=" << mlpTrainingMethod;
        if (mlpUseReg == "true" || mlpUseReg == "1") mo << ":UseRegulator";
        else mo << ":!UseRegulator";

        factory->BookMethod( dataloader, TMVA::Types::kMLP, "MLP", mo.str().c_str() );
      }
   }

   if (Use["MLPBFGS"])
      factory->BookMethod( dataloader, TMVA::Types::kMLP, "MLPBFGS", "H:!V:NeuronType=tanh:VarTransform=N:NCycles=600:HiddenLayers=N+5:TestRate=5:TrainingMethod=BFGS:!UseRegulator" );

   if (Use["MLPBNN"])
      factory->BookMethod( dataloader, TMVA::Types::kMLP, "MLPBNN", "H:!V:NeuronType=tanh:VarTransform=N:NCycles=600:HiddenLayers=N+5:TestRate=5:TrainingMethod=BFGS:UseRegulator" ); // BFGS training with bayesian regulators


   // Multi-architecture DNN implementation.
   if (Use["DNN"])
   {
      // General layout.
      TString layoutString ("Layout=TANH|128,TANH|128,TANH|128,LINEAR");

      // Training strategies.
      TString training0("LearningRate=1e-1,Momentum=0.9,Repetitions=1,"
                        "ConvergenceSteps=20,BatchSize=256,TestRepetitions=10,"
                        "WeightDecay=1e-4,Regularization=L2,"
                        "DropConfig=0.0+0.5+0.5+0.5, Multithreading=True");
      TString training1("LearningRate=1e-2,Momentum=0.9,Repetitions=1,"
                        "ConvergenceSteps=20,BatchSize=256,TestRepetitions=10,"
                        "WeightDecay=1e-4,Regularization=L2,"
                        "DropConfig=0.0+0.0+0.0+0.0, Multithreading=True");
      TString training2("LearningRate=1e-3,Momentum=0.0,Repetitions=1,"
                        "ConvergenceSteps=20,BatchSize=256,TestRepetitions=10,"
                        "WeightDecay=1e-4,Regularization=L2,"
                        "DropConfig=0.0+0.0+0.0+0.0, Multithreading=True");
      TString trainingStrategyString ("TrainingStrategy=");
      trainingStrategyString += training0 + "|" + training1 + "|" + training2;

      // General Options.
      TString dnnOptions ("!H:V:ErrorStrategy=CROSSENTROPY:VarTransform=N:"
                          "WeightInitialization=XAVIERUNIFORM");
      dnnOptions.Append (":"); dnnOptions.Append (layoutString);
      dnnOptions.Append (":"); dnnOptions.Append (trainingStrategyString);

      // Standard implementation, no dependencies.
      TString stdOptions = dnnOptions + ":Architecture=STANDARD";
      factory->BookMethod(dataloader, TMVA::Types::kDNN, "DNN", stdOptions);

      // Cuda implementation.
      if (Use["DNN_GPU"]) {
         TString gpuOptions = dnnOptions + ":Architecture=GPU";
         factory->BookMethod(dataloader, TMVA::Types::kDNN, "DNN GPU", gpuOptions);
      }
      // Multi-core CPU implementation.
      if (Use["DNN_CPU"]) {
         TString cpuOptions = dnnOptions + ":Architecture=CPU";
         factory->BookMethod(dataloader, TMVA::Types::kDNN, "DNN CPU", cpuOptions);
      }
   }

   // CF(Clermont-Ferrand)ANN
   if (Use["CFMlpANN"])
      factory->BookMethod( dataloader, TMVA::Types::kCFMlpANN, "CFMlpANN", "!H:!V:NCycles=2000:HiddenLayers=N+1,N"  ); // n_cycles:#nodes:#nodes:...

   // Tmlp(Root)ANN
   if (Use["TMlpANN"])
      factory->BookMethod( dataloader, TMVA::Types::kTMlpANN, "TMlpANN", "!H:!V:NCycles=200:HiddenLayers=N+1,N:LearningMethod=BFGS:ValidationFraction=0.3"  ); // n_cycles:#nodes:#nodes:...

   // Support Vector Machine
   if (Use["SVM"])
      factory->BookMethod( dataloader, TMVA::Types::kSVM, "SVM", "Gamma=0.25:Tol=0.001:VarTransform=Norm" );

   // Boosted Decision Trees
   if (Use["BDTG"]) // Gradient Boost
   {
      std::string bdtOptions = getp("bdt-options", "");
      if (!bdtOptions.empty()) {
         factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDTG", bdtOptions.c_str() );
      } else {
         std::string bdt_ntrees = getp("bdt-ntrees", "1000");
         std::string bdt_minNode = getp("bdt-minnodesize", "2.5%");
         std::string bdt_boost = getp("bdt-boosttype", "Grad");
         std::string bdt_shrink = getp("bdt-shrinkage", "0.10");
         std::string bdt_useBag = getp("bdt-useBaggedBoost", "true");
         std::string bdt_bagFrac = getp("bdt-baggedSampleFraction", "0.5");
         std::string bdt_nCuts = getp("bdt-ncuts", "20");
         std::string bdt_maxDepth = getp("bdt-maxdepth", "2");
         std::ostringstream bo;
         bo << "!H:!V:NTrees=" << bdt_ntrees
            << ":MinNodeSize=" << bdt_minNode
            << ":BoostType=" << bdt_boost
            << ":Shrinkage=" << bdt_shrink;
         if (bdt_useBag == "true" || bdt_useBag == "1") bo << ":UseBaggedBoost:BaggedSampleFraction=" << bdt_bagFrac;
         bo << ":nCuts=" << bdt_nCuts
            << ":MaxDepth=" << bdt_maxDepth;
         factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDTG", bo.str().c_str() );
      }
   }

   if (Use["BDT"])  // Adaptive Boost
   {
      std::string bdtOptions = getp("bdt-options", "");
      if (!bdtOptions.empty()) {
         factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDT", bdtOptions.c_str() );
      } else {
         std::string bdt_ntrees = getp("bdt-ntrees", "850");
         std::string bdt_minNode = getp("bdt-minnodesize", "2.5%");
         std::string bdt_maxDepth = getp("bdt-maxdepth", "3");
         std::string bdt_boost = getp("bdt-boosttype", "AdaBoost");
         std::string bdt_adaBeta = getp("bdt-adabeta", "0.5");
         std::string bdt_useBag = getp("bdt-useBaggedBoost", "true");
         std::string bdt_bagFrac = getp("bdt-baggedSampleFraction", "0.5");
         std::string bdt_nCuts = getp("bdt-ncuts", "20");
         std::ostringstream bo;
         bo << "!H:!V:NTrees=" << bdt_ntrees
            << ":MinNodeSize=" << bdt_minNode
            << ":MaxDepth=" << bdt_maxDepth
            << ":BoostType=" << bdt_boost
            << ":AdaBoostBeta=" << bdt_adaBeta;
         if (bdt_useBag == "true" || bdt_useBag == "1") bo << ":UseBaggedBoost:BaggedSampleFraction=" << bdt_bagFrac;
         bo << ":SeparationType=GiniIndex:nCuts=" << bdt_nCuts;
         factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDT", bo.str().c_str() );
      }
   }

   if (Use["BDTB"]) // Bagging
      factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDTB",
                           "!H:!V:NTrees=400:BoostType=Bagging:SeparationType=GiniIndex:nCuts=20" );

   if (Use["BDTD"]) // Decorrelation + Adaptive Boost
      factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDTD",
                           "!H:!V:NTrees=400:MinNodeSize=5%:MaxDepth=3:BoostType=AdaBoost:SeparationType=GiniIndex:nCuts=20:VarTransform=Decorrelate" );

   if (Use["BDTF"])  // Allow Using Fisher discriminant in node splitting for (strong) linearly correlated variables
      factory->BookMethod( dataloader, TMVA::Types::kBDT, "BDTF",
                           "!H:!V:NTrees=50:MinNodeSize=2.5%:UseFisherCuts:MaxDepth=3:BoostType=AdaBoost:AdaBoostBeta=0.5:SeparationType=GiniIndex:nCuts=20" );

   // RuleFit -- TMVA implementation of Friedman's method
   if (Use["RuleFit"])
      factory->BookMethod( dataloader, TMVA::Types::kRuleFit, "RuleFit",
                           "H:!V:RuleFitModule=RFTMVA:Model=ModRuleLinear:MinImp=0.001:RuleMinDist=0.001:NTrees=20:fEventsMin=0.01:fEventsMax=0.5:GDTau=-1.0:GDTauPrec=0.01:GDStep=0.01:GDNSteps=10000:GDErrScale=1.02" );

   // For an example of the category classifier usage, see: TMVAClassificationCategory
   //
   // --------------------------------------------------------------------------------------------------
   //  Now you can optimize the setting (configuration) of the MVAs using the set of training events
   // STILL EXPERIMENTAL and only implemented for BDT's !
   //
   //     factory->OptimizeAllMethods("SigEffAt001","Scan");
   //     factory->OptimizeAllMethods("ROCIntegral","FitGA");
   //
   // --------------------------------------------------------------------------------------------------

   // Now you can tell the factory to train, test, and evaluate the MVAs
   //
   // Train MVAs using the set of training events
   factory->TrainAllMethods();

   // Evaluate all MVAs using the set of test events
   factory->TestAllMethods();

   // Evaluate and compare performance of all configured MVAs
   factory->EvaluateAllMethods();

   // --------------------------------------------------------------

   // Save the output
   outputFile->Close();

   std::cout << "==> Wrote root file: " << outputFile->GetName() << std::endl;
   std::cout << "==> TMVAClassification is done!" << std::endl;

   delete factory;
   delete dataloader;
   // Launch the GUI for the root macros
       // By default don't launch the TMVAGui (it blocks/opens a window). To enable the GUI
       // set environment variable TMVA_SHOW_GUI=1 before running ROOT.
       if (const char* show = std::getenv("TMVA_SHOW_GUI")) {
          if (std::string(show) == "1") {
             if (!gROOT->IsBatch()) TMVA::TMVAGui( outfileName );
          }
       } else {
          std::cout << "TMVAGui suppressed (set TMVA_SHOW_GUI=1 to enable)" << std::endl;
       }

   return 0;
}

int main( int argc, char** argv )
{
   // Parse command-line arguments into a method list and a map of hyperparameters.
   // Usage examples:
   //   root -l TMVAClassification.C\("BDT,MLP"\)
   //   root -l TMVAClassification.C -- --mlp-hidden=N+5 --bdt-ntrees=500 BDT MLP
   TString methodList;
   std::map<std::string,std::string> hparams;

   for (int i=1; i<argc; ++i) {
      std::string a(argv[i]);
      // preserve original batch flag handling
      if (a == "-b" || a == "--batch") continue;

      if (a.rfind("--", 0) == 0) {
         // --key=value  OR  --key value  OR  --flag (boolean)
         std::string body = a.substr(2);
         std::string key, val;
         size_t eq = body.find('=');
         if (eq != std::string::npos) {
            key = body.substr(0, eq);
            val = body.substr(eq + 1);
         } else {
            // look ahead for a value that doesn't start with '-'
            if (i + 1 < argc) {
               std::string next(argv[i+1]);
               if (!next.empty() && next[0] != '-') {
                  val = next;
                  ++i; // consume
               } else {
                  val = "1"; // boolean flag
               }
            } else {
               val = "1"; // boolean flag
            }
            key = body;
         }
         // store as-is (use same key names as used in this file, e.g. "mlp-hidden")
         hparams[key] = val;
      } else {
         // Non-flag token: treat as method name(s). Allow comma-separated list.
         std::string token = a;
         size_t start = 0;
         while (start < token.size()) {
            size_t comma = token.find(',', start);
            std::string piece = (comma == std::string::npos) ? token.substr(start) : token.substr(start, comma - start);
            std::string trimmed = piece;
            // trim whitespace
            auto l = trimmed.find_first_not_of(" \t\r\n");
            auto r = trimmed.find_last_not_of(" \t\r\n");
            if (l == std::string::npos) trimmed = "";
            else trimmed = trimmed.substr(l, r - l + 1);
            if (!trimmed.empty()) {
               if (!methodList.IsNull()) methodList += TString(",");
               methodList += TString(trimmed.c_str());
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
         }
      }
   }

   // If user provided --methods=... prefer that over positional method tokens
   auto it = hparams.find("methods");
   if (it != hparams.end()) {
      if (!methodList.IsNull()) methodList = TString( it->second.c_str() );
      else methodList = TString( it->second.c_str() );
   }

   // Optionally echo parsed hparams for user's visibility
   if (!hparams.empty()) {
      std::cout << "Parsed hyperparameters:\n";
      for (const auto &kv : hparams) std::cout << "  " << kv.first << " = " << kv.second << "\n";
   }

   return TMVAClassification(methodList, hparams);
}
//overtraining plot, you want them to overlap as much as possible.
//need to look at the one that has the overlap of the training and the test, and make sure that they arent doing the same thing--that you're not over trained. 
