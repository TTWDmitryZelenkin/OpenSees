/* ****************************************************************** **
**    OpenSees - Open System for Earthquake Engineering Simulation    **
**          Pacific Earthquake Engineering Research Center            **
**                                                                    **
**                                                                    **
** (C) Copyright 2001, The Regents of the University of California    **
** All Rights Reserved.                                               **
**                                                                    **
** Commercial use of this program without express permission of the   **
** University of California, Berkeley, is strictly prohibited.  See   **
** file 'COPYRIGHT'  in main directory for information on usage and   **
** redistribution,  and for a DISCLAIMER OF ALL WARRANTIES.           **
**                                                                    **
** Developed by:                                                      **
**   Frank McKenna (fmckenna@ce.berkeley.edu)                         **
**   Gregory L. Fenves (fenves@ce.berkeley.edu)                       **
**   Filip C. Filippou (filippou@ce.berkeley.edu)                     **
**                                                                    **
** Reliability module developed by:                                   **
**   Terje Haukaas (haukaas@ce.berkeley.edu)                          **
**   Armen Der Kiureghian (adk@ce.berkeley.edu)                       **
**                                                                    **
** ****************************************************************** */
                                                                        
// $Revision: 1.2 $
// $Date: 2003-04-28 20:51:25 $
// $Source: /usr/local/cvs/OpenSees/SRC/reliability/analysis/analysis/SamplingAnalysis.cpp,v $


//
// Written by Terje Haukaas (haukaas@ce.berkeley.edu)
//

#include <SamplingAnalysis.h>
#include <ReliabilityDomain.h>
#include <ReliabilityAnalysis.h>
#include <LimitStateFunction.h>
#include <ProbabilityTransformation.h>
#include <NatafProbabilityTransformation.h>
#include <GFunEvaluator.h>
#include <BasicGFunEvaluator.h>
#include <RandomNumberGenerator.h>
#include <RandomVariable.h>
#include <NormalRV.h>
#include <Vector.h>
#include <Matrix.h>
#include <MatrixOperations.h>
#include <NormalRV.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iomanip>
#include <iostream>
using std::ifstream;
using std::ios;
using std::setw;
using std::setprecision;
using std::setiosflags;


SamplingAnalysis::SamplingAnalysis(	ReliabilityDomain *passedReliabilityDomain,
										ProbabilityTransformation *passedProbabilityTransformation,
										GFunEvaluator *passedGFunEvaluator,
										RandomNumberGenerator *passedRandomNumberGenerator,
										int passedNumberOfSimulations,
										double passedTargetCOV,
										double passedSamplingVariance,
										int passedPrintFlag,
										const char *passedFileName,
										Vector *pStartPoint,
										int passedAnalysisTypeTag)
:ReliabilityAnalysis()
{
	theReliabilityDomain = passedReliabilityDomain;
	theProbabilityTransformation = passedProbabilityTransformation;
	theGFunEvaluator = passedGFunEvaluator;
	theRandomNumberGenerator = passedRandomNumberGenerator;
	numberOfSimulations = passedNumberOfSimulations;
	targetCOV = passedTargetCOV;
	samplingVariance = passedSamplingVariance;
	printFlag = passedPrintFlag;
	fileName = new char[256];
	strcpy(fileName,passedFileName);
	startPoint = pStartPoint;
	analysisTypeTag = passedAnalysisTypeTag;
}




SamplingAnalysis::~SamplingAnalysis()
{
	if (fileName != 0)
		delete [] fileName;
}



