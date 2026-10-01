#include "DSelector_2eMissingProton.h"

// ========== PARTICLE TYPE CONFIGURATION ==========
// Define particle labels for easy porting between channels
// These constants control histogram names and ROOT LaTeX formatting
// 
// TO PORT TO A DIFFERENT CHANNEL:
// Simply change these three definitions and all histograms will update automatically
//
// Current configuration: e+e- channel
static const char* PARTICLE_PLUS = "ep";      // Histogram name suffix for positive track
static const char* PARTICLE_MINUS = "em";     // Histogram name suffix for negative track  
static const char* PARTICLE_LATEX = "e";     // ROOT LaTeX symbol for plot titles

//
// Examples for other channels:
//   Electrons: PARTICLE_PLUS="ep",  PARTICLE_MINUS="em",  PARTICLE_LATEX="e"
//   Kaons:     PARTICLE_PLUS="kp",  PARTICLE_MINUS="km",  PARTICLE_LATEX="K"
//   Muons:     PARTICLE_PLUS="mup", PARTICLE_MINUS="mum", PARTICLE_LATEX="#mu"
//
// This gives you:
//   - Histogram names like "FCAL_Energy_pip" or "FCAL_Energy_ep"
//   - Invariant mass like "InvMass_pipim" or "InvMass_epem"
//   - Axis labels like "E/p π+" or "E/p e+"
// =================================================

// Sanitize function to protect against NaN/Inf values in flat tree
// Replaces non-finite values with sentinel value (default -999)
static inline Float_t sanitize_f(Float_t v, Float_t sentinel = -999.f) {
    return std::isfinite((double)v) ? v : sentinel;
}

