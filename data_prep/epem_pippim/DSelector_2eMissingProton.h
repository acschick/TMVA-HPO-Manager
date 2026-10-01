#ifndef DSelector_2eMissingProton_h
#define DSelector_2eMissingProton_h

#include <iostream>

#include "DSelector/DSelector.h"
#include "DSelector/DHistogramActions.h"
#include "DSelector/DCutActions.h"
#include "TMVA/Reader.h"

#include "TH1I.h"
#include "TH2I.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TRandom.h"
#include "TLorentzVector.h"
#include "TLorentzRotation.h"

class DSelector_2eMissingProton : public DSelector
{
	public:

		DSelector_2eMissingProton(TTree* locTree = NULL) : DSelector(locTree){}
		virtual ~DSelector_2eMissingProton(){}

		void Init(TTree *tree);
		Bool_t Process(Long64_t entry);

	private:
		// TMVA Readers for XML-based inference
		TMVA::Reader* dTMVAReader_MLP;
		TMVA::Reader* dTMVAReader_BDT;
		
		// Input variables for TMVA (must be Float_t for TMVA::Reader)
		Float_t dTMVA_EoverPkin_FCAL;
		Float_t dTMVA_TrackFCAL_DOCA;
		Float_t dTMVA_E9E25_FCAL;
		Float_t dTMVA_E1E9_FCAL;
		Float_t dTMVA_SumU_FCAL;
		Float_t dTMVA_SumV_FCAL;
		Float_t dTMVA_UV_Asymmetry_FCAL;
		Float_t dTMVA_Saturation_FCAL;
		
		void Get_ComboWrappers(void);
		void Finalize(void);
		
		// Apply FCAL energy corrections (modifies FCAL_Energy values in place)
		void ApplyFCALCorrections(Double_t positiveP, Double_t negativeP, 
		                          Double_t& FCAL_Energy_plus, Double_t& FCAL_Energy_minus);
		
		// Swap all positive/negative track variables (for charge randomization)
		void SwapChargeLabels(
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
		);

		// BEAM POLARIZATION INFORMATION
		UInt_t dPreviousRunNumber;
		bool dIsPolarizedFlag; //else is AMO
		bool dIsPARAFlag; //else is PERP or AMO
		int dPolarizationAngle;

		bool dIsMC;

		// ANALYZE CUT ACTIONS
		// // Automatically makes mass histograms where one cut is missing
		DHistogramAction_AnalyzeCutActions* dAnalyzeCutActions;

		//CREATE REACTION-SPECIFIC PARTICLE ARRAYS

		//Step 0
		DParticleComboStep* dStep0Wrapper;
		DBeamParticle* dComboBeamWrapper;
		DChargedTrackHypothesis* dPositiveWrapper;  // Generic: e+, π+, K+, etc.
		DChargedTrackHypothesis* dNegativeWrapper;  // Generic: e-, π-, K-, etc.
		DKinematicData* dRecoilWrapper;  // Generic: missing proton, detected proton, etc.
	
		// DEFINE YOUR HISTOGRAMS HERE
		// EXAMPLES:
		TH1I* dHist_MissingMassSquared;
		TH1I* dHist_BeamEnergy;
		TH1I* dHist_BeamEnergy_BestChiSq;
		TH1I* dHist_TaggerAccidentals;
		TH1D* dHist_KinFitChiSq;
		TH1D* dHist_KinFitCL;

		TH1D* dHist_InvMass_TwoTrack;
		TH1D* dHist_InvMass_TwoTrack_BestChiSq;
		TH1D* dHist_MLPResponse_plus;
		TH1D* dHist_MLPResponse_minus;
		TH2D* dHist_MLPResponsePlus_vs_MLPResponseMinus;