int 
SamplingAnalysis::analyze(void)
{

	// Alert the user that the simulation analysis has started
	opserr << "Sampling Analysis is running ... " << endln;

	// Declaration of some of the data used in the algorithm
	double gFunctionValue;
	int result;
	int I, i, j;
	int k = 1;
	int seed = 1;
	double det_covariance;
	double phi;
	double h;
	double q;
	int numRV = theReliabilityDomain->getNumberOfRandomVariables();
	Matrix covariance(numRV, numRV);
	Matrix chol_covariance(numRV, numRV);
	Matrix inv_covariance(numRV, numRV);
	Vector startValues(numRV);
	Vector x(numRV);
	Vector z(numRV);
	Vector u(numRV);
	Vector randomArray(numRV);
	LimitStateFunction *theLimitStateFunction = 0;
	NormalRV *aStdNormRV = 0;
	aStdNormRV = new NormalRV(1,0.0,1.0,0.0);
	ofstream *outputFile = 0;
	bool failureHasOccured = false;

	
	// Check if computer ran out of memory
	if (aStdNormRV==0) {
		opserr << "SamplingAnalysis::analyze() - out of memory while instantiating internal objects." << endln;
		return -1;
	}

	
	// Establish covariance matrix
	for (i=0;  i<numRV;  i++) {
		for (j=0;  j<numRV;  j++) {
			if (i==j) {
				covariance(i,j) = samplingVariance;
			}
			else
				covariance(i,j) = 0.0;
		}
	}


	// Create object to do matrix operations on the covariance matrix
	MatrixOperations *theMatrixOperations = 0;
	theMatrixOperations = new MatrixOperations(covariance);
	if (theMatrixOperations == 0) {
		opserr << "SamplingAnalysis::analyze() - could not create" << endln
			<< " the object to perform matrix operations." << endln;
		return -1;
	}


	// Cholesky decomposition of covariance matrix
	result = theMatrixOperations->computeLowerCholesky();
	if (result < 0) {
		opserr << "SamplingAnalysis::analyze() - could not compute" << endln
			<< " the Cholesky decomposition of the covariance matrix." << endln;
		return -1;
	}
	chol_covariance = theMatrixOperations->getLowerCholesky();


	// Inverse of covariance matrix
	result = theMatrixOperations->computeInverse();
	if (result < 0) {
		opserr << "SamplingAnalysis::analyze() - could not compute" << endln
			<< " the inverse of the covariance matrix." << endln;
		return -1;
	}
	inv_covariance = theMatrixOperations->getInverse();


	// Compute the determinant, knowing that this is a diagonal matrix
	result = theMatrixOperations->computeTrace();
	if (result < 0) {
		opserr << "SamplingAnalysis::analyze() - could not compute" << endln
			<< " the trace of the covariance matrix." << endln;
		return -1;
	}
	det_covariance = theMatrixOperations->getTrace();
	

	// Pre-compute some factors to minimize computations inside simulation loop
	double pi = 3.14159265358979;
	double factor1 = 1.0 / ( pow((2.0*pi),((double)numRV/2.0)) );
	double factor2 = 1.0 / ( pow((2.0*pi),((double)numRV/2.0)) * sqrt(det_covariance) );


	// Number of limit-state functions
	int numLsf = theReliabilityDomain->getNumberOfLimitStateFunctions();




	Vector sum_q(numLsf);
	Vector sum_q_squared(numLsf);
	double var_qbar;
	double pfIn;
	double CovIn;
	char restartFileName[256];
	sprintf(restartFileName,"%s_%s","restart",fileName);

	if (analysisTypeTag == 1) {
		// Possible read data from file if this is a restart simulation
		if (printFlag == 2) {
			ifstream inputFile( restartFileName, ios::in );
			inputFile >> k;
			inputFile >> seed;
			if (k==1 && seed==1) {
			}
			else { 
				for (int i=1; i<=numLsf; i++) {
					inputFile >> pfIn;
					if (pfIn > 0.0) {
						failureHasOccured = true;
					}
					inputFile >> CovIn;
					sum_q(i-1) = pfIn*k;
					var_qbar = (CovIn*pfIn)*(CovIn*pfIn);
					if (k<1.0e-6) {
						opserr << "WARNING: Zero number of simulations read from restart file" << endln;
					}
					sum_q_squared(i-1) = k*( k*var_qbar + pow(sum_q(i-1)/k,2.0) );
				}
				k++;
			}
			inputFile.close();
		}
	}


	// Transform start point into standard normal space, 
	// unless it is the origin that is to be sampled around
	Vector startPointY(numRV);
	if (startPoint == 0) {
		// Do nothing; keep it as zero
	}
	else {
		result = theProbabilityTransformation->set_x(*startPoint);
		if (result < 0) {
			opserr << "SamplingAnalysis::analyze() - could not " << endln
				<< " set the x-vector for xu-transformation. " << endln;
			return -1;
		}
		result = theProbabilityTransformation->transform_x_to_u();
		if (result < 0) {
			opserr << "SamplingAnalysis::analyze() - could not " << endln
				<< " transform x to u. " << endln;
			return -1;
		}
		startPointY = theProbabilityTransformation->get_u();
	}


	// Initial declarations
	Vector cov_of_q_bar(numLsf);
	Vector q_bar(numLsf);
	Vector variance_of_q_bar(numLsf);
	Vector responseVariance(numLsf);
	Vector responseStdv(numLsf);
	Vector g_storage(numLsf);
	Vector sum_of_g_minus_mean_squared(numLsf);
	Matrix crossSums(numLsf,numLsf);
	Matrix responseCorrelation(numLsf,numLsf);
	char string[60];
	Vector pf(numLsf);
	Vector cov(numLsf);
	double govCov = 999.0;
	Vector temp1;
	double temp2;
	double denumerator;
	bool FEconvergence;


	bool isFirstSimulation = true;
	while( (k<=numberOfSimulations && govCov>targetCOV || k<=2) ) {

		// Keep the user posted
		if (printFlag == 1 || printFlag == 2) {
			opserr << "Simulation #" << k << ":" << endln;
		}

		
		// Create array of standard normal random numbers
		if (isFirstSimulation) {
			result = theRandomNumberGenerator->generate_nIndependentStdNormalNumbers(numRV,seed);
		}
		else {
			result = theRandomNumberGenerator->generate_nIndependentStdNormalNumbers(numRV);
		}
		seed = theRandomNumberGenerator->getSeed();
		if (result < 0) {
			opserr << "SamplingAnalysis::analyze() - could not generate" << endln
				<< " random numbers for simulation." << endln;
			return -1;
		}
		randomArray = theRandomNumberGenerator->getGeneratedNumbers();


		// Compute the point in standard normal space
		u = startPointY + chol_covariance * randomArray;

		// Transform into original space
		result = theProbabilityTransformation->set_u(u);
		if (result < 0) {
			opserr << "SamplingAnalysis::analyze() - could not " << endln
				<< " set the u-vector for xu-transformation. " << endln;
			return -1;
		}

		
		result = theProbabilityTransformation->transform_u_to_x();
		if (result < 0) {
			opserr << "SamplingAnalysis::analyze() - could not " << endln
				<< " transform u to x. " << endln;
			return -1;
		}
		x = theProbabilityTransformation->get_x();


		// Evaluate limit-state function
		FEconvergence = true;
		result = theGFunEvaluator->runGFunAnalysis(x);
		if (result < 0) {
			// In this case a failure happened during the analysis
			// Hence, register this as failure
			FEconvergence = false;
		}

		
		// Loop over number of limit-state functions
		for (int lsf=0; lsf<numLsf; lsf++ ) {


			// Set tag of "active" limit-state function
			theReliabilityDomain->setTagOfActiveLimitStateFunction(lsf+1);


			// Get value of limit-state function
			result = theGFunEvaluator->evaluateG(x);
			if (result < 0) {
				opserr << "SamplingAnalysis::analyze() - could not " << endln
					<< " tokenize limit-state function. " << endln;
				return -1;
			}
			gFunctionValue = theGFunEvaluator->getG();
			if (!FEconvergence) {
				gFunctionValue = -1.0;
			}


			
			// ESTIMATION OF FAILURE PROBABILITY
			if (analysisTypeTag == 1) {

				// Collect result of sampling
				if (gFunctionValue < 0.0) {
					I = 1;
					failureHasOccured = true;
				}
				else {
					I = 0;
				}


				// Compute values of joint distributions at the u-point
				phi = factor1 * exp( -0.5 * (u ^ u) );
				temp1 = inv_covariance ^ (u-startPointY);
				temp2 = temp1 ^ (u-startPointY);
				h   = factor2 * exp( -0.5 * temp2 );


				// Update sums
				q = I * phi / h;
				sum_q(lsf) = sum_q(lsf) + q;
				sum_q_squared(lsf) = sum_q_squared(lsf) + q*q;



				if (sum_q(lsf) > 0.0) {
					// Compute coefficient of variation (of pf)
					q_bar(lsf) = 1.0/(double)k * sum_q(lsf);
					variance_of_q_bar(lsf) = 1.0/(double)k * 
						( 1.0/(double)k * sum_q_squared(lsf) - (sum_q(lsf)/(double)k)*(sum_q(lsf)/(double)k));
					if (variance_of_q_bar(lsf) < 0.0) {
						variance_of_q_bar(lsf) = 0.0;
					}
					cov_of_q_bar(lsf) = sqrt(variance_of_q_bar(lsf)) / q_bar(lsf);
				}

			}
			else {
			// ESTIMATION OF RESPONSE STATISTICS

				// Now q=g and q_bar=mean
				q = gFunctionValue; 
				failureHasOccured = true;
				
				sum_q(lsf) = sum_q(lsf) + q;
				sum_q_squared(lsf) = sum_q_squared(lsf) + q*q;

				g_storage(lsf) = gFunctionValue;
				
				if (sum_q(lsf) > 0.0) {
					
					// Compute coefficient of variation (of mean)
					q_bar(lsf) = 1.0/(double)k * sum_q(lsf);
					variance_of_q_bar(lsf) = 1.0/(double)k * 
						( 1.0/(double)k * sum_q_squared(lsf) - (sum_q(lsf)/(double)k)*(sum_q(lsf)/(double)k));
					if (variance_of_q_bar(lsf) < 0.0) {
						variance_of_q_bar(lsf) = 0.0;
					}
					cov_of_q_bar(lsf) = sqrt(variance_of_q_bar(lsf)) / q_bar(lsf);

					// Compute variance and standard deviation
					if (k>1) {
						responseVariance(lsf) = 1.0/((double)k-1) * (  sum_q_squared(lsf) - 1.0/((double)k) * sum_q(lsf) * sum_q(lsf)  );
					}
					else {
						responseVariance(lsf) = 1.0;
					}

					if (responseVariance(lsf) <= 0.0) {
						opserr << "ERROR: Response variance of limit-state function number "<< lsf << endln
							<< " is zero! " << endln;
					}
					else {
						responseStdv(lsf) = sqrt(responseVariance(lsf));
					}
				}
			}

			// Keep the user posted
			if (printFlag == 1 || printFlag == 2) {
				sprintf(string," GFun #%d, estimate:%15.10f, cov:%15.10f",lsf+1,q_bar(lsf),cov_of_q_bar(lsf));
				opserr << string << endln;
			}
		}

		// Now all the limit-state functions have been looped over


		// Possibly compute correlation coefficient
		if (analysisTypeTag == 2) {

			for (int i=0; i<numLsf; i++) {
				for (int j=i+1; j<numLsf; j++) {

					crossSums(i,j) = crossSums(i,j) + g_storage(i) * g_storage(j);

					denumerator = 	(sum_q_squared(i)-1.0/(double)k*sum_q(i)*sum_q(i))
									*(sum_q_squared(j)-1.0/(double)k*sum_q(j)*sum_q(j));

					if (denumerator <= 0.0) {
						responseCorrelation(i,j) = 0.0;
					}
					else {
						responseCorrelation(i,j) = (crossSums(i,j)-1.0/(double)k*sum_q(i)*sum_q(j))
							/(sqrt(denumerator));
					}
				}
			}
		}

		
		// Compute governing coefficient of variation
		if (!failureHasOccured) {
			govCov = 999.0;
		}
		else {
			govCov = 0.0;
			for (int mmmm=0; mmmm<numLsf; mmmm++) {
				if (cov_of_q_bar(mmmm) > govCov) {
					govCov = cov_of_q_bar(mmmm);
				}
			}
		}

		
		// Make sure the cov isn't exactly zero; that could be the case if only failures
		// occur in cases where the 'q' remains 1
		if (govCov == 0.0) {
			govCov = 999.0;
		}


		// Print to the restart file, if requested. 
		if (printFlag == 2) {
			if (outputFile != 0) {
				delete outputFile;
			}
			outputFile = new ofstream( restartFileName, ios::out );
			(*outputFile) << k << endln;
			(*outputFile) << seed << endln;
			for (int lsf=0; lsf<numLsf; lsf++ ) {
				sprintf(string,"%15.10f  %15.10f",q_bar(lsf),cov_of_q_bar(lsf));
				(*outputFile) << string << " " << endln;
			}
		}

		// Increment k (the simulation number counter)
		k++;
		isFirstSimulation = false;

	}

	// Step 'k' back a step now that we went out
	k--;
	opserr << endln;


	// Delete possible 'new' objects
	if (outputFile != 0) {
		delete outputFile;
	}


	// Open output file
	ofstream resultsOutputFile( fileName, ios::out );


	if (!failureHasOccured) {
		opserr << "WARNING: Failure did not occur for any of the limit-state functions. " << endln;
	}

	
	for (int lsf=1; lsf<=numLsf; lsf++ ) {

		if ( q_bar(lsf-1) == 0.0 ) {

			resultsOutputFile << "#######################################################################" << endln;
			resultsOutputFile << "#  SAMPLING ANALYSIS RESULTS, LIMIT-STATEFUNCDTION NUMBER   "
				<<setiosflags(ios::left)<<setprecision(1)<<setw(4)<<lsf <<"      #" << endln;
			resultsOutputFile << "#                                                                     #" << endln;
			resultsOutputFile << "#  Failure did not occur, or zero response!                           #" << endln;
			resultsOutputFile << "#                                                                     #" << endln;
			resultsOutputFile << "#######################################################################" << endln << endln << endln;
		}
		else {

			// Some declarations
			double beta_sim, pf_sim, cov_sim;
			int num_sim;


			// Set tag of "active" limit-state function
			theReliabilityDomain->setTagOfActiveLimitStateFunction(lsf);


			// Get the limit-state function pointer
			theLimitStateFunction = 0;
			//lsf = theReliabilityDomain->getTagOfActiveLimitStateFunction();
			theLimitStateFunction = theReliabilityDomain->getLimitStateFunctionPtr(lsf);
			if (theLimitStateFunction == 0) {
				opserr << "SamplingAnalysis::analyze() - could not find" << endln
					<< " limit-state function with tag #" << lsf << "." << endln;
				return -1;
			}

		
			// Store results
			if (analysisTypeTag == 1) {
				beta_sim = -aStdNormRV->getInverseCDFvalue(q_bar(lsf-1));
				pf_sim	 = q_bar(lsf-1);
				cov_sim	 = cov_of_q_bar(lsf-1);
				num_sim  = k;
				theLimitStateFunction->SimulationReliabilityIndexBeta = beta_sim;
				theLimitStateFunction->SimulationProbabilityOfFailure_pfsim = pf_sim;
				theLimitStateFunction->CoefficientOfVariationOfPfFromSimulation = cov_sim;
				theLimitStateFunction->NumberOfSimulations = num_sim;
			}


			// Print results to the output file
			if (analysisTypeTag == 1) {
				resultsOutputFile << "#######################################################################" << endln;
				resultsOutputFile << "#  SAMPLING ANALYSIS RESULTS, LIMIT-STATE FUNCTION NUMBER   "
					<<setiosflags(ios::left)<<setprecision(1)<<setw(4)<<lsf <<"      #" << endln;
				resultsOutputFile << "#                                                                     #" << endln;
				resultsOutputFile << "#  Reliability index beta: ............................ " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<beta_sim 
					<< "  #" << endln;
				resultsOutputFile << "#  Estimated probability of failure pf_sim: ........... " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<pf_sim 
					<< "  #" << endln;
				resultsOutputFile << "#  Number of simulations: ............................. " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<num_sim 
					<< "  #" << endln;
				resultsOutputFile << "#  Coefficient of variation (of pf): .................. " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<cov_sim 
					<< "  #" << endln;
				resultsOutputFile << "#                                                                     #" << endln;
				resultsOutputFile << "#######################################################################" << endln << endln << endln;
			}
			else {
				resultsOutputFile << "#######################################################################" << endln;
				resultsOutputFile << "#  SAMPLING ANALYSIS RESULTS, LIMIT-STATE FUNCTION NUMBER   "
					<<setiosflags(ios::left)<<setprecision(1)<<setw(4)<<lsf <<"      #" << endln;
				resultsOutputFile << "#                                                                     #" << endln;
				resultsOutputFile << "#  Estimated mean: .................................... " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<q_bar(lsf-1) 
					<< "  #" << endln;
				resultsOutputFile << "#  Estimated standard deviation: ...................... " 
					<<setiosflags(ios::left)<<setprecision(5)<<setw(12)<<responseStdv(lsf-1) 
					<< "  #" << endln;
				resultsOutputFile << "#                                                                     #" << endln;
				resultsOutputFile << "#######################################################################" << endln << endln << endln;
			}
		}
	}

	if (analysisTypeTag == 2) {
		resultsOutputFile << "#######################################################################" << endln;
		resultsOutputFile << "#  RESPONSE CORRELATION COEFFICIENTS                                  #" << endln;
		resultsOutputFile << "#                                                                     #" << endln;
		if (numLsf <=1) {
			resultsOutputFile << "#  Only one limit-state function!                                     #" << endln;
		}
		else {
			resultsOutputFile << "#   gFun   gFun     Correlation                                       #" << endln;
			resultsOutputFile.setf(ios::fixed, ios::floatfield);
			for (i=0; i<numLsf; i++) {
				for (int j=i+1; j<numLsf; j++) {
					resultsOutputFile << "#    " <<setw(3)<<(i+1)<<"    "<<setw(3)<<(j+1)<<"     ";
					if (responseCorrelation(i,j)<0.0) { resultsOutputFile << "-"; }
					else { resultsOutputFile << " "; }
					resultsOutputFile <<setprecision(7)<<setw(11)<<fabs(responseCorrelation(i,j));
					resultsOutputFile << "                                      #" << endln;
				}
			}
		}
		resultsOutputFile << "#                                                                     #" << endln;
		resultsOutputFile << "#######################################################################" << endln << endln << endln;
	}


	// Print summary of results to screen (more here!!!)
	opserr << "Simulation Analysis completed." << endln;

	// Clean up
	resultsOutputFile.close();
	delete theMatrixOperations;
	delete aStdNormRV;

	return 0;
}

