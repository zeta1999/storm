/*
 * Formulas.h
 *
 *  Created on: 06.12.2012
 *      Author: chris
 */

#ifndef STORM_FORMULA_FORMULAS_H_
#define STORM_FORMULA_FORMULAS_H_

#include "modelchecker/ForwardDeclarations.h"

#include "And.h"
#include "Ap.h"
#include "BoundedUntil.h"
#include "BoundedNaryUntil.h"
#include "Next.h"
#include "Not.h"
#include "Or.h"
#include "ProbabilisticNoBoundOperator.h"
#include "ProbabilisticBoundOperator.h"

#include "Until.h"
#include "Eventually.h"
#include "Globally.h"
#include "BoundedEventually.h"

#include "InstantaneousReward.h"
#include "CumulativeReward.h"
#include "ReachabilityReward.h"
#include "RewardBoundOperator.h"
#include "RewardNoBoundOperator.h"
#include "SteadyStateReward.h"

#include "SteadyStateOperator.h"

#include "AbstractFormula.h"
#include "AbstractPathFormula.h"
#include "AbstractStateFormula.h"

// FIXME: Why is this needed? To me this makes no sense.
#include "modelchecker/DtmcPrctlModelChecker.h"

#endif /* STORM_FORMULA_FORMULAS_H_ */