void DSelector_2eMissingProton::Init(TTree *locTree)
{
	// USERS: IN THIS FUNCTION, ONLY MODIFY SECTIONS WITH A "USER" OR "EXAMPLE" LABEL. LEAVE THE REST ALONE.

	// The Init() function is called when the selector needs to initialize a new tree or chain.
	// Typically here the branch addresses and branch pointers of the tree will be set.
	// Init() will be called many times when running on PROOF (once per file to be processed).

	//USERS: SET OUTPUT FILE NAME //can be overriden by user in PROOF
	dOutputFileName = "FFS1_qDATAq.root"; //"" for none
	dOutputTreeFileName = "";//"ee_MVA_7-20-2020_ComboTrees.root"; //"" for none
	dFlatTreeFileName = "Flat_FFS1_qDATAq_epemmissp__B2_BestX2.root"; //output flat tree (one combo per tree entry), "" for none
	dFlatTreeName = "Flat_FFS1_qDATAq_epemmissp__B2_BestX2"; //if blank, default name will be chosen

	// AUTO-GENERATE OUTPUT FILE NAMES FROM INPUT FILE NAME
	// Extract run period and angle identifier from input file path (e.g., "1801_45DEG", "1808_0DEG")
	if(locTree->GetCurrentFile() != NULL) {
		TString inputFileName = locTree->GetCurrentFile()->GetName();
		TString baseName = gSystem->BaseName(inputFileName);  // Get filename without path
		
		// Extract identifier from input file name
		// Look for pattern: <run_period>_<angle>DEG (e.g., 1801_45DEG, 1808_0DEG, etc.)
		TString identifier = "";
		
		// Use regex-like approach: find 4-digit run period followed by angle
		TObjArray* tokens = baseName.Tokenize("_/.");
		for(Int_t i = 0; i < tokens->GetEntries()-1; i++) {
			TString token = ((TObjString*)tokens->At(i))->GetString();
			TString nextToken = ((TObjString*)tokens->At(i+1))->GetString();
			
			// Check if token is a 4-digit number (run period like 1801, 1808, etc.)
			if(token.Length() == 4 && token.IsDigit()) {
				// Check if next token contains angle (DEG or deg)
				if(nextToken.Contains("DEG") || nextToken.Contains("deg")) {
					identifier = token + "_" + nextToken;
					identifier.ReplaceAll("deg", "DEG");  // Standardize to uppercase
					break;
				}
			}
			// Also check reversed pattern: angle followed by run period (e.g., 0DEG_1801)
			if(token.Contains("DEG") || token.Contains("deg")) {
				if(nextToken.Length() == 4 && nextToken.IsDigit()) {
					TString angle = token;
					angle.ReplaceAll("deg", "DEG");
					identifier = nextToken + "_" + angle;
					break;
				}
			}
		}
		delete tokens;
		
		// Update output file names by inserting identifier into the template names
		if(identifier != "") {
			// Insert identifier after "qDATAq" in the output file names
			dOutputFileName = "FFS1_qDATAq_" + identifier + ".root";
			dFlatTreeFileName = "Flat_FFS1_qDATAq_" + identifier + "_epemmissp__B2_BestX2.root";
			// Keep tree name the SAME across all files so hadd can merge them
			dFlatTreeName = "Flat_FFS1_qDATAq_epemmissp__B2_BestX2";  // NO identifier in tree name!
			cout << "Auto-generated output names from input: " << baseName << endl;
			cout << "  Identifier extracted: " << identifier << endl;
			cout << "  Histogram file: " << dOutputFileName << endl;
			cout << "  Flat tree file: " << dFlatTreeFileName << endl;
			cout << "  Flat tree name: " << dFlatTreeName << " (same for all files for hadd)" << endl;
		} else {
			cout << "WARNING: Could not extract run period and angle from: " << baseName << endl;
			cout << "  Using default output file names" << endl;
		}
	}
	  
	//Because this function gets called for each TTree in the TChain, we must be careful:
		//We need to re-initialize the tree interface & branch wrappers, but don't want to recreate histograms
	bool locInitializedPriorFlag = dInitializedFlag; //save whether have been initialized previously
	DSelector::Init(locTree); //This must be called to initialize wrappers for each new TTree
	//gDirectory now points to the output file with name dOutputFileName (if any)
	if(locInitializedPriorFlag)
		return; //have already created histograms, etc. below: exit

	Get_ComboWrappers();
	dPreviousRunNumber = 0;

	/*********************************** EXAMPLE USER INITIALIZATION: ANALYSIS ACTIONS **********************************/

	// EXAMPLE: Create deque for histogramming particle masses:
	// // For histogramming the phi mass in phi -> K+ K-
	// // Be sure to change this and dAnalyzeCutActions to match reaction
	//std::deque<Particle_t> MyPhi;
	//MyPhi.push_back(KPlus); MyPhi.push_back(KMinus);

	//ANALYSIS ACTIONS: //Executed in order if added to dAnalysisActions
	//false/true below: use measured/kinfit data

	//PID
	//	dAnalysisActions.push_back(new DHistogramAction_ParticleID(dComboWrapper, false)); //This was DEFAULT setting. I copied from pi file whats below
	dAnalysisActions.push_back(new DHistogramAction_ParticleID(dComboWrapper, false)); //false: use measured data                                                  
	dAnalysisActions.push_back(new DHistogramAction_ParticleID(dComboWrapper, true, "KinFit")); //true: use kinfit data 
	//below: value: +/- N ns, Unknown: All PIDs, SYS_NULL: all timing systems
	//dAnalysisActions.push_back(new DCutAction_PIDDeltaT(dComboWrapper, false, 0.5, KPlus, SYS_BCAL));

	//MASSES
	//dAnalysisActions.push_back(new DHistogramAction_InvariantMass(dComboWrapper, false, Lambda, 1000, 1.0, 1.2, "Lambda"));
	//dAnalysisActions.push_back(new DHistogramAction_MissingMassSquared(dComboWrapper, false, 1000, -0.1, 0.1));

	//KINFIT RESULTS
	dAnalysisActions.push_back(new DHistogramAction_KinFitResults(dComboWrapper));

	//CUT MISSING MASS
	//dAnalysisActions.push_back(new DCutAction_MissingMassSquared(dComboWrapper, false, -0.03, 0.02));

	//BEAM ENERGY
	dAnalysisActions.push_back(new DHistogramAction_BeamEnergy(dComboWrapper, false));
	//dAnalysisActions.push_back(new DCutAction_BeamEnergy(dComboWrapper, false, 8.4, 9.05));

	//KINEMATICS
	dAnalysisActions.push_back(new DHistogramAction_ParticleComboKinematics(dComboWrapper, false));

	// ANALYZE CUT ACTIONS
	 // Change MyPhi to match reaction
	//dAnalyzeCutActions = new DHistogramAction_AnalyzeCutActions( dAnalysisActions, dComboWrapper, false, 0, MyPhi, 1000, 0.9, 2.4, "CutActionEffect" );

	//INITIALIZE ACTIONS
	//If you create any actions that you want to run manually (i.e. don't add to dAnalysisActions), be sure to initialize them here as well
	Initialize_Actions();
	//	dAnalyzeCutActions->Initialize(); // manual action, must call Initialize()

	/******************************** USER INITIALIZATION: STAND-ALONE HISTOGRAMS *******************************/
	//EXAMPLE MANUAL HISTOGRAMS:
	// Create directory structure for organizing histograms
	TDirectory *dir_Main = gDirectory->mkdir("Main");
	TDirectory *dir_FCAL = dir_Main->mkdir("FCAL");
	TDirectory *dir_BCAL = dir_Main->mkdir("BCAL");
	TDirectory *dir_epemPol = gDirectory->mkdir("epemPol");
	TDirectory *dir_Rho0Pol = gDirectory->mkdir("Rho0Pol");
	// Top-level Main directory histograms
	dir_Main->cd();
	dHist_MissingMassSquared = new TH1I("MissingMassSquared", ";Missing Mass Squared (GeV/c^{2})^{2}", 600, -0.06, 0.06);
	dHist_BeamEnergy = new TH1I("BeamEnergy", ";Beam Energy (GeV)", 600, 0.0, 12.0);
	dHist_TaggerAccidentals = new TH1I("dHist_TaggerAccidentals", "Vertex time - RF (ns)", 400,-20,20);

	dHist_BeamEnergy_BestChiSq = new TH1I("BeamEnergy_BestChiSq", ";Beam Energy (GeV)", 600, 0.0, 12.0);
	dHist_KinFitChiSq = new TH1D("KinFitChiSq", ";Kinematic Fit #chi^{2}", 200, 0.0, 50.0);
	dHist_KinFitCL = new TH1D("KinFitCL", ";Kinematic Fit Confidence Level", 200, 0.0, 1.0);

	dHist_InvMass_TwoTrack = new TH1D(Form("InvMass_%s%s", PARTICLE_PLUS, PARTICLE_MINUS), 
		Form(";Invariant Mass %s^{+}%s^{-} (GeV/c^{2})", PARTICLE_LATEX, PARTICLE_LATEX), 300, 0.25, 1.2);
	dHist_InvMass_TwoTrack_BestChiSq = new TH1D(Form("InvMass_%s%s_BestChiSq", PARTICLE_PLUS, PARTICLE_MINUS), 
		Form(";Invariant Mass %s^{+}%s^{-} (GeV/c^{2})", PARTICLE_LATEX, PARTICLE_LATEX), 300, 0.25, 1.2);

	dHist_MLPResponse_plus = new TH1D(Form("MLPResponse_%s", PARTICLE_PLUS), 
		Form(";MLP Response %s^{+}", PARTICLE_LATEX), 100, 0.0, 1.0);
	dHist_MLPResponse_minus = new TH1D(Form("MLPResponse_%s", PARTICLE_MINUS), 
		Form(";MLP Response %s^{-}", PARTICLE_LATEX), 100, 0.0, 1.0);

	dHist_MLPResponsePlus_vs_MLPResponseMinus = new TH2D("MLPResponsePlus_vs_MLPResponseMinus", 
		";MLP Response e^{+};MLP Response e^{-}", 100, 0.0, 1.0, 100, 0.0, 1.0);
	// FCAL subdirectory histograms
	dir_FCAL->cd();

	dHist_FCAL_Energy_pip = new TH1D(Form("FCAL_Energy_%s", PARTICLE_PLUS), ";GeV", 200, 0, 6);
	dHist_FCAL_Energy_pim = new TH1D(Form("FCAL_Energy_%s", PARTICLE_MINUS), ";GeV", 200, 0, 6);
	dHist_FCAL_EoverP_pip = new TH1D(Form("FCAL_EoverP_%s", PARTICLE_PLUS), 
		Form(";E/p %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.2);
	dHist_FCAL_EoverP_pim = new TH1D(Form("FCAL_EoverP_%s", PARTICLE_MINUS), 
		Form(";E/p %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.2);
	dHist_FCAL_EoverPmeas_pip = new TH1D(Form("FCAL_EoverPmeas_%s", PARTICLE_PLUS), 
		Form(";E_{FCAL}/P_{meas} %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.2);
	dHist_FCAL_EoverPmeas_pim = new TH1D(Form("FCAL_EoverPmeas_%s", PARTICLE_MINUS), 
		Form(";E_{FCAL}/P_{meas} %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.2);
	dHist_Delta_Efcal_kinfitE_vs_kinPmag_plus = new TH2D(Form("Delta_Efcal-kinfitE_vs_kinPmag_%s", PARTICLE_PLUS), 
		Form(";p_{kinfit} %s^{+};E_{FCAL} - E_{kinfit} %s^{+}", PARTICLE_LATEX, PARTICLE_LATEX), 200, 0.0, 9.0, 200, -2.0, 2.0);
	dHist_Delta_Efcal_kinfitE_vs_kinPmag_minus = new TH2D(Form("Delta_Efcal-kinfitE_vs_kinPmag_%s", PARTICLE_MINUS), 
		Form(";p_{kinfit} %s^{-};E_{FCAL} - E_{kinfit} %s^{-}", PARTICLE_LATEX, PARTICLE_LATEX), 200, 0.0, 9.0, 200, -2.0, 2.0);
	dHist_Delta_Efcal_kinfitE_vs_kinPtheta_plus = new TH2D(Form("Delta_Efcal-kinfitE_vs_kinPtheta_%s", PARTICLE_PLUS), 
		Form(";#theta_{kinfit} %s^{+};E_{FCAL} - E_{kinfit} %s^{+}", PARTICLE_LATEX, PARTICLE_LATEX), 200, 0.0, 15.0, 200, -2.0, 2.0);
	dHist_Delta_Efcal_kinfitE_vs_kinPtheta_minus = new TH2D(Form("Delta_Efcal-kinfitE_vs_kinPtheta_%s", PARTICLE_MINUS), 
		Form(";#theta_{kinfit} %s^{-};E_{FCAL} - E_{kinfit} %s^{-}", PARTICLE_LATEX, PARTICLE_LATEX), 200, 0.0, 15.0, 200, -2.0, 2.0);

	dHist_FCAL_Elasticity = new TH1D("FCAL_Elasticity", ";(E1 + E2)/Ebeam", 200, 0.0, 1.2);
	dHist_FCAL_Asymmetry = new TH1D("FCAL_Asymmetry", ";|(E1 - E2)/(E1 + E2)|", 200, 0.0, 1.0);
	//3 FCAL Asymmetry regions: FCAL_Asymmetry < 0.2, 0.2 < FCAL_Asymmetry < 0.5, FCAL_Asymmetry > 0.5. Create these histograms to study each region separately.
	dHist_FCAL_Elasticity_Asym_L0pt2 = new TH1D("FCAL_Elasticity_Asym_L0pt2", ";(E1 + E2)/Ebeam", 200, 0.0, 1.2);
	dHist_FCAL_Elasticity_Asym_G0pt2_L0pt5 = new TH1D("FCAL_Elasticity_Asym_G0pt2_L0pt5", ";(E1 + E2)/Ebeam", 200, 0.0, 1.2);
	dHist_FCAL_Elasticity_Asym_G0pt5 = new TH1D("FCAL_Elasticity_Asym_G0pt5", ";(E1 + E2)/Ebeam", 200, 0.0, 1.2);

	dHist_TrackFCAL_DOCA_plus = new TH1D(Form("FCAL_DOCA_%s", PARTICLE_PLUS), 
		Form(";FCAL_DOCA_%s", PARTICLE_PLUS), 200, 0, 4);
	dHist_TrackFCAL_DOCA_minus = new TH1D(Form("FCAL_DOCA_%s", PARTICLE_MINUS), 
		Form(";FCAL_DOCA_%s", PARTICLE_MINUS), 200, 0, 4);
	dHist_FCAL_E1E9_plus = new TH1D(Form("FCAL_E1E9_%s", PARTICLE_PLUS), 
		Form(";(Energy in central cell)/(Energy in 3x3) %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.1);
	dHist_FCAL_E1E9_minus = new TH1D(Form("FCAL_E1E9_%s", PARTICLE_MINUS), 
		Form(";(Energy in central cell)/(Energy in 3x3) %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.1);
	dHist_FCAL_E9E25_plus = new TH1D(Form("FCAL_E9E25_%s", PARTICLE_PLUS), 
		Form(";(Energy in 3x3)/(Energy in 5x5) %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.1);
	dHist_FCAL_E9E25_minus = new TH1D(Form("FCAL_E9E25_%s", PARTICLE_MINUS), 
		Form(";(Energy in 3x3)/(Energy in 5x5) %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.1);
	dHist_FCAL_kin_res_plus = new TH1D(Form("FCAL_kin_res_%s", PARTICLE_PLUS), 
		Form(";|(E_{FCAL} - E_{kinfit})|/E_{kinfit} %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.0);
	dHist_FCAL_kin_res_minus = new TH1D(Form("FCAL_kin_res_%s", PARTICLE_MINUS), 
		Form(";|(E_{FCAL} - E_{kinfit})|/E_{kinfit} %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.0);
	dHist_FCAL_meas_res_plus = new TH1D(Form("FCAL_meas_res_%s", PARTICLE_PLUS), 
		Form(";|(E_{FCAL} - E_{meas})|/E_{meas} %s^{+}", PARTICLE_LATEX), 200, 0.0, 1.0);
	dHist_FCAL_meas_res_minus = new TH1D(Form("FCAL_meas_res_%s", PARTICLE_MINUS), 
		Form(";|(E_{FCAL} - E_{meas})|/E_{meas} %s^{-}", PARTICLE_LATEX), 200, 0.0, 1.0);
	dHist_FCAL_Saturation_vs_Eshower_plus = new TH2D(Form("FCAL_saturation_vs_Eshower_%s", PARTICLE_PLUS), 
		";E shower ;Eshower*E1E9*E9E25", 100, 0, 12, 200, 0, 15);
	dHist_FCAL_Saturation_vs_Eshower_minus = new TH2D(Form("FCAL_saturation_vs_Eshower_%s", PARTICLE_MINUS), 
		";E shower ;Eshower*E1E9*E9E25", 100, 0, 12, 200, 0, 15);
	dHist_FCAL_Saturation_plus = new TH1D(Form("FCAL_saturation_%s", PARTICLE_PLUS), 
		";Eshower*E1E9*E9E25", 200, 0, 15);
	dHist_FCAL_Saturation_minus = new TH1D(Form("FCAL_saturation_%s", PARTICLE_MINUS), 
		";Eshower*E1E9*E9E25", 200, 0, 15);
	dHist_FCAL_SumU_plus = new TH1D(Form("FCAL_SumU_%s", PARTICLE_PLUS), 
		Form(";SumU %s^{+}", PARTICLE_LATEX), 200, 0., 25);
	dHist_FCAL_SumU_minus = new TH1D(Form("FCAL_SumU_%s", PARTICLE_MINUS), 
		Form(";SumU %s^{-}", PARTICLE_LATEX), 200, 0., 25);
	dHist_FCAL_SumV_plus = new TH1D(Form("FCAL_SumV_%s", PARTICLE_PLUS), 
		Form(";SumV %s^{+}", PARTICLE_LATEX), 200, 0., 20);
	dHist_FCAL_SumV_minus = new TH1D(Form("FCAL_SumV_%s", PARTICLE_MINUS), 
		Form(";SumV %s^{-}", PARTICLE_LATEX), 200, 0., 20);
	dHist_FCAL_UV_Asymmetry_plus = new TH1D(Form("FCAL_UV_Asymmetry_%s", PARTICLE_PLUS), 
		Form(";A_{uv} = |#sigma^{2}_{u} - #sigma^{2}_{v}|/|#sigma^{2}_{u} + #sigma^{2}_{v}| %s^{+}", PARTICLE_LATEX), 200, 0., 1.0);
	dHist_FCAL_UV_Asymmetry_minus = new TH1D(Form("FCAL_UV_Asymmetry_%s", PARTICLE_MINUS), 
		Form(";A_{uv} = |#sigma^{2}_{u} - #sigma^{2}_{v}|/|#sigma^{2}_{u} + #sigma^{2}_{v}| %s^{-}", PARTICLE_LATEX), 200, 0., 1.0);

	// epemPol directory histograms (electron mass hypothesis, regardless of actual particle type)
	dir_epemPol->cd();
	dHist_epemPol_InvMass_Kin = new TH1D("InvMass_epem_Kin", ";Invariant Mass e^{+}e^{-} (GeV/c^{2}) [Kinfit]", 300, 0.0, 1.2);
	dHist_epemPol_InvMass_Meas = new TH1D("InvMass_epem_Meas", ";Invariant Mass e^{+}e^{-} (GeV/c^{2}) [Measured]", 300, 0.0, 1.2);
	dHist_epemPol_qvec2 = new TH1D("qvec2", ";q_{vec}^{2} (GeV^{2})", 200, 0.0, 0.4);
	// Create histograms for each polarization angle
	for(int i = 0; i < nPolarizations; i++)
	{
		dHist_epemPol_JTphi_Kin[i] = new TH1D(Form("JTphi_Kin_%s", Polarizations[i]), 
			Form(";#phi_{JT} (deg) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_epemPol_JTphi_Meas[i] = new TH1D(Form("JTphi_Meas_%s", Polarizations[i]), 
			Form(";#phi_{JT} (deg) [Measured] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
	}

	// Rho0Pol directory histograms (pion mass hypothesis, regardless of actual particle type)
	dir_Rho0Pol->cd();
	dHist_Rho0Pol_InvMass_Kin = new TH1D("InvMass_pipi_Kin", ";Invariant Mass #pi^{+}#pi^{-} (GeV/c^{2}) [Kinfit]", 300, 0.0, 1.2);
	dHist_Rho0Pol_InvMass_Meas = new TH1D("InvMass_pipi_Meas", ";Invariant Mass #pi^{+}#pi^{-} (GeV/c^{2}) [Measured]", 300, 0.0, 1.2);
	// Create histograms for each polarization angle
	for(int i = 0; i < nPolarizations; i++)
	{
		dHist_Rho0Pol_CosThetaKin[i] = new TH1D(Form("CosThetaKin_%s", Polarizations[i]), 
			Form(";cos(#theta) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 100, -1.0, 1.0);
		dHist_Rho0Pol_CosThetaMeas[i] = new TH1D(Form("CosThetaMeas_%s", Polarizations[i]), 
			Form(";cos(#theta) [Measured] #theta_{pol}=%s#circ", Polarizations[i]), 100, -1.0, 1.0);
		dHist_Rho0Pol_phiKin[i] = new TH1D(Form("phiKin_%s", Polarizations[i]), 
			Form(";#phi (deg) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_phiMeas[i] = new TH1D(Form("phiMeas_%s", Polarizations[i]), 
			Form(";#phi (deg) [Measured] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_PhiKin[i] = new TH1D(Form("PhiKin_%s", Polarizations[i]), 
			Form(";#Phi (deg) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_PhiMeas[i] = new TH1D(Form("PhiMeas_%s", Polarizations[i]), 
			Form(";#Phi (deg) [Measured] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_psiKin[i] = new TH1D(Form("psiKin_%s", Polarizations[i]), 
			Form(";#psi (deg) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_psiMeas[i] = new TH1D(Form("psiMeas_%s", Polarizations[i]), 
			Form(";#psi (deg) [Measured] #theta_{pol}=%s#circ", Polarizations[i]), 180, -180, 180);
		dHist_Rho0Pol_phikin_vs_Phikin[i] = new TH2D(Form("phikin_vs_Phikin_%s", Polarizations[i]), 
			Form(";#Phi (deg) [Kinfit];#phi (deg) [Kinfit] #theta_{pol}=%s#circ", Polarizations[i]), 90, -180., 180., 90, -180., 180.);
	}

	// Return to main directory
	gDirectory->cd("..");
	

	/************************************** USER INITIALIZATION: CUT THRESHOLDS *************************************/

	dMinKinFitCL = 1e-6;              // Minimum kinematic fit confidence level
	dMinBeamEnergy = 7.0;
	dMaxBeamEnergy = 11.4;
	dMaxNumUnusedTracks = 0;          // Maximum number of unused charged tracks
	
	ApplyFCALEnergyCorrections = false;       // Correct simulated FCAL energies based on track momentum
	ApplyFCALPlusMinusRandomization = false;  // Randomly swap the 'plus' or 'minus' label of the FCAL observables to enforce 
											 // the simulated E/p distributions to be identical between e+ and e- for TMVA training
	ApplyMLPClassification = true;           // Apply MLP classifier to get particle ID scores (turn OFF when producing training files)
	ApplyMLPResponseCut = true;

	// Initialize cut enable/disable flags - Set to true to apply the cut
	// Section 0: Preselection cut emulation
	ApplyPreselectionEoPCut = false;        // 0a: E/p > 0.4 preselection (off by default)

	// Section 1: Exclusivity cuts
	ApplyNoUnusedTracksCut = true;          // 1a: No unused charged tracks
	ApplyNoUnusedShowersCut = true;         // 1b: No unused neutral shower energy

	// Section 2: Forward detector cuts
	ApplyFCALEnergyNonZeroCut = true;       // 2a: FCAL E > 0 for both tracks. True for training. False for pion sub sample.
	ApplyTOFdEdxNonZeroCut = true;          // 2b: TOF dE/dx > 0 for both tracks

	// Section 3: Fiducial cuts (turn off for systematics studies)
	ApplyBeamEnergyCut = true;              // 3a: Coherent peak is 8.2-8.8 GeV. FFStudy: 7-11.4 GeV 
	ApplyMinThetaCut = true;                // 3b: Minimum theta > 1.5 deg
	ApplyMaxThetaCut = true;               // 3c: Maximum theta < 7.5 deg (OFF for rho0 and TMVA training)
	Apply2DThetaAcceptanceMatchCut = true; // 3d: 2D theta acceptance match (OFF for rho0 and TMVA training)
	ApplyMomentumRangeCut = true;          // 3e: Valid FCAL correction range (0.45-7.92 GeV)
	ApplyInvariantMassCut = true;          // 3f: Invariant mass window (700 MeV to 770 MeV for rho0)
	ApplyVertexZCut = true;                // 3g: Vertex Z position (52-78 cm)
	ApplyKinFitCLCut = true;               // 3h: Kinematic fit CL > 1e-6


	OnlyBestChiSqComboInFlat = true;             // Only save combo with best chiSq to flat tree

	// ======================================================================
	// Additional cuts for testing--off in any official analyses
	// ======================================================================
	ApplyMinEoverP1Cut = true;
	ApplyMaxEoverP1Cut = false;
	ApplyMinEoverP2Cut = false;
	ApplyMaxEoverP2Cut = false;
	ApplyMinFCALElasticityCut = true;
	ApplyMaxFCALElasticityCut = true;
	ApplyMaxFCALDOCACut = false;
	ApplyMaxTOFdEdxCut = false;
	
	// Initialize test cut threshold values
	dMinEoverP1 = 0.7;
	dMaxEoverP1 = 1.2;
	dMinEoverP2 = 0.7;
	dMaxEoverP2 = 1.2;
	dMinFCALElasticity = 0.8;
	dMaxFCALElasticity = 1.2;
	dMaxFCALDOCA = 2.0;              // cm
	dMaxTOFdEdx =  2.0 * 0.00263;    // MeV/cm (2x mean from e+e-)
	dMinMLPResponse = 0.8;           // Minimum MLP score to identify as electron/positron
	// ======================================================================
	// ======================================================================
	
	//	======================================================================
	// FCAL non-linear energy corrections
	// Random number generator and histogram for modulo 2
	RandomGen  = new TRandom();

	ep_FCAL_cor1 = new TF1("ep_FCAL_0pt45to3pt6", 
		"-0.0815097 + 0.113626*x -0.112878*x*x + 0.0455309*x*x*x -0.00615324*x*x*x*x",
		0.0,4.0);
	ep_FCAL_cor2 = new TF1("ep_FCAL_3pt6to8pt01",
		"140.568 - 189.004*x + 106.853*x*x -32.8906*x*x*x + 5.94911*x*x*x*x -0.63234*x*x*x*x*x +0.0365855*x*x*x*x*x*x -0.000889301*x*x*x*x*x*x*x",
		0.0,10.0);

	ep_FCAL_cor2data1 = new TF1("ep_FCAL_Correct0pt36to3pt6", 
		"-0.102153 + 0.167009*x -0.168846*x*x + 0.059494*x*x*x -0.00709742*x*x*x*x",
		0.0, 4.0);
	ep_FCAL_cor2data2 = new TF1("ep_FCAL_Correct3pt6andup", 
		"-15.5234 + 14.3966*x - 5.18514*x*x + 0.904002*x*x*x -0.0770312*x*x*x*x + 0.00257271*x*x*x*x*x"
		,0.0,10.0);

	em_FCAL_cor1 = new TF1("em_FCAL_0pt45to3pt6", 
		"-0.052032 + 0.00782447*x",
		0.0, 4.0);
	em_FCAL_cor2 = new TF1("em_FCAL_3pt6to7pt92",
		"18.2227 -22.007*x + 11.0161*x*x -2.90704*x*x*x + 0.423587*x*x*x*x -0.0322319*x*x*x*x*x + 0.000999988*x*x*x*x*x*x",
		0.0, 10.0);

	em_FCAL_cor2data1 = new TF1("em_FCAL_Correct0pt36to3pt6",
		"-0.0788966 + 0.191044*x - 0.283841*x*x + 0.153231*x*x*x -0.0371609*x*x*x*x + 0.00331943*x*x*x*x*x",
		0.0, 4.0);
	em_FCAL_cor2data2 = new TF1("em_FCAL_Correct3pt6andup",
		"-2.53871 + 2.08889*x -0.630479*x*x + 0.0775441*x*x*x -0.00343892*x*x*x*x", 
		0.0, 10.0);

	/******************************** TMVA READER INITIALIZATION *******************************/
	
	std::cout << "Initializing TMVA Readers for e+/e- PID..." << std::endl;
	
	// Initialize TMVA Reader for MLP classifier
	dTMVAReader_MLP = new TMVA::Reader("!Color:!Silent");
	std::cout << "TMVA MLP Reader created." << std::endl;
	
	// Add input variables to the reader (must match training variable names and order)
	dTMVAReader_MLP->AddVariable("EoverPkin_FCAL", &dTMVA_EoverPkin_FCAL);
	dTMVAReader_MLP->AddVariable("TrackFCAL_DOCA", &dTMVA_TrackFCAL_DOCA);
	dTMVAReader_MLP->AddVariable("E9E25_FCAL", &dTMVA_E9E25_FCAL);
	dTMVAReader_MLP->AddVariable("E1E9_FCAL", &dTMVA_E1E9_FCAL);
	dTMVAReader_MLP->AddVariable("SumU_FCAL", &dTMVA_SumU_FCAL);
	dTMVAReader_MLP->AddVariable("SumV_FCAL", &dTMVA_SumV_FCAL);
	dTMVAReader_MLP->AddVariable("UV_Asymmetry_FCAL", &dTMVA_UV_Asymmetry_FCAL);
	dTMVAReader_MLP->AddVariable("Saturation_FCAL", &dTMVA_Saturation_FCAL);
	std::cout << "TMVA MLP variables added." << std::endl;
	
	// Book the MVA method from XML weights file
	// Method name must match what's in the XML: \"MLP::MLP\"
	std::cout << "Booking MLP method from TMVAClassification_MLP.weights.xml..." << std::endl;
	dTMVAReader_MLP->BookMVA("MLP::MLP", "/work/halld/home/acschick/tmva-hpo/jobs/job_10000/dataset/weights/TMVAClassification_MLP.weights.xml");
	std::cout << "TMVA MLP Reader successfully initialized!" << std::endl;
	
	// Initialize TMVA Reader for BDT classifier
	dTMVAReader_BDT = new TMVA::Reader("!Color:!Silent");
	std::cout << "TMVA BDT Reader created." << std::endl;
	
	// Add input variables to the BDT reader (must use SAME variables as MLP)
	dTMVAReader_BDT->AddVariable("EoverPkin_FCAL", &dTMVA_EoverPkin_FCAL);
	dTMVAReader_BDT->AddVariable("TrackFCAL_DOCA", &dTMVA_TrackFCAL_DOCA);
	dTMVAReader_BDT->AddVariable("E9E25_FCAL", &dTMVA_E9E25_FCAL);
	dTMVAReader_BDT->AddVariable("E1E9_FCAL", &dTMVA_E1E9_FCAL);
	dTMVAReader_BDT->AddVariable("SumU_FCAL", &dTMVA_SumU_FCAL);
	dTMVAReader_BDT->AddVariable("SumV_FCAL", &dTMVA_SumV_FCAL);
	dTMVAReader_BDT->AddVariable("UV_Asymmetry_FCAL", &dTMVA_UV_Asymmetry_FCAL);
	dTMVAReader_BDT->AddVariable("Saturation_FCAL", &dTMVA_Saturation_FCAL);
	std::cout << "TMVA BDT variables added." << std::endl;
	
	// Book the BDT MVA method from XML weights file
	std::cout << "Booking BDT method from TMVAClassification_BDT.weights.xml..." << std::endl;
	dTMVAReader_BDT->BookMVA("BDT", "/work/halld/home/acschick/tmva-hpo/jobs/job_10001/dataset/weights/TMVAClassification_BDT.weights.xml");
	std::cout << "TMVA BDT Reader successfully initialized!" << std::endl;

	/************************** USER INITIALIZATION: FLAT TREE BRANCHES *************************/

	// ===== Branch creation =====
	// Event Information
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("RunNumber");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("EventNumber");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Combo");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("NumCombos");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("NumUnusedShowers");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Energy_UnusedShowers");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("NumUnusedTracks");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("kinfit_CL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("ChiSq_KinFit");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("NDF_KinFit");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("RFTime_KinFit");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("RFTime_Measured");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("AccWeight");

	// Beam Kinematic Fit X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_KinFit_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_KinFit_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_KinFit_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_KinFit_Z");

	// Beam Measured X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_Measured_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_X4_Measured_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_Measured_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Beam_P4_Measured_Z");

	// Positive Track Kinematic Fit X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_KinFit_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_KinFit_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_KinFit_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_KinFit_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P3_KinFit_Mag");

	// Positive Track Measured X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_Measured_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_X4_Measured_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_Measured_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P4_Measured_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_P3_Measured_Mag");

	// Negative Track Kinematic Fit X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_KinFit_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_KinFit_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_KinFit_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_KinFit_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P3_KinFit_Mag");
	// Negative Track Measured X4 and P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_Measured_T");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_X4_Measured_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_Measured_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_Measured_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_Measured_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P4_Measured_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_P3_Measured_Mag");

	// Kinematic Fit P4 of Recoil track (detected proton for pippim__B4)
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Recoil_P4_KinFit_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Recoil_P4_KinFit_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Recoil_P4_KinFit_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Recoil_P4_KinFit_Z");

	// gamma from pi0->ge+e- Kinematic Fit and measured P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_kin_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_kin_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_kin_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_kin_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_meas_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_meas_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_meas_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("g_p_meas_Z");

	// Decaying Pi0 Kinematic Fit and measured P4
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_kin_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_kin_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_kin_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_kin_Z");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_meas_E");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_meas_X");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_meas_Y");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("DecayingPi0_p_meas_Z");

	// Track Goodness of Fit
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_Beta_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_ChiSq_DCdEdx");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_NDF_DCdEdx");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_ChiSq_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_NDF_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_ChiSq_Tracking");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_NDF_Tracking");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_PIDFOM");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_ThrownIndex");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_TrackID");

	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_Beta_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_ChiSq_DCdEdx");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_NDF_DCdEdx");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_ChiSq_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_NDF_Timing");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_ChiSq_Tracking");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_NDF_Tracking");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_PIDFOM");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_ThrownIndex");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_TrackID");

	// FCAL
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("FCAL_Elasticity");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("FCAL_Asymmetry");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_Energy_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_EoverPkin_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_EoverPmeas_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_E1E9_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_E9E25_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_TrackFCAL_DOCA");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_SumU_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_SumV_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_Saturation_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_kin_res_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_meas_res_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_UV_Asymmetry_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_Energy_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_EoverPkin_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_EoverPmeas_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_E1E9_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_E9E25_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_TrackFCAL_DOCA");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_SumU_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_SumV_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_Saturation_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_kin_res_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_meas_res_FCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_UV_Asymmetry_FCAL");

	// BCAL
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_Energy_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_Energy_BCALPreshower");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_RMSTime_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_SigLong_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_SigTheta_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_SigTrans_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_TrackBCAL_DeltaPhi");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_TrackBCAL_DeltaZ");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_Energy_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_Energy_BCALPreshower");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_RMSTime_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_SigLong_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_SigTheta_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_SigTrans_BCAL");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_TrackBCAL_DeltaPhi");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_TrackBCAL_DeltaZ");

	// Other dEdx and Timing Information
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_dEdx_CDC");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_dEdx_CDC_integral");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_dEdx_FDC");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_dEdx_ST");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_dEdx_TOF");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_HitTime");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Negative_RFDeltaTVar");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_HitTime");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_RFDeltaTVar");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_dEdx_CDC");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_dEdx_CDC_integral");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_dEdx_FDC");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_dEdx_ST");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("Positive_dEdx_TOF");

	// Negative Track Model Response Place Holders
	// ---------------------------------------------
	// TMVA requires the structure of the ROOT tree used in training
	// to be the same as that used in analysis/TMVAClassificationApplication.C
	// We apply the models to the negative track first, and then to the positive,
	// so when we train the positive track model, we have to anticipate the structure
	// of the final tree used in analysis.
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("MLP_Response_Minus");
	dFlatTreeInterface->Create_Branch_Fundamental<Float_t>("BDT_Response_Minus");

	/************************************* ADVANCED EXAMPLE: CHOOSE BRANCHES TO READ ************************************/

	//TO SAVE PROCESSING TIME
		//If you know you don't need all of the branches/data, but just a subset of it, you can speed things up
		//By default, for each event, the data is retrieved for all branches
		//If you know you only need data for some branches, you can skip grabbing data from the branches you don't need
		//Do this by doing something similar to the commented code below

	//dTreeInterface->Clear_GetEntryBranches(); //now get none
	//dTreeInterface->Register_GetEntryBranch("Proton__P4"); //manually set the branches you want
}

Bool_t DSelector_2eMissingProton::Process(Long64_t locEntry)
{
	// The Process() function is called for each entry in the tree. The entry argument
	// specifies which entry in the currently loaded tree is to be processed.
	//
	// This function should contain the "body" of the analysis. It can contain
	// simple or elaborate selection criteria, run algorithms on the data
	// of the event and typically fill histograms.
	//
	// The processing can be stopped by calling Abort().
	// Use fStatus to set the return value of TTree::Process().
	// The return value is currently not used.

	//CALL THIS FIRST
	DSelector::Process(locEntry); //Gets the data from the tree for the entry
	//cout << "RUN " << Get_RunNumber() << ", EVENT " << Get_EventNumber() << endl;


	/******************************************** GET POLARIZATION ORIENTATION ******************************************/

	//Only if the run number changes
	//RCDB environment must be setup in order for this to work! (Will return false otherwise)
	UInt_t locRunNumber = Get_RunNumber();
	if(locRunNumber != dPreviousRunNumber)
	{
		dIsPolarizedFlag = dAnalysisUtilities.Get_IsPolarizedBeam(locRunNumber, dIsPARAFlag);
		dAnalysisUtilities.Get_PolarizationAngle(locRunNumber, dPolarizationAngle);
		dPreviousRunNumber = locRunNumber;
		//cout << "Run " << locRunNumber << ": IsPolarized = " << dIsPolarizedFlag << ", IsPARA = " << dIsPARAFlag
		//     << ", PolarizationAngle = " << dPolarizationAngle << " deg" << endl;
	}

	/********************************************* SETUP UNIQUENESS TRACKING ********************************************/

	//ANALYSIS ACTIONS: Reset uniqueness tracking for each action
	//For any actions that you are executing manually, be sure to call Reset_NewEvent() on them here
	Reset_Actions_NewEvent();
	//dAnalyzeCutActions->Reset_NewEvent(); // manual action, must call Reset_NewEvent()

	//PREVENT-DOUBLE COUNTING WHEN HISTOGRAMMING
	//Sometimes, some content is the exact same between one combo and the next
	//e.g. maybe two combos have different beam particles, but the same data for the final-state
	//When histogramming, you don't want to double-count when this happens: artificially inflates your signal (or background)
	//So, for each quantity you histogram, keep track of what particles you used (for a given combo)
	//Then for each combo, just compare to what you used before, and make sure it's unique

	//EXAMPLE 1: Particle-specific info:
	set<Int_t> locUsedSoFar_BeamEnergy; //Int_t: Unique ID for beam particles. set: easy to use, fast to search
	set<Int_t> locUsedSoFar_Positron, locUsedSoFar_Electron;

	//EXAMPLE 2: Combo-specific info:
	//In general: Could have multiple particles with the same PID: Use a set of Int_t's
	//In general: Multiple PIDs, so multiple sets: Contain within a map
	//Multiple combos: Contain maps within a set (easier, faster to search)
	set<map<Particle_t, set<Int_t> > > locUsedSoFar_2e, locUsedSoFar_Angles;

	//INSERT USER ANALYSIS UNIQUENESS TRACKING HERE

	//EXAMPLE 0: Event-specific info:
	Bool_t locUsedSoFar_BestChiSq = false; // Flag to ensure only best χ² combo fills flat tree


	/******************************************* LOOP OVER THROWN DATA (OPTIONAL) ***************************************/
	/*                                                                
        double locEbeam_Thrown = 0;
        if(dThrownBeam != NULL)
	  		locEbeam_Thrown = dThrownBeam->Get_P4().T();
        	TLorentzVector locBeamP4_Thrown = dThrownBeam->Get_P4();
        	//Loop over throwns                         
        	TLorentzVector locProton_Thrown;
        	TLorentzVector locPositronP4_Thrown;
        	TLorentzVector locElectronP4_Thrown;
  

        for(UInt_t loc_i = 0; loc_i < Get_NumThrown(); ++loc_i){
			//Set branch array indices corresponding to this particle                                                                                                         
			dThrownWrapper->Set_ArrayIndex(loc_i);
			Particle_t thrown_pid = dThrownWrapper->Get_PID();
			cout << " loc_i=" << loc_i << " thrown_pid=" << thrown_pid << endl;
			TLorentzVector locP4_Thrown = dThrownWrapper->Get_P4();
			if (loc_i == 2) locProton_Thrown = locP4_Thrown;    // Assume recoil is index 3                                                                                   
			if (loc_i == 1) locElectronP4_Thrown = locP4_Thrown;                                                                                                              
			if (loc_i == 0) locPositronP4_Thrown = locP4_Thrown;
	  	}
		cout << "After thrown PID assignment " << endl;
		TLorentzVector loc2eP4_Thrown = locPositronP4_Thrown + locElectronP4_Thrown;

        double tgen = (dThrownBeam->Get_P4() - locPositronP4_Thrown - locElectronP4_Thrown).M2();    // use beam and 2pi momenta                                                          

        double eplusE_Thrown = locPositronP4_Thrown.E();
        double eminusE_Thrown = locElectronP4_Thrown.E();
        double E2e_Thrown = eplusE_Thrown + eminusE_Thrown;

        TLorentzVector locMissingP4_Thrown = dThrownBeam->Get_P4() + dTargetP4;
		locMissingP4_Thrown -= locPositronP4_Thrown + locElectronP4_Thrown;

		double JWeight1_Thrown = locPositronP4_Thrown.X()*locPositronP4_Thrown.X() + locPositronP4_Thrown.Y()*locPositronP4_Thrown.Y() + 	ElectronMass*ElectronMass;
        double JWeight2_Thrown = locElectronP4_Thrown.X()*locElectronP4_Thrown.X() + locElectronP4_Thrown.Y()*locElectronP4_Thrown.Y() + ElectronMass*ElectronMass;
		double Jx_Thrown = locPositronP4_Thrown.X()/JWeight1_Thrown + locElectronP4_Thrown.X()/JWeight2_Thrown;
		double Jy_Thrown = locPositronP4_Thrown.Y()/JWeight1_Thrown + locElectronP4_Thrown.Y()/JWeight2_Thrown;        
		double Jphi_Thrown = atan2(Jy_Thrown,Jx_Thrown)*180/PI;

       	double Elasticity_Thrown = (locBeamP4_Thrown.E() - locElectronP4_Thrown.E() - locPositronP4_Thrown.E())/locBeamP4_Thrown.E();
	
	*/

	/************************************************* LOOP OVER COMBOS *************************************************/

	// Vector to store combo information
	std::vector<std::pair<UInt_t, Double_t>> loc_combos;

	// Pre-loop to gather kinfit ComboIndex-chiSq pairing and sort by chiSq value ascendingly
	for(UInt_t loc_i = 0; loc_i < Get_NumCombos(); ++loc_i)
	{
		dComboWrapper->Set_ComboIndex(loc_i);
		Double_t locChiSq = dComboWrapper->Get_ChiSq_KinFit("");
		loc_combos.push_back(std::make_pair(loc_i, locChiSq));
	}
	// Sort the combos by ChiSq
	std::sort(loc_combos.begin(), loc_combos.end(), [](const std::pair<UInt_t, Double_t>& a, const std::pair<UInt_t, Double_t>& b) {
		return a.second < b.second;
	});

	//Loop over combos
	for(const auto& loc_combo : loc_combos){
		UInt_t loc_i = loc_combo.first;
		//Set branch array indices for combo and all combo particles
		dComboWrapper->Set_ComboIndex(loc_i);
		
		// Is used to indicate when combos have been cut
		if(dComboWrapper->Get_IsComboCut()) // Is false when tree originally created
		continue; // Combo has been cut previously

		/********************************************** GET PARTICLE INDICES *********************************************/
		//Used for tracking uniqueness when filling histograms, and for determining unused particles
		
		//Step 0
		Int_t locBeamID = dComboBeamWrapper->Get_BeamID();
		Int_t locPositronTrackID = dPositiveWrapper->Get_TrackID();
		Int_t locElectronTrackID = dNegativeWrapper->Get_TrackID();

		/*********************************************** GET FOUR-MOMENTUM **********************************************/

		// Get P4's: //is kinfit if kinfit performed, else is measured
		//dTargetP4 is target p4
		//Step 0
		cout << "DEBUG 1: Getting kinfit P4s" << endl;
		TLorentzVector locBeamP4 = dComboBeamWrapper->Get_P4();
		TLorentzVector locPositiveP4 = dPositiveWrapper->Get_P4();
		TLorentzVector locNegativeP4 = dNegativeWrapper->Get_P4();
		TLorentzVector locRecoilP4 = dRecoilWrapper->Get_P4();
		cout << "DEBUG 2: Got kinfit P4s" << endl;

		// Get Measured P4's:
		//Step 0
		cout << "DEBUG 3: Getting measured P4s" << endl;
		TLorentzVector locBeamP4_Measured = dComboBeamWrapper->Get_P4_Measured();
		TLorentzVector locPositiveP4_Measured = dPositiveWrapper->Get_P4_Measured();
		TLorentzVector locNegativeP4_Measured = dNegativeWrapper->Get_P4_Measured();
		// Note: Recoil proton is missing - no measured P4 available
		cout << "DEBUG 4: Got measured P4s" << endl;

	  
	  	/********************************************* GET COMBO RF TIMING INFO *****************************************/
		TLorentzVector locBeam_X4 = dComboBeamWrapper->Get_X4();
		TLorentzVector locBeamX4_Measured = dComboBeamWrapper->Get_X4_Measured();
		Double_t locBunchPeriod = dAnalysisUtilities.Get_BeamBunchPeriod(Get_RunNumber());
		Double_t locDeltaT_RF = dAnalysisUtilities.Get_DeltaT_RF(Get_RunNumber(), locBeamX4_Measured, dComboWrapper);
		Int_t locRelBeamBucket = dAnalysisUtilities.Get_RelativeBeamBucket(Get_RunNumber(), locBeamX4_Measured, dComboWrapper); // 0 for in-time events, non-zero integer for out-of-time photons
		Int_t locNumOutOfTimeBunchesInTree = 2; //YOU need to specify this number
			//Number of out-of-time beam bunches in tree (on a single side, so that total number out-of-time bunches accepted is 2 times this number for left + right bunches)

		// Bool_t locSkipNearestOutOfTimeBunch = true; // True: skip events from nearest out-of-time bunch on either side (recommended).
		// Int_t locNumOutOfTimeBunchesToUse = locSkipNearestOutOfTimeBunch ? locNumOutOfTimeBunchesInTree-1:locNumOutOfTimeBunchesInTree;
		// Double_t locAccidentalScalingFactor = dAnalysisUtilities.Get_AccidentalScalingFactor(Get_RunNumber(), locBeamP4.E(), dIsMC); // Ideal value would be 1, but deviations require added factor, which is different for data and MC.
		// Double_t locAccidentalScalingFactorError = dAnalysisUtilities.Get_AccidentalScalingFactorError(Get_RunNumber(), locBeamP4.E()); // Ideal value would be 1, but deviations observed, need added factor.
		// Double_t locHistAccidWeightFactor = locRelBeamBucket==0 ? 1 : -locAccidentalScalingFactor/(2*locNumOutOfTimeBunchesToUse) ; // Weight by 1 for in-time events, ScalingFactor*(1/NBunches) for out-of-time
		// if(locSkipNearestOutOfTimeBunch && abs(locRelBeamBucket)==1) { // Skip nearest out-of-time bunch: tails of in-time distribution also leak in
		// 	dComboWrapper->Set_IsComboCut(true);
		// 	continue;
		// }

		/******************************************** Accidental Subtraction ***************************************************/
		double AccWeight = 0.;
		double locRFTime = dComboWrapper->Get_RFTime_Measured();
		// time difference between tagger and RF (corrected for production vertex position relative to target center)
		double locBeamDeltaT = locBeamX4_Measured.T() - (locRFTime + (locBeamX4_Measured.Z() - dTargetCenter.Z())/29.9792458);
		if(fabs(locBeamDeltaT) < 0.5*4.008) { // prompt signal recieves a weight of 1
			AccWeight = 1.;
	    }
	    else { // accidentals recieve a weight of 1/(# RF bunches included in TTree) which is 4 in this case
			AccWeight = -1./4.; //need to have the total number of buckets. Make the histogram wider to find out how many
	    }

		/*
		 cout << " Tagger Accidentals: dTargetCenter=" << dTargetCenter.Z()
		 << " locRFTime=" << locRFTime
		 << " locBeamDeltaT=" << locBeamDeltaT
		 << " AccWeight=" << AccWeight << endl;

		 cout << " locBeam_X4_Measured="; locBeam_X4_Measured.Print();
		*/

		dHist_TaggerAccidentals->Fill(locBeamDeltaT);
		//cout << "accidental subtraction" << "\n";



	  	/********************************************* COMBINE FOUR-MOMENTUM ********************************************/

		// Combine 4-vectors
		TLorentzVector locMissingP4_Measured = locBeamP4_Measured + dTargetP4;
		locMissingP4_Measured -= locPositiveP4_Measured + locNegativeP4_Measured;
		
		TLorentzVector loc2TrackP4 = locPositiveP4 + locNegativeP4;
		TLorentzVector loc2TrackP4_Measured = locPositiveP4_Measured + locNegativeP4_Measured;

		double M2TrackKin = loc2TrackP4.M();  // Generic name for 2-track invariant mass

		Float_t MLP_responsePIM = 1;
		Float_t BDT_responsePIM = 1;

		double positive_phi = locPositiveP4.Phi();
		double negative_phi = locNegativeP4.Phi();


		//Missing Mass Squared
		double locMissingMassSquared_Measured = locMissingP4_Measured.M2();
				
		// FCAL variables
		double FCAL_Energy_plus = dPositiveWrapper->Get_Energy_FCAL();
		double FCAL_Energy_minus = dNegativeWrapper->Get_Energy_FCAL();
		double FCAL_Elasticity = (FCAL_Energy_plus + FCAL_Energy_minus)/locBeamP4.E();
		double TrackFCAL_DOCA_plus = dPositiveWrapper->Get_TrackFCAL_DOCA();
		double TrackFCAL_DOCA_minus = dNegativeWrapper->Get_TrackFCAL_DOCA();
		double FCAL_E1E9_plus = dPositiveWrapper->Get_E1E9_FCAL();
		double FCAL_E1E9_minus = dNegativeWrapper->Get_E1E9_FCAL();
		double FCAL_E9E25_plus = dPositiveWrapper->Get_E9E25_FCAL();
		double FCAL_E9E25_minus = dNegativeWrapper->Get_E9E25_FCAL();
		double FCAL_kin_res_plus = (FCAL_Energy_plus - locPositiveP4.E())/locPositiveP4.E();
		double FCAL_kin_res_minus = (FCAL_Energy_minus - locNegativeP4.E())/locNegativeP4.E();
		double FCAL_meas_res_plus = (FCAL_Energy_plus - locPositiveP4_Measured.E())/locPositiveP4_Measured.E();
		double FCAL_meas_res_minus = (FCAL_Energy_minus - locNegativeP4_Measured.E())/locNegativeP4_Measured.E();
		// Note: FCAL_EoverP variables declared later after swap, these are just for histograms
		double FCAL_EoverPkin_plus_hist = FCAL_Energy_plus/locPositiveP4.Vect().Mag();
		double FCAL_EoverPkin_minus_hist = FCAL_Energy_minus/locNegativeP4.Vect().Mag();
		double FCAL_EoverPmeas_plus_hist = FCAL_Energy_plus/locPositiveP4_Measured.Vect().Mag();
		double FCAL_EoverPmeas_minus_hist = FCAL_Energy_minus/locNegativeP4_Measured.Vect().Mag();
		double FCAL_Emax_plus = FCAL_Energy_plus * FCAL_E9E25_plus * FCAL_E1E9_plus;
        double FCAL_Emax_minus = FCAL_Energy_minus * FCAL_E9E25_minus * FCAL_E1E9_minus;
        double FCAL_Asymmetry = fabs((FCAL_Energy_plus - FCAL_Energy_minus)/(FCAL_Energy_plus + FCAL_Energy_minus));
		
		// Geometric asymmetry from eq. 2.4: A_uv = |σ²_u - σ²_v| / |σ²_u + σ²_v|
		// SumU and SumV correspond to the second moments σ²_u and σ²_v
		double SumU_plus = dPositiveWrapper->Get_SumU_FCAL();
		double SumV_plus = dPositiveWrapper->Get_SumV_FCAL();
		double SumU_minus = dNegativeWrapper->Get_SumU_FCAL();
		double SumV_minus = dNegativeWrapper->Get_SumV_FCAL();
		double FCAL_UV_Asymmetry_plus = fabs(SumU_plus - SumV_plus) / fabs(SumU_plus + SumV_plus);
		double FCAL_UV_Asymmetry_minus = fabs(SumU_minus - SumV_minus) / fabs(SumU_minus + SumV_minus);

		cout << "DEBUG 4.5: Got FCAL variables" << endl;

		// dE/dx variables
		double TOF_dEdx_plus = dPositiveWrapper->Get_dEdx_TOF();
		double TOF_dEdx_minus = dNegativeWrapper->Get_dEdx_TOF();
		double FDC_dEdx_plus = dPositiveWrapper->Get_dEdx_FDC();
		double FDC_dEdx_minus = dNegativeWrapper->Get_dEdx_FDC();

		// ============================================================================
		// COLLECT ALL POSITIVE/NEGATIVE TRACK PROPERTIES INTO LOCAL VARIABLES
		// (These will be swapped if ApplyFCALPlusMinusRandomization is true)
		// ============================================================================
		
		// Kinfit and Measured 4-vectors (already declared above)
		// locPositiveP4, locNegativeP4, locPositiveP4_Measured, locNegativeP4_Measured
		
		// Kinfit X4
		TLorentzVector Positive_X4_KinFit = dPositiveWrapper->Get_X4();
		TLorentzVector Negative_X4_KinFit = dNegativeWrapper->Get_X4();
		
		// Measured X4
		TLorentzVector Positive_X4_Measured = dPositiveWrapper->Get_X4_Measured();
		TLorentzVector Negative_X4_Measured = dNegativeWrapper->Get_X4_Measured();
		
		// Derived momentum magnitudes (calculated once, swapped if needed)
		Double_t Positive_P3_KinFit_Mag = locPositiveP4.Vect().Mag();
		Double_t Negative_P3_KinFit_Mag = locNegativeP4.Vect().Mag();
		Double_t Positive_P3_Measured_Mag = locPositiveP4_Measured.Vect().Mag();
		Double_t Negative_P3_Measured_Mag = locNegativeP4_Measured.Vect().Mag();
		
		// Derived FCAL E/p ratios (calculated once, will be recalculated after corrections/swap)
		Double_t FCAL_EoverPkin_plus = FCAL_Energy_plus/locPositiveP4.Vect().Mag();
		Double_t FCAL_EoverPkin_minus = FCAL_Energy_minus/locNegativeP4.Vect().Mag();
		Double_t FCAL_EoverPmeas_plus = FCAL_Energy_plus/locPositiveP4_Measured.Vect().Mag();
		Double_t FCAL_EoverPmeas_minus = FCAL_Energy_minus/locNegativeP4_Measured.Vect().Mag();
		
		// Additional dEdx (CDC, ST)
		Double_t CDC_dEdx_plus = dPositiveWrapper->Get_dEdx_CDC();
		Double_t CDC_dEdx_minus = dNegativeWrapper->Get_dEdx_CDC();
		Double_t CDC_dEdx_integral_plus = dPositiveWrapper->Get_dEdx_CDC_integral();
		Double_t CDC_dEdx_integral_minus = dNegativeWrapper->Get_dEdx_CDC_integral();
		Double_t ST_dEdx_plus = dPositiveWrapper->Get_dEdx_ST();
		Double_t ST_dEdx_minus = dNegativeWrapper->Get_dEdx_ST();
		
		// Timing and Chi-Squared quantities
		Double_t Beta_Timing_plus = dPositiveWrapper->Get_Beta_Timing();
		Double_t Beta_Timing_minus = dNegativeWrapper->Get_Beta_Timing();
		Double_t ChiSq_DCdEdx_plus = dPositiveWrapper->Get_ChiSq_DCdEdx();
		Double_t ChiSq_DCdEdx_minus = dNegativeWrapper->Get_ChiSq_DCdEdx();
		Double_t NDF_DCdEdx_plus = dPositiveWrapper->Get_NDF_DCdEdx();
		Double_t NDF_DCdEdx_minus = dNegativeWrapper->Get_NDF_DCdEdx();
		Double_t ChiSq_Timing_plus = dPositiveWrapper->Get_ChiSq_Timing();
		Double_t ChiSq_Timing_minus = dNegativeWrapper->Get_ChiSq_Timing();
		Double_t NDF_Timing_plus = dPositiveWrapper->Get_NDF_Timing();
		Double_t NDF_Timing_minus = dNegativeWrapper->Get_NDF_Timing();
		Double_t ChiSq_Tracking_plus = dPositiveWrapper->Get_ChiSq_Tracking();
		Double_t ChiSq_Tracking_minus = dNegativeWrapper->Get_ChiSq_Tracking();
		Double_t NDF_Tracking_plus = dPositiveWrapper->Get_NDF_Tracking();
		Double_t NDF_Tracking_minus = dNegativeWrapper->Get_NDF_Tracking();
		Double_t PIDFOM_plus = dPositiveWrapper->Get_PIDFOM();
		Double_t PIDFOM_minus = dNegativeWrapper->Get_PIDFOM();
		Int_t ThrownIndex_plus = dPositiveWrapper->Get_ThrownIndex();
		Int_t ThrownIndex_minus = dNegativeWrapper->Get_ThrownIndex();
		Double_t HitTime_plus = dPositiveWrapper->Get_HitTime();
		Double_t HitTime_minus = dNegativeWrapper->Get_HitTime();
		Double_t RFDeltaTVar_plus = dPositiveWrapper->Get_RFDeltaTVar();
		Double_t RFDeltaTVar_minus = dNegativeWrapper->Get_RFDeltaTVar();
		
		// BCAL quantities
		Double_t Energy_BCAL_plus = dPositiveWrapper->Get_Energy_BCAL();
		Double_t Energy_BCAL_minus = dNegativeWrapper->Get_Energy_BCAL();
		Double_t Energy_BCALPreshower_plus = dPositiveWrapper->Get_Energy_BCALPreshower();
		Double_t Energy_BCALPreshower_minus = dNegativeWrapper->Get_Energy_BCALPreshower();
		Double_t RMSTime_BCAL_plus = dPositiveWrapper->Get_RMSTime_BCAL();
		Double_t RMSTime_BCAL_minus = dNegativeWrapper->Get_RMSTime_BCAL();
		Double_t SigLong_BCAL_plus = dPositiveWrapper->Get_SigLong_BCAL();
		Double_t SigLong_BCAL_minus = dNegativeWrapper->Get_SigLong_BCAL();
		Double_t SigTheta_BCAL_plus = dPositiveWrapper->Get_SigTheta_BCAL();
		Double_t SigTheta_BCAL_minus = dNegativeWrapper->Get_SigTheta_BCAL();
		Double_t SigTrans_BCAL_plus = dPositiveWrapper->Get_SigTrans_BCAL();
		Double_t SigTrans_BCAL_minus = dNegativeWrapper->Get_SigTrans_BCAL();
		Double_t TrackBCAL_DeltaPhi_plus = dPositiveWrapper->Get_TrackBCAL_DeltaPhi();
		Double_t TrackBCAL_DeltaPhi_minus = dNegativeWrapper->Get_TrackBCAL_DeltaPhi();
		Double_t TrackBCAL_DeltaZ_plus = dPositiveWrapper->Get_TrackBCAL_DeltaZ();
		Double_t TrackBCAL_DeltaZ_minus = dNegativeWrapper->Get_TrackBCAL_DeltaZ();
		
		// Track IDs (already declared as locPositronTrackID, locElectronTrackID - will rename)
		Int_t Positive_TrackID = locPositronTrackID;
		Int_t Negative_TrackID = locElectronTrackID;

		Int_t NumUnusedTracks = dComboWrapper->Get_NumUnusedTracks();
                double Energy_UnusedShowers = dComboWrapper->Get_Energy_UnusedShowers();
		cout << "DEBUG 4.7: Finished variable declarations, starting cuts" << endl;


		// Declare variables for MLP responses (will be filled conditionally)
        Float_t mlp_response_minus = -999.0;  // Use sentinel value as default
        Float_t mlp_response_plus = -999.0;

        // Only apply MLP classification if enabled AND reader is initialized
        if(ApplyMLPClassification && dTMVAReader_MLP != NULL) {
            // ==================== Evaluate ELECTRON (negative track) ====================
            dTMVA_EoverPkin_FCAL = (Float_t)FCAL_EoverPkin_minus;
            dTMVA_TrackFCAL_DOCA = (Float_t)TrackFCAL_DOCA_minus;
            dTMVA_E9E25_FCAL = (Float_t)FCAL_E9E25_minus;
            dTMVA_E1E9_FCAL = (Float_t)FCAL_E1E9_minus;
            dTMVA_SumU_FCAL = (Float_t)SumU_minus;
            dTMVA_SumV_FCAL = (Float_t)SumV_minus;
            dTMVA_UV_Asymmetry_FCAL = (Float_t)FCAL_UV_Asymmetry_minus;
            dTMVA_Saturation_FCAL = (Float_t)Saturation_minus;
            
            mlp_response_minus = (Float_t)dTMVAReader_MLP->EvaluateMVA("MLP::MLP");
            
            // ==================== Now evaluate POSITRON (positive track) ====================
            dTMVA_EoverPkin_FCAL = (Float_t)FCAL_EoverPkin_plus;
            dTMVA_TrackFCAL_DOCA = (Float_t)TrackFCAL_DOCA_plus;
            dTMVA_E9E25_FCAL = (Float_t)FCAL_E9E25_plus;
            dTMVA_E1E9_FCAL = (Float_t)FCAL_E1E9_plus;
            dTMVA_SumU_FCAL = (Float_t)SumU_plus;
            dTMVA_SumV_FCAL = (Float_t)SumV_plus;
            dTMVA_UV_Asymmetry_FCAL = (Float_t)FCAL_UV_Asymmetry_plus;
            dTMVA_Saturation_FCAL = (Float_t)Saturation_plus;
            
            mlp_response_plus = (Float_t)dTMVAReader_MLP->EvaluateMVA("MLP::MLP");
        }


		/********************************************** SECTION 3: OFFICIAL ANALYSIS CUTS **********************************************/
		// These are the standard cuts used in the official analysis.

		// -------------------------------------------------------------
		// 0.) Preselection cut (optional, normally off):
		// -------------------------------------------------------------
		// Measured E/p > 0.4 for both tracks
		if(ApplyPreselectionEoPCut) {
			if(FCAL_EoverPmeas_plus < 0.4 || FCAL_EoverPmeas_minus < 0.4) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// 1.) Two track exclusive (not counting missing proton) cuts:
		// -------------------------------------------------------------
		// (a) No unused charged tracks
		if(ApplyNoUnusedTracksCut) {
			if(NumUnusedTracks > dMaxNumUnusedTracks) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (b) No unused neutral shower energy
		if(ApplyNoUnusedShowersCut) {
			if(Energy_UnusedShowers > 0.0){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// -----------------------------------------------------
		// 2.) Ensure tracks go into only the forward detectors:
		// -----------------------------------------------------
		// (a) FCAL E_1 and FCAL E_2 are greater than zero
		if(ApplyFCALEnergyNonZeroCut) {
			if (FCAL_Energy_plus <= 0.0 || FCAL_Energy_minus <= 0.0){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (b) TOF dE/dx > 0 for both tracks
		if(ApplyTOFdEdxNonZeroCut) {
			if(TOF_dEdx_plus <= 0.0 || TOF_dEdx_minus <= 0.0){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// ------------------------------------------------------------------
		// 3.) Fiducial Cuts:
		// Turn these off if preparing a file to do systematics studies with.
		// ------------------------------------------------------------------

		// (a) Coherent peak cut (8.2 GeV < E_gamma < 8.8 GeV)
		if(ApplyBeamEnergyCut) {
			if( locBeamP4.E() < dMinBeamEnergy || locBeamP4.E() > dMaxBeamEnergy){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (b) Minimum theta of tracks cut
		if(ApplyMinThetaCut) {
			if ( locPositiveP4.Theta()*TMath::RadToDeg() < 1.5 || locNegativeP4.Theta()*TMath::RadToDeg() < 1.5){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (c) Maximum theta of tracks cut
		// DO NOT APPLY maximum theta of tracks cut for Rho0 file. It kills your yield.
		if(ApplyMaxThetaCut) {
			if ( locPositiveP4.Theta()*TMath::RadToDeg() > 7.5 || locNegativeP4.Theta()*TMath::RadToDeg() > 7.5){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (d) Simulation/data acceptance match curve: theta_2 > 14 * (theta_1/(theta_1^(5/2) + 0.2)) deg
		// DO NOT APPLY this cut for Rho0 file--again, will kill your yield.
		if(Apply2DThetaAcceptanceMatchCut) {
			// Implementation would go here if needed
			// Currently just a placeholder flag
		}

		// (e) Valid range of momentum dependent non-linear FCAL corrections in the simulation, 0.45 GeV < |p| < 7.92 GeV
		if(ApplyMomentumRangeCut) { 
			if ( locPositiveP4.Vect().Mag() < 0.45 || locPositiveP4.Vect().Mag() > 7.92){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
			if ( locNegativeP4.Vect().Mag() < 0.45 || locNegativeP4.Vect().Mag()> 7.92){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// (f) Invariant mass cut (0.25 GeV is where acceptance kicks in. 0.621 to avoid the rho0 peak)
		if(ApplyInvariantMassCut) {
			if(M2TrackKin < 0.25 || M2TrackKin > 0.621){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (g) Vertex position: 52 cm < Z < 78 cm
		if(ApplyVertexZCut) {
			if(locBeamX4_Measured.Z() < 52 || locBeamX4_Measured.Z() > 78){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
		// (h) Minimum chi squared confidence level: 1E-6
		if(ApplyKinFitCLCut) {
			if(dComboWrapper->Get_ConfidenceLevel_KinFit("") <= dMinKinFitCL) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		/****************************************** SECTION 4: TEST/DEVELOPMENT CUTS (NOT IN OFFICIAL ANALYSIS) **********************************************/
		// These cuts are provided for user convenience to quickly test various particle ID criteria.
		// They are typically disabled (set to false in Init()) and not part of the standard analysis.
		// Easy to copy/paste this entire section into other DSelectors for development work.

		// (a) Minimum E/P cut: FCAL energy divided by kinematic fit momentum
		if(ApplyMinEoverP1Cut) {
			// Use already-calculated E/P values
			if(FCAL_EoverPkin_plus < dMinEoverP1) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		if(ApplyMinEoverP2Cut) {
			if(FCAL_EoverPkin_minus < dMinEoverP2) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// (b) Maximum E/P cut: FCAL energy divided by kinematic fit momentum
		if(ApplyMaxEoverP1Cut) {
			if(FCAL_EoverPkin_plus > dMaxEoverP1) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		if(ApplyMaxEoverP2Cut) {
			if(FCAL_EoverPkin_minus > dMaxEoverP2) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}


		// (c) Minimum FCAL elasticity cut: (E_plus + E_minus) / E_beam
		if(ApplyMinFCALElasticityCut) {
			// Use already-calculated FCAL_Elasticity
			if(FCAL_Elasticity < dMinFCALElasticity) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// (d) Maximum FCAL elasticity cut: (E_plus + E_minus) / E_beam
		if(ApplyMaxFCALElasticityCut) {
			// Use already-calculated FCAL_Elasticity
			if(FCAL_Elasticity > dMaxFCALElasticity) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// (e) Maximum FCAL DOCA cut: distance of closest approach to shower center
		if(ApplyMaxFCALDOCACut) {
			// Use already-calculated DOCA values
			if(TrackFCAL_DOCA_plus > dMaxFCALDOCA || TrackFCAL_DOCA_minus > dMaxFCALDOCA) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// (f) Maximum TOF dE/dx cut
		if(ApplyMaxTOFdEdxCut) {
			// Use already-calculated TOF dE/dx values
			if(TOF_dEdx_plus > dMaxTOFdEdx || TOF_dEdx_minus > dMaxTOFdEdx) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}

		// Fill before MLP cuts
		if(ApplyMLPClassification){
			dHist_MLPResponsePlus_vs_MLPResponseMinus->Fill(mlp_response_minus, mlp_response_plus);
			dHist_MLPResponse_minus->Fill(mlp_response_minus);
			dHist_MLPResponse_plus->Fill(mlp_response_plus);
		}
		// (g) MLP Response Cuts
		if(ApplyMLPResponseCut) {
			if(mlp_response_plus < dMinMLPResponse || mlp_response_minus < dMinMLPResponse) {
				dComboWrapper->Set_IsComboCut(true);
				continue;
			}
		}
	// -------------------------------------------------------------
	//  FCAL ENERGY CORRECTIONS
		cout << "DEBUG 4.8: Starting FCAL corrections, flag=" << ApplyFCALEnergyCorrections << endl;
	if(ApplyFCALEnergyCorrections == true){
		cout << "DEBUG 4.8.1: Inside if block, about to call ApplyFCALCorrections" << endl;
		// Apply corrections to FCAL energies based on track momentum
		ApplyFCALCorrections(locPositiveP4.Vect().Mag(), locNegativeP4.Vect().Mag(),
		                     FCAL_Energy_plus, FCAL_Energy_minus);
		cout << "DEBUG 4.8.2: Returned from ApplyFCALCorrections" << endl;
          }
		cout << "DEBUG 4.9: Finished FCAL corrections, starting swap check" << endl;
		// -------------------------------------------------------------
		//  CHARGE LABEL RANDOMIZATION (SWAP POSITIVE/NEGATIVE)
		// -------------------------------------------------------------
		cout << "DEBUG 4.9.1: Checking randomization flag=" << ApplyFCALPlusMinusRandomization << endl;
		if(ApplyFCALPlusMinusRandomization == true){
			int gen = int((1e8)*RandomGen->Rndm(1))%2;
			// dHist_randMOD2->Fill(gen);  // Histogram not created - commented out
			cout << "DEBUG 4.9.2: gen=" << gen << endl;
			
			if(gen == 1){
				cout << "DEBUG 4.9.3: About to call SwapChargeLabels" << endl;
				// Swap all positive/negative track labels
				SwapChargeLabels(
					locPositiveP4, locNegativeP4,
					locPositiveP4_Measured, locNegativeP4_Measured,
					Positive_X4_KinFit, Negative_X4_KinFit,
					Positive_X4_Measured, Negative_X4_Measured,
					Positive_TrackID, Negative_TrackID,
					Positive_P3_KinFit_Mag, Negative_P3_KinFit_Mag,
					Positive_P3_Measured_Mag, Negative_P3_Measured_Mag,
					FCAL_Energy_plus, FCAL_Energy_minus,
					FCAL_EoverPkin_plus, FCAL_EoverPkin_minus,
					FCAL_EoverPmeas_plus, FCAL_EoverPmeas_minus,
					TrackFCAL_DOCA_plus, TrackFCAL_DOCA_minus,
					FCAL_E1E9_plus, FCAL_E1E9_minus,
					FCAL_E9E25_plus, FCAL_E9E25_minus,
					SumU_plus, SumU_minus,
					SumV_plus, SumV_minus,
					TOF_dEdx_plus, TOF_dEdx_minus,
					FDC_dEdx_plus, FDC_dEdx_minus,
					CDC_dEdx_plus, CDC_dEdx_minus,
					CDC_dEdx_integral_plus, CDC_dEdx_integral_minus,
					ST_dEdx_plus, ST_dEdx_minus,
					Beta_Timing_plus, Beta_Timing_minus,
					ChiSq_DCdEdx_plus, ChiSq_DCdEdx_minus,
					NDF_DCdEdx_plus, NDF_DCdEdx_minus,
					ChiSq_Timing_plus, ChiSq_Timing_minus,
					NDF_Timing_plus, NDF_Timing_minus,
					ChiSq_Tracking_plus, ChiSq_Tracking_minus,
					NDF_Tracking_plus, NDF_Tracking_minus,
					PIDFOM_plus, PIDFOM_minus,
					ThrownIndex_plus, ThrownIndex_minus,
					HitTime_plus, HitTime_minus,
					RFDeltaTVar_plus, RFDeltaTVar_minus,
					Energy_BCAL_plus, Energy_BCAL_minus,
					Energy_BCALPreshower_plus, Energy_BCALPreshower_minus,
					RMSTime_BCAL_plus, RMSTime_BCAL_minus,
					SigLong_BCAL_plus, SigLong_BCAL_minus,
					SigTheta_BCAL_plus, SigTheta_BCAL_minus,
					SigTrans_BCAL_plus, SigTrans_BCAL_minus,
					TrackBCAL_DeltaPhi_plus, TrackBCAL_DeltaPhi_minus,
					TrackBCAL_DeltaZ_plus, TrackBCAL_DeltaZ_minus
				);
				cout << "DEBUG 4.9.4: Returned from SwapChargeLabels" << endl;
			}
		}
		cout << "DEBUG 4.9.5: Finished swap section, moving to BH polarization" << endl;






		/**************************** Bethe-Heitler Polarization ****************************/
		// Assume mass of electron for both tracks
		TLorentzVector PositronP4, ElectronP4, TwoTrackP4_eeMass, PositronP4_Measured, ElectronP4_Measured, TwoTrackP4_eeMass_Measured;
		PositronP4.SetXYZM(locPositiveP4.Px(), locPositiveP4.Py(), locPositiveP4.Pz(), 0.000511);
		ElectronP4.SetXYZM(locNegativeP4.Px(), locNegativeP4.Py(), locNegativeP4.Pz(), 0.000511);
		TwoTrackP4_eeMass = PositronP4 + ElectronP4;
		PositronP4_Measured.SetXYZM(locPositiveP4_Measured.Px(), locPositiveP4_Measured.Py(), locPositiveP4_Measured.Pz(), 0.000511);
		ElectronP4_Measured.SetXYZM(locNegativeP4_Measured.Px(), locNegativeP4_Measured.Py(), locNegativeP4_Measured.Pz(), 0.000511);
		TwoTrackP4_eeMass_Measured = PositronP4_Measured + ElectronP4_Measured;
		double qvec2 = (locBeamP4.Vect() - PositronP4.Vect() - ElectronP4.Vect()).Mag2();
		double qvec2_meas = (locBeamP4_Measured.Vect() - PositronP4_Measured.Vect() - ElectronP4_Measured.Vect()).Mag2();

		/***** POLARIZATION OBSERVABLE phi of vector J_T *****/
		Double_t ep_trans_mag = sqrt(PositronP4.X()*PositronP4.X() + PositronP4.Y()*PositronP4.Y() + PositronP4.Z()*PositronP4.Z());
		Double_t em_trans_mag = sqrt(ElectronP4.X()*ElectronP4.X() + ElectronP4.Y()*ElectronP4.Y() + ElectronP4.Z()*ElectronP4.Z());

		Double_t JTx = PositronP4.X() * 2*ElectronP4.E()/(PositronP4.E() - ep_trans_mag * cos(PositronP4.Theta()))  +
			ElectronP4.X() * 2*PositronP4.E()/(ElectronP4.E() - em_trans_mag * cos(ElectronP4.Theta()));
		Double_t JTy = PositronP4.Y() * 2*ElectronP4.E()/(PositronP4.E() - ep_trans_mag * cos(PositronP4.Theta()))  +
			ElectronP4.Y() * 2*PositronP4.E()/(ElectronP4.E() - em_trans_mag * cos(ElectronP4.Theta()));

		Double_t Jphi = atan2(JTy, JTx)*TMath::RadToDeg();

		Double_t ep_trans_mag_meas = sqrt(PositronP4_Measured.X()*PositronP4_Measured.X() + PositronP4_Measured.Y()*PositronP4_Measured.Y() + PositronP4_Measured.Z()*PositronP4_Measured.Z());
		Double_t em_trans_mag_meas = sqrt(ElectronP4_Measured.X()*ElectronP4_Measured.X() + ElectronP4_Measured.Y()*ElectronP4_Measured.Y() + ElectronP4_Measured.Z()*ElectronP4_Measured.Z());

		Double_t JTx_meas = PositronP4_Measured.X() * 2*ElectronP4_Measured.E()/(PositronP4_Measured.E() - ep_trans_mag_meas * cos(PositronP4_Measured.Theta()))  +
			ElectronP4_Measured.X() * 2*PositronP4_Measured.E()/(ElectronP4_Measured.E() - em_trans_mag_meas * cos(ElectronP4_Measured.Theta()));
		Double_t JTy_meas = PositronP4_Measured.Y() * 2*ElectronP4_Measured.E()/(PositronP4_Measured.E() -	 ep_trans_mag_meas * cos(PositronP4_Measured.Theta()))  +
			ElectronP4_Measured.Y() * 2*PositronP4_Measured.E()/(ElectronP4_Measured.E() - em_trans_mag_meas * cos(ElectronP4_Measured.Theta()));	
		Double_t Jphi_meas = atan2(JTy_meas, JTx_meas)*TMath::RadToDeg();


		/**************************** Psi, Phi, phi, and CosTheta (Rho0 Helicity Study) *****************************/
		// Assume pion mass for both tracks
		TLorentzVector PiPlusP4, PiMinusP4, TwoTrackP4_PiMass, PiPlusP4_Measured, PiMinusP4_Measured, TwoTrackP4_Measured; 
		PiPlusP4.SetXYZM(locPositiveP4.Px(), locPositiveP4.Py(), locPositiveP4.Pz(), 0.13957);
		PiMinusP4.SetXYZM(locNegativeP4.Px(), locNegativeP4.Py(), locNegativeP4.Pz(), 0.13957);
		TwoTrackP4_PiMass = PiPlusP4 + PiMinusP4;
		PiPlusP4_Measured.SetXYZM(locPositiveP4_Measured.Px(), locPositiveP4_Measured.Py(), locPositiveP4_Measured.Pz(), 0.13957);
		PiMinusP4_Measured.SetXYZM(locNegativeP4_Measured.Px(), locNegativeP4_Measured.Py(), locNegativeP4_Measured.Pz(), 0.13957);
		TwoTrackP4_Measured = PiPlusP4_Measured + PiMinusP4_Measured;

		// calculate rho0 angular variables
		double tkin = (locBeamP4 - TwoTrackP4_PiMass).M2();    // use beam and 2-track momenta
		TLorentzRotation resonanceBoost2( -TwoTrackP4_PiMass.BoostVector() );   // boost into 2-track frame

		TLorentzVector beam_res = resonanceBoost2 * locBeamP4;
		TLorentzVector recoil_res = resonanceBoost2 * locRecoilP4;
		TLorentzVector p1_res = resonanceBoost2 * PiPlusP4;
		TLorentzVector p2_res = resonanceBoost2 * PiMinusP4;

		double phipol = 0;                           // *** Note assumes horizontal polarization plane.
		TVector3 eps(cos(phipol), sin(phipol), 0.0); // beam polarization vector in lab

		// choose helicity frame: z-axis opposite recoil target in rho rest frame. Note that for Primakoff recoil is missing P4, including target.
		TVector3 y = (locBeamP4.Vect().Unit().Cross(-locRecoilP4.Vect().Unit())).Unit();

		// choose helicity frame: z-axis opposite recoil proton in rho rest frame
		TVector3 z = -1. * recoil_res.Vect().Unit();
		TVector3 x = y.Cross(z).Unit();
		TVector3 angleskin( (p1_res.Vect()).Dot(x),
                                    (p1_res.Vect()).Dot(y),
                                    (p1_res.Vect()).Dot(z) );

		double CosThetakin = angleskin.CosTheta();
		double phikin = angleskin.Phi();

		double Phikin = atan2(y.Dot(eps), locBeamP4.Vect().Unit().Dot(eps.Cross(y)));

		double psikin = Phikin - phikin;
		if(psikin < -3.14159) psikin += 2*3.14159;
		if(psikin > 3.14159) psikin -= 2*3.14159;

		/******************************* Repeat for measured variables *******************************/

		// calculate measured and angular variables
		double tmeas = (locBeamP4_Measured - loc2TrackP4_Measured).M2();    // use beam and 2-track momenta
		// cout << " tgen = " << tgen;
		// cout << " tmeas= " << tmeas;
		// cout << " tkin = " << tkin;
		//cout << " " << endl;
		TLorentzRotation resonanceBoost3( -loc2TrackP4_Measured.BoostVector() );   // boost into 2-track frame

		TLorentzVector beam_res_meas = resonanceBoost3 * locBeamP4_Measured;
		TLorentzVector recoil_res_meas = resonanceBoost3 * locMissingP4_Measured;
		TLorentzVector p1_res_meas = resonanceBoost3 * PiPlusP4_Measured;
		TLorentzVector p2_res_meas = resonanceBoost3 * PiMinusP4_Measured;

		// choose helicity frame: z-axis opposite recoil target in rho rest frame. Note that for Primakoff recoil is missing P4, including target.
		TVector3 y_meas = (locBeamP4_Measured.Vect().Unit().Cross(-locMissingP4_Measured.Vect().Unit())).Unit();
		// choose helicity frame: z-axis opposite recoil proton in rho rest frame
		TVector3 z_meas = -1. * recoil_res_meas.Vect().Unit();
		TVector3 x_meas = y_meas.Cross(z_meas).Unit();
		TVector3 anglesmeas( (p1_res_meas.Vect()).Dot(x_meas),
								(p1_res_meas.Vect()).Dot(y_meas),
								(p1_res_meas.Vect()).Dot(z_meas) );

		double CosThetameas = anglesmeas.CosTheta();
		double phimeas = anglesmeas.Phi();

		double Phimeas = atan2(y_meas.Dot(eps), locBeamP4_Measured.Vect().Unit().Dot(eps.Cross(y_meas)));

		double psimeas = Phimeas - phimeas;
		if(psimeas < -3.14159) psimeas += 2*3.14159;
		if(psimeas > 3.14159) psimeas -= 2*3.14159;
		/******************************************** THROWN PARTICLE INFORMATION *******************************************/
	   
		/*
		//Just for doing MC stuff real quick:
		double locEbeam_Thrown = 0;
		if(dThrownBeam != NULL){
			locEbeam_Thrown = dThrownBeam->Get_P4().T();
			TLorentzVector locBeamP4_Thrown = dThrownBeam->Get_P4(); 
			//Loop over throwns
			TLorentzVector locProton_Thrown;
			TLorentzVector locPositronP4_Thrown; 
			TLorentzVector  locElectronP4_Thrown;  
			for(UInt_t loc_i = 0; loc_i < Get_NumThrown(); ++loc_i){
				//Set branch array indices corresponding to this particle 
				dThrownWrapper->Set_ArrayIndex(loc_i); 
				Particle_t thrown_pid = dThrownWrapper->Get_PID(); 
				cout << " loc_i=" << loc_i << " thrown_pid=" << thrown_pid << endl;   
				TLorentzVector locP4_Thrown = dThrownWrapper->Get_P4();
				if (loc_i == 2) locProton_Thrown = locP4_Thrown;    // Assume recoil is index 3
				if (loc_i == 1) locElectronP4_Thrown = locP4_Thrown;
				if (loc_i == 0) locPositronP4_Thrown = locP4_Thrown;
	    	}                                                                                                                                                                            
	    	cout << "After thrown PID assignment " << endl;
	    	TLorentzVector loc2eP4_Thrown = locPositronP4_Thrown + locElectronP4_Thrown;        
	    	double t_thrown = (locBeamP4_Thrown - loc2eP4_Thrown).M2();
	    	double qvec2_thrown = (locBeamP4_Thrown.Vect() - locElectronP4_Thrown.Vect() - locPositronP4_Thrown.Vect()).Mag2();
	    	double elasticity_thrown = locBeamP4_Thrown.E() - locElectronP4_Thrown.E() - locPositronP4_Thrown.E();
		}

	  	//
		*/
	  	/******************************************** EXECUTE ANALYSIS ACTIONS *******************************************/
	  	//	  cout << "analysis action" << endl;
	  	//	  cout << " " << endl;
	  
	  	// Loop through the analysis actions, executing them in order for the active particle combo
	  	//dAnalyzeCutActions->Perform_Action(); // Must be executed before Execute_Actions()
	  	if(!Execute_Actions()) //if the active combo fails a cut, IsComboCutFlag automatically set
	    	continue;

	  	//if you manually execute any actions, and it fails a cut, be sure to call:
	  	//dComboWrapper->Set_IsComboCut(true);



	  	//dHist_CLvsMissingMassDiff->Fill(locMissingMassSquared, dComboWrapper-> Get_ConfidenceLevel_KinFit(""));                                             
		
		dHist_InvMass_TwoTrack->Fill(M2TrackKin, AccWeight);


		/**************************************** HISTOGRAM KINEMATIC FIT QUANTITIES *****************************************/
		dHist_KinFitChiSq->Fill(dComboWrapper->Get_ChiSq_KinFit("")/dComboWrapper->Get_NDF_KinFit(""));
		dHist_KinFitCL->Fill(dComboWrapper->Get_ConfidenceLevel_KinFit(""));
		/**************************************** EXAMPLE: HISTOGRAM BEAM ENERGY *****************************************/

			/**************************************** EXAMPLE: BEST chi2 METHOD *****************************************/

        // Need to uncomment the section computing combo timing info before running this block of code
		{
			// Fill the histogram only when the beam bunch is in-time. 
			if(!locRelBeamBucket)
			{
				dHist_BeamEnergy_BestChiSq->Fill(locBeamP4.E());
				dHist_InvMass_TwoTrack_BestChiSq->Fill(M2TrackKin);
				dHist_FCAL_Energy_pip->Fill(FCAL_Energy_plus);
				dHist_FCAL_Energy_pim->Fill(FCAL_Energy_minus);
				dHist_FCAL_EoverP_pip->Fill(FCAL_EoverPkin_plus_hist);
				dHist_FCAL_EoverP_pim->Fill(FCAL_EoverPkin_minus_hist);
				dHist_FCAL_EoverPmeas_pip->Fill(FCAL_EoverPmeas_plus_hist);
				dHist_FCAL_EoverPmeas_pim->Fill(FCAL_EoverPmeas_minus_hist);
				dHist_Delta_Efcal_kinfitE_vs_kinPmag_plus->Fill(locPositiveP4.Vect().Mag(), FCAL_Energy_plus - locPositiveP4.E());
				dHist_Delta_Efcal_kinfitE_vs_kinPmag_minus->Fill(locNegativeP4.Vect().Mag(), FCAL_Energy_minus - locNegativeP4.E());
				dHist_Delta_Efcal_kinfitE_vs_kinPtheta_plus->Fill(locPositiveP4.Theta()*TMath::RadToDeg(), FCAL_Energy_plus - locPositiveP4.E());
				dHist_Delta_Efcal_kinfitE_vs_kinPtheta_minus->Fill(locNegativeP4.Theta()*TMath::RadToDeg(), FCAL_Energy_minus - locNegativeP4.E());
				dHist_FCAL_Elasticity->Fill(FCAL_Elasticity);
				dHist_FCAL_Asymmetry->Fill(FCAL_Asymmetry);
				// Fill elasticity in asymmetry regions
				if(FCAL_Asymmetry < 0.2) {
					dHist_FCAL_Elasticity_Asym_L0pt2->Fill(FCAL_Elasticity);
				} else if(FCAL_Asymmetry < 0.5) {
					dHist_FCAL_Elasticity_Asym_G0pt2_L0pt5->Fill(FCAL_Elasticity);
				} else {
					dHist_FCAL_Elasticity_Asym_G0pt5->Fill(FCAL_Elasticity);
				}
				dHist_TrackFCAL_DOCA_plus->Fill(TrackFCAL_DOCA_plus);
				dHist_TrackFCAL_DOCA_minus->Fill(TrackFCAL_DOCA_minus);
				dHist_FCAL_E1E9_plus->Fill(FCAL_E1E9_plus);
				dHist_FCAL_E1E9_minus->Fill(FCAL_E1E9_minus);
				dHist_FCAL_E9E25_plus->Fill(FCAL_E9E25_plus);
				dHist_FCAL_E9E25_minus->Fill(FCAL_E9E25_minus);
				dHist_FCAL_kin_res_plus->Fill(fabs(FCAL_kin_res_plus));
				dHist_FCAL_kin_res_minus->Fill(fabs(FCAL_kin_res_minus));
				dHist_FCAL_Saturation_vs_Eshower_plus->Fill(FCAL_Energy_plus, FCAL_Emax_plus);
				dHist_FCAL_Saturation_vs_Eshower_minus->Fill(FCAL_Energy_minus, FCAL_Emax_minus);
				dHist_FCAL_Saturation_plus->Fill(FCAL_Emax_plus);
				dHist_FCAL_Saturation_minus->Fill(FCAL_Emax_minus);
				dHist_FCAL_SumU_plus->Fill(dPositiveWrapper->Get_SumU_FCAL());
				dHist_FCAL_SumU_minus->Fill(dNegativeWrapper->Get_SumU_FCAL());
				dHist_FCAL_SumV_plus->Fill(dPositiveWrapper->Get_SumV_FCAL());
				dHist_FCAL_SumV_minus->Fill(dNegativeWrapper->Get_SumV_FCAL());
				dHist_FCAL_UV_Asymmetry_plus->Fill(FCAL_UV_Asymmetry_plus);
				dHist_FCAL_UV_Asymmetry_minus->Fill(FCAL_UV_Asymmetry_minus);

				// epemPol histograms (electron mass hypothesis)
				dHist_epemPol_InvMass_Kin->Fill(TwoTrackP4_eeMass.M());
				dHist_epemPol_InvMass_Meas->Fill(TwoTrackP4_eeMass_Measured.M());
				dHist_epemPol_qvec2->Fill(qvec2);
				
				// Fill polarization-gated azimuthal histograms
				for(int i = 0; i < nPolarizations; i++)
				{
					if(dPolarizationAngle == atoi(Polarizations[i]))
					{
						dHist_epemPol_JTphi_Kin[i]->Fill(Jphi);
						dHist_epemPol_JTphi_Meas[i]->Fill(Jphi_meas);
						break;
					}
				}

				// Rho0Pol histograms (pion mass hypothesis)
				dHist_Rho0Pol_InvMass_Kin->Fill(TwoTrackP4_PiMass.M());
				dHist_Rho0Pol_InvMass_Meas->Fill(TwoTrackP4_Measured.M());
				
				// Fill polarization-gated angular histograms
				for(int i = 0; i < nPolarizations; i++)
				{
					if(dPolarizationAngle == atoi(Polarizations[i]))
					{
						dHist_Rho0Pol_CosThetaKin[i]->Fill(CosThetakin);
						dHist_Rho0Pol_CosThetaMeas[i]->Fill(CosThetameas);
						dHist_Rho0Pol_phiKin[i]->Fill(phikin * TMath::RadToDeg());
						dHist_Rho0Pol_phiMeas[i]->Fill(phimeas * TMath::RadToDeg());
						dHist_Rho0Pol_PhiKin[i]->Fill(Phikin * TMath::RadToDeg());
						dHist_Rho0Pol_PhiMeas[i]->Fill(Phimeas * TMath::RadToDeg());
						dHist_Rho0Pol_psiKin[i]->Fill(psikin * TMath::RadToDeg());
						dHist_Rho0Pol_psiMeas[i]->Fill(psimeas * TMath::RadToDeg());
						dHist_Rho0Pol_phikin_vs_Phikin[i]->Fill(Phikin * TMath::RadToDeg(), phikin * TMath::RadToDeg());
						break;
					}
				}
				
				ThisComboIsBestChiSq = true;
			}
		}
		/**************************************** EXAMPLE: HISTOGRAM BEAM ENERGY *****************************************/

		//Histogram beam energy (if haven't already)
		if(locUsedSoFar_BeamEnergy.find(locBeamID) == locUsedSoFar_BeamEnergy.end())
		{
			dHist_BeamEnergy->Fill(locBeamP4.E()); // Fills in-time and out-of-time beam photon combos
			//dHist_BeamEnergy->Fill(locBeamP4.E(),locHistAccidWeightFactor); // Alternate version with accidental subtraction

			locUsedSoFar_BeamEnergy.insert(locBeamID);
		}

		/************************************ EXAMPLE: HISTOGRAM MISSING MASS SQUARED ************************************/

		//Missing Mass Squared
		double locMissingMassSquared = locMissingP4_Measured.M2();
		dHist_MissingMassSquared->Fill(locMissingMassSquared);

		//E.g. Cut on missing mass squared if desired
		//if((locMissingMassSquared < -0.04) || (locMissingMassSquared > 0.04))
		//{
		//	dComboWrapper->Set_IsComboCut(true);
		//	continue;
		//}


		/****************************************** FILL FLAT TREE (IF DESIRED) ******************************************/
	  	cout << "DEBUG 5: Starting flat tree fill" << endl;
		if(OnlyBestChiSqComboInFlat == true && ThisComboIsBestChiSq == false){
			// Skip if out-of-time beam bunch
			if(locRelBeamBucket != 0){
				dComboWrapper->Set_IsComboCut(true);
				continue;
			} 
		}

	 	// Event Information
		cout << "DEBUG 6: Filling event info" << endl;
		dFlatTreeInterface->Fill_Fundamental<Float_t>("RunNumber", sanitize_f(Get_RunNumber()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("EventNumber", sanitize_f(Get_EventNumber()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Combo", sanitize_f(loc_i));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("NumCombos", sanitize_f(dComboWrapper->Get_NumCombos()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("NumUnusedShowers", sanitize_f(dComboWrapper->Get_NumUnusedShowers()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Energy_UnusedShowers", sanitize_f(dComboWrapper->Get_Energy_UnusedShowers()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("NumUnusedTracks", sanitize_f(dComboWrapper->Get_NumUnusedTracks()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("kinfit_CL", sanitize_f(dComboWrapper->Get_ConfidenceLevel_KinFit("")));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("ChiSq_KinFit", sanitize_f(dComboWrapper->Get_ChiSq_KinFit()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("NDF_KinFit", sanitize_f(dComboWrapper->Get_NDF_KinFit()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("RFTime_KinFit", sanitize_f(dComboWrapper->Get_RFTime()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("RFTime_Measured", sanitize_f(dComboWrapper->Get_RFTime_Measured()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("AccWeight", sanitize_f(AccWeight));
		cout << "DEBUG 7: Filled event info" << endl;

		// Beam Kinematic Fit X4 and P4
		cout << "DEBUG 8: Filling beam info" << endl;
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_KinFit_E", sanitize_f(dComboBeamWrapper->Get_P4().E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_KinFit_X", sanitize_f(dComboBeamWrapper->Get_P4().X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_KinFit_Y", sanitize_f(dComboBeamWrapper->Get_P4().Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_KinFit_Z", sanitize_f(dComboBeamWrapper->Get_P4().Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_KinFit_T", sanitize_f(dComboBeamWrapper->Get_X4().T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_KinFit_X", sanitize_f(dComboBeamWrapper->Get_X4().X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_KinFit_Y", sanitize_f(dComboBeamWrapper->Get_X4().Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_KinFit_Z", sanitize_f(dComboBeamWrapper->Get_X4().Z()));

		// Beam Measured X4 and P4
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_Measured_T", sanitize_f(dComboBeamWrapper->Get_X4_Measured().T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_Measured_X", sanitize_f(dComboBeamWrapper->Get_X4_Measured().X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_Measured_Y", sanitize_f(dComboBeamWrapper->Get_X4_Measured().Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_X4_Measured_Z", sanitize_f(dComboBeamWrapper->Get_X4_Measured().Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_Measured_E", sanitize_f(dComboBeamWrapper->Get_P4_Measured().E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_Measured_X", sanitize_f(dComboBeamWrapper->Get_P4_Measured().X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_Measured_Y", sanitize_f(dComboBeamWrapper->Get_P4_Measured().Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Beam_P4_Measured_Z", sanitize_f(dComboBeamWrapper->Get_P4_Measured().Z()));

		// Positive Track Kinematic Fit X4 and P4
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_KinFit_T", sanitize_f(Positive_X4_KinFit.T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_KinFit_X", sanitize_f(Positive_X4_KinFit.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_KinFit_Y", sanitize_f(Positive_X4_KinFit.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_KinFit_Z", sanitize_f(Positive_X4_KinFit.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_KinFit_E", sanitize_f(locPositiveP4.E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_KinFit_X", sanitize_f(locPositiveP4.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_KinFit_Y", sanitize_f(locPositiveP4.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_KinFit_Z", sanitize_f(locPositiveP4.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P3_KinFit_Mag", sanitize_f(locPositiveP4.P()));

		// Positive Track Measured X4 and P4
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_Measured_T", sanitize_f(Positive_X4_Measured.T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_Measured_X", sanitize_f(Positive_X4_Measured.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_Measured_Y", sanitize_f(Positive_X4_Measured.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_X4_Measured_Z", sanitize_f(Positive_X4_Measured.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_Measured_E", sanitize_f(locPositiveP4_Measured.E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_Measured_X", sanitize_f(locPositiveP4_Measured.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_Measured_Y", sanitize_f(locPositiveP4_Measured.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P4_Measured_Z", sanitize_f(locPositiveP4_Measured.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_P3_Measured_Mag", sanitize_f(locPositiveP4_Measured.P()));

		// Negative Track Kinematic Fit X4 and P4
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_KinFit_T", sanitize_f(Negative_X4_KinFit.T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_KinFit_X", sanitize_f(Negative_X4_KinFit.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_KinFit_Y", sanitize_f(Negative_X4_KinFit.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_KinFit_Z", sanitize_f(Negative_X4_KinFit.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_KinFit_E", sanitize_f(locNegativeP4.E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_KinFit_X", sanitize_f(locNegativeP4.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_KinFit_Y", sanitize_f(locNegativeP4.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_KinFit_Z", sanitize_f(locNegativeP4.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P3_KinFit_Mag", sanitize_f(locNegativeP4.P()));

		// Negative Track Measured X4 and P4
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_Measured_T", sanitize_f(Negative_X4_Measured.T()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_Measured_X", sanitize_f(Negative_X4_Measured.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_Measured_Y", sanitize_f(Negative_X4_Measured.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_X4_Measured_Z", sanitize_f(Negative_X4_Measured.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_Measured_E", sanitize_f(locNegativeP4_Measured.E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_Measured_X", sanitize_f(locNegativeP4_Measured.X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_Measured_Y", sanitize_f(locNegativeP4_Measured.Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P4_Measured_Z", sanitize_f(locNegativeP4_Measured.Z()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_P3_Measured_Mag", sanitize_f(locNegativeP4_Measured.P()));

		// Kinematic Fit P4 of Recoil track (missing particle)
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Recoil_P4_KinFit_E", sanitize_f(dRecoilWrapper->Get_P4().E()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Recoil_P4_KinFit_X", sanitize_f(dRecoilWrapper->Get_P4().X()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Recoil_P4_KinFit_Y", sanitize_f(dRecoilWrapper->Get_P4().Y()));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Recoil_P4_KinFit_Z", sanitize_f(dRecoilWrapper->Get_P4().Z()));

		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_kin_E",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_kin_X",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_kin_Y",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_kin_Z",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_meas_E",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_meas_X",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_meas_Y",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("g_p_meas_Z",1);

		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_kin_E",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_kin_X",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_kin_Y",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_kin_Z",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_meas_E",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_meas_X",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_meas_Y",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("DecayingPi0_p_meas_Z",1);

		// Track Goodness of Fit
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_Beta_Timing", sanitize_f(Beta_Timing_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_ChiSq_DCdEdx", sanitize_f(ChiSq_DCdEdx_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_NDF_DCdEdx", sanitize_f(NDF_DCdEdx_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_ChiSq_Timing", sanitize_f(ChiSq_Timing_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_NDF_Timing", sanitize_f(NDF_Timing_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_ChiSq_Tracking", sanitize_f(ChiSq_Tracking_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_NDF_Tracking", sanitize_f(NDF_Tracking_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_PIDFOM", sanitize_f(PIDFOM_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_ThrownIndex", sanitize_f(ThrownIndex_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_TrackID", sanitize_f(Positive_TrackID));

		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_Beta_Timing", sanitize_f(Beta_Timing_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_ChiSq_DCdEdx", sanitize_f(ChiSq_DCdEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_NDF_DCdEdx", sanitize_f(NDF_DCdEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_ChiSq_Timing", sanitize_f(ChiSq_Timing_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_NDF_Timing", sanitize_f(NDF_Timing_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_ChiSq_Tracking", sanitize_f(ChiSq_Tracking_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_NDF_Tracking", sanitize_f(NDF_Tracking_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_PIDFOM", sanitize_f(PIDFOM_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_ThrownIndex", sanitize_f(ThrownIndex_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_TrackID", sanitize_f(Negative_TrackID));

		// FCAL - Use corrected and randomized values
		dFlatTreeInterface->Fill_Fundamental<Float_t>("FCAL_Elasticity", sanitize_f((FCAL_Energy_plus + FCAL_Energy_minus)/(dComboBeamWrapper->Get_P4().E())));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("FCAL_Asymmetry", sanitize_f(FCAL_Asymmetry));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_Energy_FCAL", sanitize_f(FCAL_Energy_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_EoverPkin_FCAL", sanitize_f(FCAL_EoverPkin_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_EoverPmeas_FCAL", sanitize_f(FCAL_EoverPmeas_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_E1E9_FCAL", sanitize_f(FCAL_E1E9_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_E9E25_FCAL", sanitize_f(FCAL_E9E25_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_TrackFCAL_DOCA", sanitize_f(TrackFCAL_DOCA_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_SumU_FCAL", sanitize_f(SumU_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_SumV_FCAL", sanitize_f(SumV_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_UV_Asymmetry_FCAL", sanitize_f(FCAL_UV_Asymmetry_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_Saturation_FCAL", sanitize_f(FCAL_Emax_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_kin_res_FCAL", sanitize_f(FCAL_kin_res_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_meas_res_FCAL", sanitize_f(FCAL_meas_res_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_Energy_FCAL", sanitize_f(FCAL_Energy_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_EoverPkin_FCAL", sanitize_f(FCAL_EoverPkin_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_EoverPmeas_FCAL", sanitize_f(FCAL_EoverPmeas_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_E1E9_FCAL", sanitize_f(FCAL_E1E9_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_E9E25_FCAL", sanitize_f(FCAL_E9E25_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_TrackFCAL_DOCA", sanitize_f(TrackFCAL_DOCA_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_SumU_FCAL", sanitize_f(SumU_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_SumV_FCAL", sanitize_f(SumV_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_UV_Asymmetry_FCAL", sanitize_f(FCAL_UV_Asymmetry_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_Saturation_FCAL", sanitize_f(FCAL_Emax_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_kin_res_FCAL", sanitize_f(FCAL_kin_res_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_meas_res_FCAL", sanitize_f(FCAL_meas_res_minus));

		// BCAL
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_Energy_BCAL", sanitize_f(Energy_BCAL_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_Energy_BCALPreshower", sanitize_f(Energy_BCALPreshower_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_RMSTime_BCAL", sanitize_f(RMSTime_BCAL_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_SigLong_BCAL", sanitize_f(SigLong_BCAL_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_SigTheta_BCAL", sanitize_f(SigTheta_BCAL_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_SigTrans_BCAL", sanitize_f(SigTrans_BCAL_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_TrackBCAL_DeltaPhi", sanitize_f(TrackBCAL_DeltaPhi_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_TrackBCAL_DeltaZ", sanitize_f(TrackBCAL_DeltaZ_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_Energy_BCAL", sanitize_f(Energy_BCAL_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_Energy_BCALPreshower", sanitize_f(Energy_BCALPreshower_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_RMSTime_BCAL", sanitize_f(RMSTime_BCAL_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_SigLong_BCAL", sanitize_f(SigLong_BCAL_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_SigTheta_BCAL", sanitize_f(SigTheta_BCAL_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_SigTrans_BCAL", sanitize_f(SigTrans_BCAL_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_TrackBCAL_DeltaPhi", sanitize_f(TrackBCAL_DeltaPhi_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_TrackBCAL_DeltaZ", sanitize_f(TrackBCAL_DeltaZ_minus));

		// Other dEdx and Timing Information
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_dEdx_CDC", sanitize_f(CDC_dEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_dEdx_CDC_integral", sanitize_f(CDC_dEdx_integral_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_dEdx_FDC", sanitize_f(FDC_dEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_dEdx_ST", sanitize_f(ST_dEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_dEdx_TOF", sanitize_f(TOF_dEdx_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_HitTime", sanitize_f(HitTime_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Negative_RFDeltaTVar", sanitize_f(RFDeltaTVar_minus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_HitTime", sanitize_f(HitTime_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_RFDeltaTVar", sanitize_f(RFDeltaTVar_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_dEdx_CDC", sanitize_f(CDC_dEdx_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_dEdx_CDC_integral", sanitize_f(CDC_dEdx_integral_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_dEdx_FDC", sanitize_f(FDC_dEdx_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_dEdx_ST", sanitize_f(ST_dEdx_plus));
		dFlatTreeInterface->Fill_Fundamental<Float_t>("Positive_dEdx_TOF", sanitize_f(TOF_dEdx_plus));

		// Placeholder Minus Track Model Responses for Correct TMVA File Structure
		dFlatTreeInterface->Fill_Fundamental<Float_t>("MLP_Response_Minus",1);
		dFlatTreeInterface->Fill_Fundamental<Float_t>("BDT_Response_Minus",1);

		Fill_FlatTree(); //for all surviving combos
		
	} // end of combo loop

	//FILL HISTOGRAMS: Num combos / events surviving actions
	Fill_NumCombosSurvivedHists();


	/************************************ EXAMPLE: FILL CLONE OF TTREE HERE WITH CUTS APPLIED ************************************/
	/*
	Bool_t locIsEventCut = true;
	for(UInt_t loc_i = 0; loc_i < Get_NumCombos(); ++loc_i) {
		//Set branch array indices for combo and all combo particles
		dComboWrapper->Set_ComboIndex(loc_i);
		// Is used to indicate when combos have been cut
		if(dComboWrapper->Get_IsComboCut())
			continue;
		locIsEventCut = false; // At least one combo succeeded
		break;
	}
	if(!locIsEventCut && dOutputTreeFileName != ""){
	  Fill_OutputTree();
		
	}
	*/

	return kTRUE;
}

void DSelector_2eMissingProton::ApplyFCALCorrections(Double_t positiveP, Double_t negativeP,
                                                      Double_t& FCAL_Energy_plus, Double_t& FCAL_Energy_minus)
{
	cout << "DEBUG FCAL_FUNC: Inside ApplyFCALCorrections, positiveP=" << positiveP << ", negativeP=" << negativeP << endl;
	// Apply momentum-dependent non-linear FCAL energy corrections
	// Corrects MC simulation to match data E/p distributions
	//
	// For positive track (e+):
	if(positiveP <= 3.60){
		FCAL_Energy_plus -= ep_FCAL_cor1->Eval(positiveP);
		FCAL_Energy_plus += ep_FCAL_cor2data1->Eval(positiveP);
	} else {
		FCAL_Energy_plus -= ep_FCAL_cor2->Eval(positiveP);
		FCAL_Energy_plus += ep_FCAL_cor2data2->Eval(positiveP);
	}
	
	// For negative track (e-):
	if(negativeP <= 3.60){
		FCAL_Energy_minus -= em_FCAL_cor1->Eval(negativeP);
		FCAL_Energy_minus += em_FCAL_cor2data1->Eval(negativeP);
	} else {
		FCAL_Energy_minus -= em_FCAL_cor1->Eval(negativeP);
		FCAL_Energy_minus += em_FCAL_cor2data2->Eval(negativeP);
	}
}

void DSelector_2eMissingProton::SwapChargeLabels(
	// Kinfit P4
	TLorentzVector& Positive_P4_KinFit, TLorentzVector& Negative_P4_KinFit,
	// Measured P4
	TLorentzVector& Positive_P4_Measured, TLorentzVector& Negative_P4_Measured,
	// Kinfit X4
	TLorentzVector& Positive_X4_KinFit, TLorentzVector& Negative_X4_KinFit,
	// Measured X4
	TLorentzVector& Positive_X4_Measured, TLorentzVector& Negative_X4_Measured,
	// Track IDs
	Int_t& Positive_TrackID, Int_t& Negative_TrackID,
	// Momentum magnitudes
	Double_t& Positive_P3_KinFit_Mag, Double_t& Negative_P3_KinFit_Mag,
	Double_t& Positive_P3_Measured_Mag, Double_t& Negative_P3_Measured_Mag,
	// FCAL quantities
	Double_t& FCAL_Energy_plus, Double_t& FCAL_Energy_minus,
	Double_t& FCAL_EoverPkin_plus, Double_t& FCAL_EoverPkin_minus,
	Double_t& FCAL_EoverPmeas_plus, Double_t& FCAL_EoverPmeas_minus,
	Double_t& TrackFCAL_DOCA_plus, Double_t& TrackFCAL_DOCA_minus,
	Double_t& FCAL_E1E9_plus, Double_t& FCAL_E1E9_minus,
	Double_t& FCAL_E9E25_plus, Double_t& FCAL_E9E25_minus,
	Double_t& SumU_plus, Double_t& SumU_minus,
	Double_t& SumV_plus, Double_t& SumV_minus,
	// dEdx
	Double_t& TOF_dEdx_plus, Double_t& TOF_dEdx_minus,
	Double_t& FDC_dEdx_plus, Double_t& FDC_dEdx_minus,
	Double_t& CDC_dEdx_plus, Double_t& CDC_dEdx_minus,
	Double_t& CDC_dEdx_integral_plus, Double_t& CDC_dEdx_integral_minus,
	Double_t& ST_dEdx_plus, Double_t& ST_dEdx_minus,
	// Timing and Chi-Squared
	Double_t& Beta_Timing_plus, Double_t& Beta_Timing_minus,
	Double_t& ChiSq_DCdEdx_plus, Double_t& ChiSq_DCdEdx_minus,
	Double_t& NDF_DCdEdx_plus, Double_t& NDF_DCdEdx_minus,
	Double_t& ChiSq_Timing_plus, Double_t& ChiSq_Timing_minus,
	Double_t& NDF_Timing_plus, Double_t& NDF_Timing_minus,
	Double_t& ChiSq_Tracking_plus, Double_t& ChiSq_Tracking_minus,
	Double_t& NDF_Tracking_plus, Double_t& NDF_Tracking_minus,
	Double_t& PIDFOM_plus, Double_t& PIDFOM_minus,
	Int_t& ThrownIndex_plus, Int_t& ThrownIndex_minus,
	Double_t& HitTime_plus, Double_t& HitTime_minus,
	Double_t& RFDeltaTVar_plus, Double_t& RFDeltaTVar_minus,
	// BCAL quantities
	Double_t& Energy_BCAL_plus, Double_t& Energy_BCAL_minus,
	Double_t& Energy_BCALPreshower_plus, Double_t& Energy_BCALPreshower_minus,
	Double_t& RMSTime_BCAL_plus, Double_t& RMSTime_BCAL_minus,
	Double_t& SigLong_BCAL_plus, Double_t& SigLong_BCAL_minus,
	Double_t& SigTheta_BCAL_plus, Double_t& SigTheta_BCAL_minus,
	Double_t& SigTrans_BCAL_plus, Double_t& SigTrans_BCAL_minus,
	Double_t& TrackBCAL_DeltaPhi_plus, Double_t& TrackBCAL_DeltaPhi_minus,
	Double_t& TrackBCAL_DeltaZ_plus, Double_t& TrackBCAL_DeltaZ_minus
)
{
	// Swap all positive/negative labels by std::swap
	// P4 vectors
	std::swap(Positive_P4_KinFit, Negative_P4_KinFit);
	std::swap(Positive_P4_Measured, Negative_P4_Measured);
	std::swap(Positive_X4_KinFit, Negative_X4_KinFit);
	std::swap(Positive_X4_Measured, Negative_X4_Measured);
	
	// Track IDs
	std::swap(Positive_TrackID, Negative_TrackID);
	
	// Momentum magnitudes
	std::swap(Positive_P3_KinFit_Mag, Negative_P3_KinFit_Mag);
	std::swap(Positive_P3_Measured_Mag, Negative_P3_Measured_Mag);
	
	// FCAL
	std::swap(FCAL_Energy_plus, FCAL_Energy_minus);
	std::swap(FCAL_EoverPkin_plus, FCAL_EoverPkin_minus);
	std::swap(FCAL_EoverPmeas_plus, FCAL_EoverPmeas_minus);
	std::swap(TrackFCAL_DOCA_plus, TrackFCAL_DOCA_minus);
	std::swap(FCAL_E1E9_plus, FCAL_E1E9_minus);
	std::swap(FCAL_E9E25_plus, FCAL_E9E25_minus);
	std::swap(SumU_plus, SumU_minus);
	std::swap(SumV_plus, SumV_minus);
	
	// dEdx
	std::swap(TOF_dEdx_plus, TOF_dEdx_minus);
	std::swap(FDC_dEdx_plus, FDC_dEdx_minus);
	std::swap(CDC_dEdx_plus, CDC_dEdx_minus);
	std::swap(CDC_dEdx_integral_plus, CDC_dEdx_integral_minus);
	std::swap(ST_dEdx_plus, ST_dEdx_minus);
	
	// Timing and Chi-Squared
	std::swap(Beta_Timing_plus, Beta_Timing_minus);
	std::swap(ChiSq_DCdEdx_plus, ChiSq_DCdEdx_minus);
	std::swap(NDF_DCdEdx_plus, NDF_DCdEdx_minus);
	std::swap(ChiSq_Timing_plus, ChiSq_Timing_minus);
	std::swap(NDF_Timing_plus, NDF_Timing_minus);
	std::swap(ChiSq_Tracking_plus, ChiSq_Tracking_minus);
	std::swap(NDF_Tracking_plus, NDF_Tracking_minus);
	std::swap(PIDFOM_plus, PIDFOM_minus);
	std::swap(ThrownIndex_plus, ThrownIndex_minus);
	std::swap(HitTime_plus, HitTime_minus);
	std::swap(RFDeltaTVar_plus, RFDeltaTVar_minus);
	
	// BCAL
	std::swap(Energy_BCAL_plus, Energy_BCAL_minus);
	std::swap(Energy_BCALPreshower_plus, Energy_BCALPreshower_minus);
	std::swap(RMSTime_BCAL_plus, RMSTime_BCAL_minus);
	std::swap(SigLong_BCAL_plus, SigLong_BCAL_minus);
	std::swap(SigTheta_BCAL_plus, SigTheta_BCAL_minus);
	std::swap(SigTrans_BCAL_plus, SigTrans_BCAL_minus);
	std::swap(TrackBCAL_DeltaPhi_plus, TrackBCAL_DeltaPhi_minus);
	std::swap(TrackBCAL_DeltaZ_plus, TrackBCAL_DeltaZ_minus);
}

void DSelector_2eMissingProton::Finalize(void)
{
	//Save anything to output here that you do not want to be in the default DSelector output ROOT file.

	//Otherwise, don't do anything else (especially if you are using PROOF).
		//If you are using PROOF, this function is called on each thread,
		//so anything you do will not have the combined information from the various threads.
		//Besides, it is best-practice to do post-processing (e.g. fitting) separately, in case there is a problem.

	//DO YOUR STUFF HERE
	
	// Clean up TMVA Readers
	if(dTMVAReader_MLP != NULL) {
		delete dTMVAReader_MLP;
		dTMVAReader_MLP = NULL;
	}
	if(dTMVAReader_BDT != NULL) {
		delete dTMVAReader_BDT;
		dTMVAReader_BDT = NULL;
	}
	
	//CALL THIS LAST
	DSelector::Finalize(); //Saves results to the output file
}