		TH1D* dHist_FCAL_Energy_pip;
		TH1D* dHist_FCAL_Energy_pim;
		TH1D* dHist_FCAL_EoverP_pip;
		TH1D* dHist_FCAL_EoverP_pim;
		TH1D* dHist_FCAL_EoverPmeas_pip;
		TH1D* dHist_FCAL_EoverPmeas_pim;
		TH2D* dHist_Delta_Efcal_kinfitE_vs_kinPmag_plus;
		TH2D* dHist_Delta_Efcal_kinfitE_vs_kinPmag_minus;
		TH2D* dHist_Delta_Efcal_kinfitE_vs_kinPtheta_plus;
		TH2D* dHist_Delta_Efcal_kinfitE_vs_kinPtheta_minus;
		TH1D* dHist_FCAL_Elasticity;
		TH1D* dHist_FCAL_Asymmetry;
		TH1D* dHist_FCAL_Elasticity_Asym_L0pt2;
		TH1D* dHist_FCAL_Elasticity_Asym_G0pt2_L0pt5;
		TH1D* dHist_FCAL_Elasticity_Asym_G0pt5;
		TH1D* dHist_TrackFCAL_DOCA_plus;
		TH1D* dHist_TrackFCAL_DOCA_minus;
		TH1D* dHist_FCAL_E1E9_plus;
		TH1D* dHist_FCAL_E1E9_minus;
		TH1D* dHist_FCAL_E9E25_plus;
		TH1D* dHist_FCAL_E9E25_minus;
		TH1D* dHist_FCAL_kin_res_plus;
		TH1D* dHist_FCAL_kin_res_minus;
		TH1D* dHist_FCAL_meas_res_plus;
		TH1D* dHist_FCAL_meas_res_minus;
		TH2D* dHist_FCAL_Saturation_vs_Eshower_plus;
		TH2D* dHist_FCAL_Saturation_vs_Eshower_minus;
		TH1D* dHist_FCAL_Saturation_plus;
		TH1D* dHist_FCAL_Saturation_minus;
		TH1D* dHist_FCAL_SumU_plus;
		TH1D* dHist_FCAL_SumU_minus;
		TH1D* dHist_FCAL_SumV_plus;
		TH1D* dHist_FCAL_SumV_minus;
		TH1D* dHist_FCAL_UV_Asymmetry_plus;
		TH1D* dHist_FCAL_UV_Asymmetry_minus;

		// Polarization angles to analyze
		static const int nPolarizations = 4;
		static constexpr const char* Polarizations[nPolarizations] = {"0", "45", "90", "135"};

		// epemPol directory histograms (electron mass hypothesis) - indexed by polarization
		TH1D* dHist_epemPol_InvMass_Kin;
		TH1D* dHist_epemPol_InvMass_Meas;
		TH1D* dHist_epemPol_JTphi_Kin[nPolarizations];
		TH1D* dHist_epemPol_JTphi_Meas[nPolarizations];
		TH1D* dHist_epemPol_qvec2;

		// Rho0Pol directory histograms (pion mass hypothesis) - indexed by polarization
		TH1D* dHist_Rho0Pol_InvMass_Kin;
		TH1D* dHist_Rho0Pol_InvMass_Meas;
		TH1D* dHist_Rho0Pol_CosThetaKin[nPolarizations];
		TH1D* dHist_Rho0Pol_CosThetaMeas[nPolarizations];
		TH1D* dHist_Rho0Pol_phiKin[nPolarizations];
		TH1D* dHist_Rho0Pol_phiMeas[nPolarizations];
		TH1D* dHist_Rho0Pol_PhiKin[nPolarizations];
		TH1D* dHist_Rho0Pol_PhiMeas[nPolarizations];
		TH1D* dHist_Rho0Pol_psiKin[nPolarizations];
		TH1D* dHist_Rho0Pol_psiMeas[nPolarizations];
		TH2D* dHist_Rho0Pol_phikin_vs_Phikin[nPolarizations];


		// Cut threshold values
		Double_t dMinKinFitCL;
		Double_t dMinBeamEnergy;
		Double_t dMaxBeamEnergy;
		Int_t dMaxNumUnusedTracks;

		// Cut enable/disable flags
		Bool_t ApplyPreselectionEoPCut;        // 0a: Preselection E/p > 0.4 (measured) for both tracks
		Bool_t ApplyNoUnusedTracksCut;         // 1a: No unused charged tracks
		Bool_t ApplyNoUnusedShowersCut;        // 1b: No unused neutral shower energy
		Bool_t ApplyFCALEnergyNonZeroCut;      // 2a: FCAL E_1 and E_2 > 0
		Bool_t ApplyTOFdEdxNonZeroCut;         // 2b: TOF dE/dx > 0 for both tracks
		Bool_t ApplyBeamEnergyCut;             // 3a: Coherent peak (8.2-8.8 GeV)
		Bool_t ApplyMinThetaCut;               // 3b: Minimum theta > 1.5 deg
		Bool_t ApplyMaxThetaCut;               // 3c: Maximum theta cut (normally off for rho0)
		Bool_t Apply2DThetaAcceptanceMatchCut; // 3d: Sim/data theta acceptance match (off for rho0)
		Bool_t ApplyMomentumRangeCut;          // 3e: Valid FCAL correction range (0.45-7.92 GeV)
		Bool_t ApplyInvariantMassCut;          // 3f: Invariant mass window (0.7-0.77 GeV for rho0)
		Bool_t ApplyVertexZCut;                // 3g: Vertex Z position (52-78 cm)
		Bool_t ApplyKinFitCLCut;               // 3h: Kinematic fit confidence level > 1E-6

		Bool_t OnlyBestChiSqComboInFlat;       // Only save combo with best chiSq to flat tree
		Bool_t ThisComboIsBestChiSq;           // Flag to indicate if the current combo is the best chiSq combo

		// FCAL-specific flags for 2eMissingProton analysis
		Bool_t ApplyFCALEnergyCorrections;      // Apply momentum-dependent FCAL energy corrections
		Bool_t ApplyFCALPlusMinusRandomization; // Randomly swap positive/negative track labels
		Bool_t ApplyMLPClassification;		    // Apply MLP-based e+/e- identification
		Bool_t ApplyMLPResponseCut;            // Test: Minimum MLP response cut for e+/e- identification

		// Additional test/development cuts (NOT used in official analysis)
		Bool_t ApplyMinEoverP1Cut;              // Test: Minimum E/P cut for track 1
		Bool_t ApplyMaxEoverP1Cut;              // Test: Maximum E/P cut for track 1
		Bool_t ApplyMinEoverP2Cut;              // Test: Minimum E/P cut for track 2
		Bool_t ApplyMaxEoverP2Cut;              // Test: Maximum E/P cut for track 2
		Bool_t ApplyMinFCALElasticityCut;      // Test: Minimum FCAL elasticity (E1+E2)/Ebeam
		Bool_t ApplyMaxFCALElasticityCut;      // Test: Maximum FCAL elasticity (E1+E2)/Ebeam
		Bool_t ApplyMaxFCALDOCACut;            // Test: Maximum FCAL DOCA cut
		Bool_t ApplyMaxTOFdEdxCut;             // Test: Maximum TOF dE/dx cut
		

		// Cut threshold values for test cuts
		Double_t dMinEoverP1;
		Double_t dMaxEoverP1;
		Double_t dMinEoverP2;
		Double_t dMaxEoverP2;
		Double_t dMinFCALElasticity;
		Double_t dMaxFCALElasticity;
		Double_t dMaxFCALDOCA;
		Double_t dMaxTOFdEdx;	Double_t dMinMLPResponse;
		// Random number generator for charge randomization
		TRandom *RandomGen;

		// FCAL correction functions for e+ and e-
		TF1* ep_FCAL_cor1;
		TF1* ep_FCAL_cor2;
		TF1* em_FCAL_cor1;
		TF1* em_FCAL_cor2;
		TF1* ep_FCAL_cor2data1;
		TF1* ep_FCAL_cor2data2;
		TF1* em_FCAL_cor2data1;
		TF1* em_FCAL_cor2data2;

		// Constants
		Double_t PI = 3.14159;
		Double_t MProton = 0.93827231;  //(GeV/c^2)
		Double_t ElectronMass = 0.000511;

	ClassDef(DSelector_2eMissingProton, 0);
};

void DSelector_2eMissingProton::Get_ComboWrappers(void)
{
	//Step 0
	dStep0Wrapper = dComboWrapper->Get_ParticleComboStep(0);
	dComboBeamWrapper = static_cast<DBeamParticle*>(dStep0Wrapper->Get_InitialParticle());
	dPositiveWrapper = static_cast<DChargedTrackHypothesis*>(dStep0Wrapper->Get_FinalParticle(0));
	dNegativeWrapper = static_cast<DChargedTrackHypothesis*>(dStep0Wrapper->Get_FinalParticle(1));
	dRecoilWrapper = dStep0Wrapper->Get_FinalParticle(2);
}

#endif // DSelector_2eMissingProton_h
