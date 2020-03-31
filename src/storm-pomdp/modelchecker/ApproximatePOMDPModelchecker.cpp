#include "ApproximatePOMDPModelchecker.h"

#include <tuple>

#include <boost/algorithm/string.hpp>

#include "storm-pomdp/analysis/FormulaInformation.h"

#include "storm/utility/ConstantsComparator.h"
#include "storm/utility/NumberTraits.h"
#include "storm/utility/graph.h"
#include "storm/logic/Formulas.h"

#include "storm/models/sparse/Dtmc.h"
#include "storm/models/sparse/StandardRewardModel.h"
#include "storm/modelchecker/prctl/SparseDtmcPrctlModelChecker.h"
#include "storm/utility/vector.h"
#include "storm/modelchecker/results/CheckResult.h"
#include "storm/modelchecker/results/ExplicitQualitativeCheckResult.h"
#include "storm/modelchecker/results/ExplicitQuantitativeCheckResult.h"
#include "storm/modelchecker/hints/ExplicitModelCheckerHint.cpp"
#include "storm/api/properties.h"
#include "storm/api/export.h"
#include "storm-parsers/api/storm-parsers.h"
#include "storm-pomdp/builder/BeliefMdpExplorer.h"
#include "storm-pomdp/modelchecker/TrivialPomdpValueBoundsModelChecker.h"

#include "storm/utility/macros.h"
#include "storm/utility/SignalHandler.h"
#include "storm/exceptions/NotSupportedException.h"

namespace storm {
    namespace pomdp {
        namespace modelchecker {
            template<typename ValueType, typename RewardModelType>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::Options::Options() {
                initialGridResolution = 10;
                explorationThreshold = storm::utility::zero<ValueType>();
                doRefinement = true;
                refinementPrecision = storm::utility::convertNumber<ValueType>(1e-4);
                numericPrecision = storm::NumberTraits<ValueType>::IsExact ? storm::utility::zero<ValueType>() : storm::utility::convertNumber<ValueType>(1e-9);
                cacheSubsimplices = false;
            }
            template<typename ValueType, typename RewardModelType>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::Statistics::Statistics() :  overApproximationBuildAborted(false), underApproximationBuildAborted(false), aborted(false) {
                // intentionally left empty;
            }
            
            template<typename ValueType, typename RewardModelType>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::ApproximatePOMDPModelchecker(storm::models::sparse::Pomdp<ValueType, RewardModelType> const& pomdp, Options options) : pomdp(pomdp), options(options) {
                cc = storm::utility::ConstantsComparator<ValueType>(storm::utility::convertNumber<ValueType>(this->options.numericPrecision), false);
            }

            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<POMDPCheckResult<ValueType>> ApproximatePOMDPModelchecker<ValueType, RewardModelType>::check(storm::logic::Formula const& formula) {
                // Reset all collected statistics
                statistics = Statistics();
                std::unique_ptr<POMDPCheckResult<ValueType>> result;
                // Extract the relevant information from the formula
                auto formulaInfo = storm::pomdp::analysis::getFormulaInformation(pomdp, formula);
                
                // Compute some initial bounds on the values for each state of the pomdp
                auto initialPomdpValueBounds = TrivialPomdpValueBoundsModelChecker<storm::models::sparse::Pomdp<ValueType>>(pomdp).getValueBounds(formula, formulaInfo);
                
                if (formulaInfo.isNonNestedReachabilityProbability()) {
                    // FIXME: Instead of giving up, introduce a new observation for target states and make sink states absorbing.
                    STORM_LOG_THROW(formulaInfo.getTargetStates().observationClosed, storm::exceptions::NotSupportedException, "There are non-target states with the same observation as a target state. This is currently not supported");
                    if (!formulaInfo.getSinkStates().empty()) {
                        auto reachableFromSinkStates = storm::utility::graph::getReachableStates(pomdp.getTransitionMatrix(), formulaInfo.getSinkStates().states, formulaInfo.getSinkStates().states, ~formulaInfo.getSinkStates().states);
                        reachableFromSinkStates &= ~formulaInfo.getSinkStates().states;
                        STORM_LOG_THROW(reachableFromSinkStates.empty(), storm::exceptions::NotSupportedException, "There are sink states that can reach non-sink states. This is currently not supported");
                    }
                    if (options.doRefinement) {
                        result = refineReachability(formulaInfo.getTargetStates().observations, formulaInfo.minimize(), false);
                    } else {
                        result = computeReachabilityOTF(formulaInfo.getTargetStates().observations, formulaInfo.minimize(), false, initialPomdpValueBounds.lower, initialPomdpValueBounds.upper);
                    }
                } else if (formulaInfo.isNonNestedExpectedRewardFormula()) {
                    // FIXME: Instead of giving up, introduce a new observation for target states and make sink states absorbing.
                    STORM_LOG_THROW(formulaInfo.getTargetStates().observationClosed, storm::exceptions::NotSupportedException, "There are non-target states with the same observation as a target state. This is currently not supported");
                    if (options.doRefinement) {
                        result = refineReachability(formulaInfo.getTargetStates().observations, formulaInfo.minimize(), true);
                    } else {
                        // FIXME: pick the non-unique reward model here
                        STORM_LOG_THROW(pomdp.hasUniqueRewardModel(), storm::exceptions::NotSupportedException, "Non-unique reward models not implemented yet.");
                        result = computeReachabilityOTF(formulaInfo.getTargetStates().observations, formulaInfo.minimize(), true, initialPomdpValueBounds.lower, initialPomdpValueBounds.upper);
                    }
                } else {
                    STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "Unsupported formula '" << formula << "'.");
                }
                if (storm::utility::resources::isTerminate()) {
                    statistics.aborted = true;
                }
                return result;
            }
            
            template<typename ValueType, typename RewardModelType>
            void ApproximatePOMDPModelchecker<ValueType, RewardModelType>::printStatisticsToStream(std::ostream& stream) const {
                stream << "##### Grid Approximation Statistics ######" << std::endl;
                stream << "# Input model: " << std::endl;
                pomdp.printModelInformationToStream(stream);
                stream << "# Max. Number of states with same observation: " << pomdp.getMaxNrStatesWithSameObservation() << std::endl;
                
                if (statistics.aborted) {
                    stream << "# Computation aborted early" << std::endl;
                }
                
                // Refinement information:
                if (statistics.refinementSteps) {
                    stream << "# Number of refinement steps: " << statistics.refinementSteps.get() << std::endl;
                }
                
                // The overapproximation MDP:
                if (statistics.overApproximationStates) {
                    stream << "# Number of states in the ";
                    if (options.doRefinement) {
                        stream << "final ";
                    }
                    stream << "grid MDP for the over-approximation: ";
                    if (statistics.overApproximationBuildAborted) {
                        stream << ">=";
                    }
                    stream << statistics.overApproximationStates.get() << std::endl;
                    stream << "# Time spend for building the over-approx grid MDP(s): " << statistics.overApproximationBuildTime << std::endl;
                    stream << "# Time spend for checking the over-approx grid MDP(s): " << statistics.overApproximationCheckTime << std::endl;
                }
                
                // The underapproximation MDP:
                if (statistics.underApproximationStates) {
                    stream << "# Number of states in the ";
                    if (options.doRefinement) {
                        stream << "final ";
                    }
                    stream << "grid MDP for the under-approximation: ";
                    if (statistics.underApproximationBuildAborted) {
                        stream << ">=";
                    }
                    stream << statistics.underApproximationStates.get() << std::endl;
                    stream << "# Time spend for building the under-approx grid MDP(s): " << statistics.underApproximationBuildTime << std::endl;
                    stream << "# Time spend for checking the under-approx grid MDP(s): " << statistics.underApproximationCheckTime << std::endl;
                }

                stream << "##########################################" << std::endl;
            }
            
            std::shared_ptr<storm::logic::Formula const> createStandardProperty(bool min, bool computeRewards) {
                std::string propertyString = computeRewards ? "R" : "P";
                propertyString += min ? "min" : "max";
                propertyString += "=? [F \"target\"]";
                std::vector<storm::jani::Property> propertyVector = storm::api::parseProperties(propertyString);
                return storm::api::extractFormulasFromProperties(propertyVector).front();
            }
            
            template<typename ValueType>
            storm::modelchecker::CheckTask<storm::logic::Formula, ValueType> createStandardCheckTask(std::shared_ptr<storm::logic::Formula const>& property, std::vector<ValueType>&& hintVector) {
                //Note: The property should not run out of scope after calling this because the task only stores the property by reference.
                // Therefore, this method needs the property by reference (and not const reference)
                auto task = storm::api::createTask<ValueType>(property, false);
                if (!hintVector.empty()) {
                    auto hint = storm::modelchecker::ExplicitModelCheckerHint<ValueType>();
                    hint.setResultHint(std::move(hintVector));
                    auto hintPtr = std::make_shared<storm::modelchecker::ExplicitModelCheckerHint<ValueType>>(hint);
                    task.setHint(hintPtr);
                }
                return task;
            }
            
            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<POMDPCheckResult<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::refineReachability(std::set<uint32_t> const &targetObservations, bool min, bool computeRewards) {
                std::srand(time(NULL));
                // Compute easy upper and lower bounds
                storm::utility::Stopwatch underlyingWatch(true);
                // Compute the results on the underlying MDP as a basic overapproximation
                storm::models::sparse::StateLabeling underlyingMdpLabeling(pomdp.getStateLabeling());
                // TODO: Is the following really necessary
                underlyingMdpLabeling.addLabel("__goal__");
                std::vector<uint64_t> goalStates;
                for (auto const &targetObs : targetObservations) {
                    for (auto const &goalState : pomdp.getStatesWithObservation(targetObs)) {
                        underlyingMdpLabeling.addLabelToState("__goal__", goalState);
                    }
                }
                storm::models::sparse::Mdp<ValueType, RewardModelType> underlyingMdp(pomdp.getTransitionMatrix(), underlyingMdpLabeling, pomdp.getRewardModels());
                auto underlyingModel = std::static_pointer_cast<storm::models::sparse::Model<ValueType, RewardModelType>>(
                        std::make_shared<storm::models::sparse::Mdp<ValueType, RewardModelType>>(underlyingMdp));
                std::string initPropString = computeRewards ? "R" : "P";
                initPropString += min ? "min" : "max";
                initPropString += "=? [F \"__goal__\"]";
                std::vector<storm::jani::Property> propVector = storm::api::parseProperties(initPropString);
                std::shared_ptr<storm::logic::Formula const> underlyingProperty = storm::api::extractFormulasFromProperties(propVector).front();
                STORM_PRINT("Underlying MDP" << std::endl)
                if (computeRewards) {
                    underlyingMdp.addRewardModel("std", pomdp.getUniqueRewardModel());
                }
                underlyingMdp.printModelInformationToStream(std::cout);
                std::unique_ptr<storm::modelchecker::CheckResult> underlyingRes(
                        storm::api::verifyWithSparseEngine<ValueType>(underlyingModel, storm::api::createTask<ValueType>(underlyingProperty, false)));
                STORM_LOG_ASSERT(underlyingRes, "Result not exist.");
                underlyingRes->filter(storm::modelchecker::ExplicitQualitativeCheckResult(storm::storage::BitVector(underlyingMdp.getNumberOfStates(), true)));
                auto initialOverApproxMap = underlyingRes->asExplicitQuantitativeCheckResult<ValueType>().getValueMap();
                underlyingWatch.stop();

                storm::utility::Stopwatch positionalWatch(true);
                // we define some positional scheduler for the POMDP as a basic lower bound
                storm::storage::Scheduler<ValueType> pomdpScheduler(pomdp.getNumberOfStates());
                for (uint32_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                    auto obsStates = pomdp.getStatesWithObservation(obs);
                    // select a random action for all states with the same observation
                    uint64_t chosenAction = std::rand() % pomdp.getNumberOfChoices(obsStates.front());
                    for (auto const &state : obsStates) {
                        pomdpScheduler.setChoice(chosenAction, state);
                    }
                }
                auto underApproxModel = underlyingMdp.applyScheduler(pomdpScheduler, false);
                if (computeRewards) {
                    underApproxModel->restrictRewardModels({"std"});
                }
                STORM_PRINT("Random Positional Scheduler" << std::endl)
                underApproxModel->printModelInformationToStream(std::cout);
                std::unique_ptr<storm::modelchecker::CheckResult> underapproxRes(
                        storm::api::verifyWithSparseEngine<ValueType>(underApproxModel, storm::api::createTask<ValueType>(underlyingProperty, false)));
                STORM_LOG_ASSERT(underapproxRes, "Result not exist.");
                underapproxRes->filter(storm::modelchecker::ExplicitQualitativeCheckResult(storm::storage::BitVector(underApproxModel->getNumberOfStates(), true)));
                auto initialUnderApproxMap = underapproxRes->asExplicitQuantitativeCheckResult<ValueType>().getValueMap();
                positionalWatch.stop();

                STORM_PRINT("Pre-Processing Results: " << initialOverApproxMap[underlyingMdp.getInitialStates().getNextSetIndex(0)] << " // "
                                                       << initialUnderApproxMap[underApproxModel->getInitialStates().getNextSetIndex(0)] << std::endl)
                STORM_PRINT("Preprocessing Times: " << underlyingWatch << " / " << positionalWatch << std::endl)

                // Initialize the resolution mapping. For now, we always give all beliefs with the same observation the same resolution.
                // This can probably be improved (i.e. resolutions for single belief states)
                STORM_PRINT("Initial Resolution: " << options.initialGridResolution << std::endl)
                std::vector<uint64_t> observationResolutionVector(pomdp.getNrObservations(), options.initialGridResolution);
                std::set<uint32_t> changedObservations;
                uint64_t underApproxModelSize = 200;
                uint64_t refinementCounter = 1;
                STORM_PRINT("==============================" << std::endl << "Initial Computation" << std::endl << "------------------------------" << std::endl)
                std::shared_ptr<RefinementComponents<ValueType>> res = computeFirstRefinementStep(targetObservations, min, observationResolutionVector, computeRewards,
                                                                                                  {},
                                                                                                  {}, underApproxModelSize);
                if (res == nullptr) {
                    statistics.refinementSteps = 0;
                    return nullptr;
                }
                ValueType lastMinScore = storm::utility::infinity<ValueType>();
                while (refinementCounter < 1000 && ((!min && res->overApproxValue - res->underApproxValue > options.refinementPrecision) ||
                                                    (min && res->underApproxValue - res->overApproxValue > options.refinementPrecision))) {
                    if (storm::utility::resources::isTerminate()) {
                        break;
                    }
                    // TODO the actual refinement
                    // choose which observation(s) to refine
                    std::vector<ValueType> obsAccumulator(pomdp.getNrObservations(), storm::utility::zero<ValueType>());
                    std::vector<uint64_t> beliefCount(pomdp.getNrObservations(), 0);
                    bsmap_type::right_map::const_iterator underApproxStateBeliefIter = res->underApproxBeliefStateMap.right.begin();
                    while (underApproxStateBeliefIter != res->underApproxBeliefStateMap.right.end()) {
                        auto currentBelief = res->beliefList[underApproxStateBeliefIter->second];
                        beliefCount[currentBelief.observation] += 1;
                        bsmap_type::left_const_iterator overApproxBeliefStateIter = res->overApproxBeliefStateMap.left.find(underApproxStateBeliefIter->second);
                        if (overApproxBeliefStateIter != res->overApproxBeliefStateMap.left.end()) {
                            // If there is an over-approximate value for the belief, use it
                            auto diff = res->overApproxMap[overApproxBeliefStateIter->second] - res->underApproxMap[underApproxStateBeliefIter->first];
                            obsAccumulator[currentBelief.observation] += diff;
                        } else {
                            //otherwise, we approximate a value TODO this is critical, we have to think about it
                            auto overApproxValue = storm::utility::zero<ValueType>();
                            auto temp = computeSubSimplexAndLambdas(currentBelief.probabilities, observationResolutionVector[currentBelief.observation], pomdp.getNumberOfStates());
                            auto subSimplex = temp.first;
                            auto lambdas = temp.second;
                            for (size_t j = 0; j < lambdas.size(); ++j) {
                                if (!cc.isEqual(lambdas[j], storm::utility::zero<ValueType>())) {
                                    uint64_t approxId = getBeliefIdInVector(res->beliefList, currentBelief.observation, subSimplex[j]);
                                    bsmap_type::left_const_iterator approxIter = res->overApproxBeliefStateMap.left.find(approxId);
                                    if (approxIter != res->overApproxBeliefStateMap.left.end()) {
                                        overApproxValue += lambdas[j] * res->overApproxMap[approxIter->second];
                                    } else {
                                        overApproxValue += lambdas[j];
                                    }
                                }
                            }
                            obsAccumulator[currentBelief.observation] += overApproxValue - res->underApproxMap[underApproxStateBeliefIter->first];
                        }
                        ++underApproxStateBeliefIter;
                    }


                    /*for (uint64_t i = 0; i < obsAccumulator.size(); ++i) {
                        obsAccumulator[i] /= storm::utility::convertNumber<ValueType>(beliefCount[i]);
                    }*/
                    changedObservations.clear();

                    //TODO think about some other scoring methods
                    auto maxAvgDifference = *std::max_element(obsAccumulator.begin(), obsAccumulator.end());
                    //if (cc.isEqual(maxAvgDifference, lastMinScore) || cc.isLess(lastMinScore, maxAvgDifference)) {
                    lastMinScore = maxAvgDifference;
                    auto maxRes = *std::max_element(observationResolutionVector.begin(), observationResolutionVector.end());
                    STORM_PRINT("Set all to " << maxRes + 1 << std::endl)
                    for (uint64_t i = 0; i < pomdp.getNrObservations(); ++i) {
                        observationResolutionVector[i] = maxRes + 1;
                        changedObservations.insert(i);
                    }
                    /*} else {
                        lastMinScore = std::min(maxAvgDifference, lastMinScore);
                        STORM_PRINT("Max Score: " << maxAvgDifference << std::endl)
                        STORM_PRINT("Last Min Score: " << lastMinScore << std::endl)
                        //STORM_PRINT("Obs(beliefCount): Score " << std::endl << "-------------------------------------" << std::endl)
                        for (uint64_t i = 0; i < pomdp.getNrObservations(); ++i) {
                            //STORM_PRINT(i << "(" << beliefCount[i] << "): " << obsAccumulator[i])
                            if (cc.isEqual(obsAccumulator[i], maxAvgDifference)) {
                                //STORM_PRINT(" *** ")
                                observationResolutionVector[i] += 1;
                                changedObservations.insert(i);
                            }
                            //STORM_PRINT(std::endl)
                        }
                    }*/
                    if (underApproxModelSize < std::numeric_limits<uint64_t>::max() - 101) {
                        underApproxModelSize += 100;
                    }
                    STORM_PRINT(
                            "==============================" << std::endl << "Refinement Step " << refinementCounter << std::endl << "------------------------------" << std::endl)
                    res = computeRefinementStep(targetObservations, min, observationResolutionVector, computeRewards,
                                                res, changedObservations, initialOverApproxMap, initialUnderApproxMap, underApproxModelSize);
                    //storm::api::exportSparseModelAsDot(res->overApproxModelPtr, "oa_model_" + std::to_string(refinementCounter +1) + ".dot");
                    STORM_LOG_ERROR_COND((!min && cc.isLess(res->underApproxValue, res->overApproxValue)) || (min && cc.isLess(res->overApproxValue, res->underApproxValue)) ||
                                         cc.isEqual(res->underApproxValue, res->overApproxValue),
                                         "The value for the under-approximation is larger than the value for the over-approximation.");
                    ++refinementCounter;
                }
                statistics.refinementSteps = refinementCounter;
                if (min) {
                    return std::make_unique<POMDPCheckResult<ValueType>>(POMDPCheckResult<ValueType>{res->underApproxValue, res->overApproxValue});
                } else {
                    return std::make_unique<POMDPCheckResult<ValueType>>(POMDPCheckResult<ValueType>{res->overApproxValue, res->underApproxValue});
                }
            }

            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<POMDPCheckResult<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeReachabilityOTF(std::set<uint32_t> const &targetObservations, bool min,
                                                                                             bool computeRewards,
                                                                                             std::vector<ValueType> const& lowerPomdpValueBounds,
                                                                                             std::vector<ValueType> const& upperPomdpValueBounds,
                                                                                             uint64_t maxUaModelSize) {
                STORM_PRINT("Use On-The-Fly Grid Generation" << std::endl)
                std::vector<uint64_t> observationResolutionVector(pomdp.getNrObservations(), options.initialGridResolution);
                auto result = computeFirstRefinementStep(targetObservations, min, observationResolutionVector, computeRewards, lowerPomdpValueBounds,
                                                         upperPomdpValueBounds, maxUaModelSize);
                if (result == nullptr) {
                    return nullptr;
                }
                if (min) {
                    return std::make_unique<POMDPCheckResult<ValueType>>(POMDPCheckResult<ValueType>{result->underApproxValue, result->overApproxValue});
                } else {
                    return std::make_unique<POMDPCheckResult<ValueType>>(POMDPCheckResult<ValueType>{result->overApproxValue, result->underApproxValue});
                }
            }

            template <typename ValueType, typename BeliefType, typename SummandsType>
            ValueType getWeightedSum(BeliefType const& belief, SummandsType const& summands) {
                ValueType result = storm::utility::zero<ValueType>();
                for (auto const& entry : belief) {
                    result += storm::utility::convertNumber<ValueType>(entry.second) * storm::utility::convertNumber<ValueType>(summands.at(entry.first));
                }
                return result;
            }
            
            template<typename ValueType, typename RewardModelType>
            std::shared_ptr<RefinementComponents<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeFirstRefinementStep(std::set<uint32_t> const &targetObservations, bool min,
                                                                                                 std::vector<uint64_t> &observationResolutionVector,
                                                                                                 bool computeRewards,
                                                                                                 std::vector<ValueType> const& lowerPomdpValueBounds,
                                                                                                 std::vector<ValueType> const& upperPomdpValueBounds,
                                                                                                 uint64_t maxUaModelSize) {
                
                auto beliefManager = std::make_shared<storm::storage::BeliefManager<storm::models::sparse::Pomdp<ValueType>>>(pomdp, options.numericPrecision);
                if (computeRewards) {
                    beliefManager->setRewardModel(); // TODO: get actual name
                }
                
                statistics.overApproximationBuildTime.start();
                storm::builder::BeliefMdpExplorer<storm::models::sparse::Pomdp<ValueType>> explorer(beliefManager, lowerPomdpValueBounds, upperPomdpValueBounds);
                if (computeRewards) {
                    explorer.startNewExploration(storm::utility::zero<ValueType>());
                } else {
                    explorer.startNewExploration(storm::utility::one<ValueType>(), storm::utility::zero<ValueType>());
                }
                
                // Expand the beliefs to generate the grid on-the-fly
                if (options.explorationThreshold > storm::utility::zero<ValueType>()) {
                    STORM_PRINT("Exploration threshold: " << options.explorationThreshold << std::endl)
                }
                while (explorer.hasUnexploredState()) {
                    uint64_t currId = explorer.exploreNextState();
                    
                    uint32_t currObservation = beliefManager->getBeliefObservation(currId);
                    if (targetObservations.count(currObservation) != 0) {
                        explorer.setCurrentStateIsTarget();
                        explorer.addSelfloopTransition();
                    } else {
                        bool stopExploration = false;
                        if (storm::utility::abs<ValueType>(explorer.getUpperValueBoundAtCurrentState() - explorer.getLowerValueBoundAtCurrentState()) < options.explorationThreshold) {
                            stopExploration = true;
                            explorer.setCurrentStateIsTruncated();
                        }
                        for (uint64 action = 0, numActions = beliefManager->getBeliefNumberOfChoices(currId); action < numActions; ++action) {
                            ValueType truncationProbability = storm::utility::zero<ValueType>();
                            ValueType truncationValueBound = storm::utility::zero<ValueType>();
                            auto successorGridPoints = beliefManager->expandAndTriangulate(currId, action, observationResolutionVector);
                            for (auto const& successor : successorGridPoints) {
                                bool added = explorer.addTransitionToBelief(action, successor.first, successor.second, stopExploration);
                                if (!added) {
                                    STORM_LOG_ASSERT(stopExploration, "Didn't add a transition although exploration shouldn't be stopped.");
                                    // We did not explore this successor state. Get a bound on the "missing" value
                                    truncationProbability += successor.second;
                                    truncationValueBound += successor.second * (min ? explorer.computeLowerValueBoundAtBelief(successor.first) : explorer.computeUpperValueBoundAtBelief(successor.first));
                                }
                            }
                            if (stopExploration) {
                                if (computeRewards) {
                                    explorer.addTransitionsToExtraStates(action, truncationProbability);
                                } else {
                                    explorer.addTransitionsToExtraStates(action, truncationValueBound, truncationProbability - truncationValueBound);
                                }
                            }
                            if (computeRewards) {
                                // The truncationValueBound will be added on top of the reward introduced by the current belief state.
                                explorer.computeRewardAtCurrentState(action, truncationValueBound);
                            }
                        }
                    }
                    if (storm::utility::resources::isTerminate()) {
                        statistics.overApproximationBuildAborted = true;
                        break;
                    }
                }
                statistics.overApproximationStates = explorer.getCurrentNumberOfMdpStates();
                if (storm::utility::resources::isTerminate()) {
                    statistics.overApproximationBuildTime.stop();
                    return nullptr;
                }
                
                explorer.finishExploration();
                statistics.overApproximationBuildTime.stop();
                STORM_PRINT("Over Approximation MDP build took " << statistics.overApproximationBuildTime << " seconds." << std::endl);
                explorer.getExploredMdp()->printModelInformationToStream(std::cout);

                statistics.overApproximationCheckTime.start();
                explorer.computeValuesOfExploredMdp(min ? storm::solver::OptimizationDirection::Minimize : storm::solver::OptimizationDirection::Maximize);
                statistics.overApproximationCheckTime.stop();

                STORM_PRINT("Time Overapproximation: " <<  statistics.overApproximationCheckTime << " seconds." << std::endl);
                STORM_PRINT("Over-Approximation Result: " << explorer.getComputedValueAtInitialState() << std::endl);
                
                //auto underApprox = weightedSumUnderMap[initialBelief.id];
                auto underApproxComponents = computeUnderapproximation(beliefManager, targetObservations, min, computeRewards, maxUaModelSize, lowerPomdpValueBounds, upperPomdpValueBounds);
                if (storm::utility::resources::isTerminate() && !underApproxComponents) {
                    // TODO: return other components needed for refinement.
                    //return std::make_unique<RefinementComponents<ValueType>>(RefinementComponents<ValueType>{modelPtr, overApprox, 0, overApproxResultMap, {}, beliefList, beliefGrid, beliefIsTarget, beliefStateMap, {}, initialBelief.id});
                    //return std::make_unique<RefinementComponents<ValueType>>(RefinementComponents<ValueType>{modelPtr, overApprox, 0, overApproxResultMap, {}, {}, {}, {}, beliefStateMap, {}, beliefManager->getInitialBelief()});
                }
                
                STORM_PRINT("Under-Approximation Result: " << underApproxComponents->underApproxValue << std::endl);
                /* TODO: return other components needed for refinement.
                return std::make_unique<RefinementComponents<ValueType>>(
                        RefinementComponents<ValueType>{modelPtr, overApprox, underApproxComponents->underApproxValue, overApproxResultMap,
                                                        underApproxComponents->underApproxMap, beliefList, beliefGrid, beliefIsTarget, beliefStateMap,
                                                        underApproxComponents->underApproxBeliefStateMap, initialBelief.id});
                */
                return std::make_unique<RefinementComponents<ValueType>>(RefinementComponents<ValueType>{explorer.getExploredMdp(), explorer.getComputedValueAtInitialState(), underApproxComponents->underApproxValue, {},
                                                                                                         underApproxComponents->underApproxMap, {}, {}, {}, {}, underApproxComponents->underApproxBeliefStateMap, beliefManager->getInitialBelief()});

            }

            template<typename ValueType, typename RewardModelType>
            std::shared_ptr<RefinementComponents<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeRefinementStep(std::set<uint32_t> const &targetObservations, bool min,
                                                                                            std::vector<uint64_t> &observationResolutionVector,
                                                                                            bool computeRewards,
                                                                                            std::shared_ptr<RefinementComponents<ValueType>> refinementComponents,
                                                                                            std::set<uint32_t> changedObservations,
                                                                                            boost::optional<std::map<uint64_t, ValueType>> overApproximationMap,
                                                                                            boost::optional<std::map<uint64_t, ValueType>> underApproximationMap,
                                                                                            uint64_t maxUaModelSize) {
                bool initialBoundMapsSet = overApproximationMap && underApproximationMap;
                std::map<uint64_t, ValueType> initialOverMap;
                std::map<uint64_t, ValueType> initialUnderMap;
                if (initialBoundMapsSet) {
                    initialOverMap = overApproximationMap.value();
                    initialUnderMap = underApproximationMap.value();
                }
                // Note that a persistent cache is not support by the current data structure. The resolution for the given belief also has to be stored somewhere to cache effectively
                std::map<uint64_t, std::vector<std::map<uint64_t, ValueType>>> subSimplexCache;
                std::map<uint64_t, std::vector<ValueType>> lambdaCache;

                // Map to save the weighted values resulting from the initial preprocessing for newly added beliefs / indices in beliefSpace
                std::map<uint64_t, ValueType> weightedSumOverMap;
                std::map<uint64_t, ValueType> weightedSumUnderMap;

                statistics.overApproximationBuildTime.start();

                uint64_t nextBeliefId = refinementComponents->beliefList.size();
                uint64_t nextStateId = refinementComponents->overApproxModelPtr->getNumberOfStates();
                std::set<uint64_t> relevantStates;
                for (auto const &iter : refinementComponents->overApproxBeliefStateMap.left) {
                    auto currentBelief = refinementComponents->beliefList[iter.first];
                    if (changedObservations.find(currentBelief.observation) != changedObservations.end()) {
                        relevantStates.insert(iter.second);
                    }
                }

                std::set<std::pair<uint64_t, uint64_t>> statesAndActionsToCheck;
                for (uint64_t state = 0; state < refinementComponents->overApproxModelPtr->getNumberOfStates(); ++state) {
                    for (uint_fast64_t row = 0; row < refinementComponents->overApproxModelPtr->getTransitionMatrix().getRowGroupSize(state); ++row) {
                        for (typename storm::storage::SparseMatrix<ValueType>::const_iterator itEntry = refinementComponents->overApproxModelPtr->getTransitionMatrix().getRow(
                                state, row).begin();
                             itEntry != refinementComponents->overApproxModelPtr->getTransitionMatrix().getRow(state, row).end(); ++itEntry) {
                            if (relevantStates.find(itEntry->getColumn()) != relevantStates.end()) {
                                statesAndActionsToCheck.insert(std::make_pair(state, row));
                                break;
                            }
                        }
                    }
                }

                std::deque<uint64_t> beliefsToBeExpanded;

                std::map<std::pair<uint64_t, uint64_t>, std::map<uint64_t, ValueType>> transitionsStateActionPair;
                for (auto const &stateActionPair : statesAndActionsToCheck) {
                    auto currId = refinementComponents->overApproxBeliefStateMap.right.at(stateActionPair.first);
                    auto action = stateActionPair.second;
                    std::map<uint32_t, ValueType> actionObservationProbabilities = computeObservationProbabilitiesAfterAction(refinementComponents->beliefList[currId],
                                                                                                                              action);
                    std::map<uint64_t, ValueType> transitionInActionBelief;
                    for (auto iter = actionObservationProbabilities.begin(); iter != actionObservationProbabilities.end(); ++iter) {
                        uint32_t observation = iter->first;
                        uint64_t idNextBelief = getBeliefAfterActionAndObservation(refinementComponents->beliefList, refinementComponents->beliefIsTarget,
                                                                                   targetObservations, refinementComponents->beliefList[currId], action, observation, nextBeliefId);
                        nextBeliefId = refinementComponents->beliefList.size();
                        //Triangulate here and put the possibly resulting belief in the grid
                        std::vector<std::map<uint64_t, ValueType>> subSimplex;
                        std::vector<ValueType> lambdas;
                        //TODO add caching
                        if (options.cacheSubsimplices && subSimplexCache.count(idNextBelief) > 0) {
                            subSimplex = subSimplexCache[idNextBelief];
                            lambdas = lambdaCache[idNextBelief];
                        } else {
                            auto temp = computeSubSimplexAndLambdas(refinementComponents->beliefList[idNextBelief].probabilities,
                                                                    observationResolutionVector[refinementComponents->beliefList[idNextBelief].observation],
                                                                    pomdp.getNumberOfStates());
                            subSimplex = temp.first;
                            lambdas = temp.second;
                            if (options.cacheSubsimplices) {
                                subSimplexCache[idNextBelief] = subSimplex;
                                lambdaCache[idNextBelief] = lambdas;
                            }
                        }
                        for (size_t j = 0; j < lambdas.size(); ++j) {
                            if (!cc.isEqual(lambdas[j], storm::utility::zero<ValueType>())) {
                                auto approxId = getBeliefIdInVector(refinementComponents->beliefGrid, observation, subSimplex[j]);
                                if (approxId == uint64_t(-1)) {
                                    // if the triangulated belief was not found in the list, we place it in the grid and add it to the work list
                                    storm::pomdp::Belief<ValueType> gridBelief = {nextBeliefId, observation, subSimplex[j]};
                                    refinementComponents->beliefList.push_back(gridBelief);
                                    refinementComponents->beliefGrid.push_back(gridBelief);
                                    refinementComponents->beliefIsTarget.push_back(targetObservations.find(observation) != targetObservations.end());
                                    // compute overapproximate value using MDP result map
                                    if (initialBoundMapsSet) {
                                        auto tempWeightedSumOver = storm::utility::zero<ValueType>();
                                        auto tempWeightedSumUnder = storm::utility::zero<ValueType>();
                                        for (uint64_t i = 0; i < subSimplex[j].size(); ++i) {
                                            tempWeightedSumOver += subSimplex[j][i] * storm::utility::convertNumber<ValueType>(initialOverMap[i]);
                                            tempWeightedSumUnder += subSimplex[j][i] * storm::utility::convertNumber<ValueType>(initialUnderMap[i]);
                                        }
                                        weightedSumOverMap[nextBeliefId] = tempWeightedSumOver;
                                        weightedSumUnderMap[nextBeliefId] = tempWeightedSumUnder;
                                    }
                                    beliefsToBeExpanded.push_back(nextBeliefId);
                                    refinementComponents->overApproxBeliefStateMap.insert(bsmap_type::value_type(nextBeliefId, nextStateId));
                                    transitionInActionBelief[nextStateId] = iter->second * lambdas[j];
                                    ++nextBeliefId;
                                    ++nextStateId;
                                } else {
                                    transitionInActionBelief[refinementComponents->overApproxBeliefStateMap.left.at(approxId)] = iter->second * lambdas[j];
                                }
                            }
                        }
                    }
                    if (!transitionInActionBelief.empty()) {
                        transitionsStateActionPair[stateActionPair] = transitionInActionBelief;
                    }
                }

                std::set<uint64_t> stoppedExplorationStateSet;

                // Expand newly added beliefs
                while (!beliefsToBeExpanded.empty()) {
                    uint64_t currId = beliefsToBeExpanded.front();
                    beliefsToBeExpanded.pop_front();
                    bool isTarget = refinementComponents->beliefIsTarget[currId];

                    if (initialBoundMapsSet &&
                        cc.isLess(weightedSumOverMap[currId] - weightedSumUnderMap[currId], storm::utility::convertNumber<ValueType>(options.explorationThreshold))) {
                        STORM_PRINT("Stop Exploration in State " << refinementComponents->overApproxBeliefStateMap.left.at(currId) << " with Value " << weightedSumOverMap[currId]
                                                                 << std::endl)
                        transitionsStateActionPair[std::make_pair(refinementComponents->overApproxBeliefStateMap.left.at(currId), 0)] = {{1, weightedSumOverMap[currId]},
                                                                                                                                         {0, storm::utility::one<ValueType>() -
                                                                                                                                             weightedSumOverMap[currId]}};
                        stoppedExplorationStateSet.insert(refinementComponents->overApproxBeliefStateMap.left.at(currId));
                        continue;
                    }

                    if (isTarget) {
                        // Depending on whether we compute rewards, we select the right initial result
                        // MDP stuff
                        transitionsStateActionPair[std::make_pair(refinementComponents->overApproxBeliefStateMap.left.at(currId), 0)] =
                                {{refinementComponents->overApproxBeliefStateMap.left.at(currId), storm::utility::one<ValueType>()}};
                    } else {
                        uint64_t representativeState = pomdp.getStatesWithObservation(refinementComponents->beliefList[currId].observation).front();
                        uint64_t numChoices = pomdp.getNumberOfChoices(representativeState);
                        std::vector<ValueType> actionRewardsInState(numChoices);

                        for (uint64_t action = 0; action < numChoices; ++action) {
                            std::map<uint32_t, ValueType> actionObservationProbabilities = computeObservationProbabilitiesAfterAction(refinementComponents->beliefList[currId], action);
                            std::map<uint64_t, ValueType> transitionInActionBelief;
                            for (auto iter = actionObservationProbabilities.begin(); iter != actionObservationProbabilities.end(); ++iter) {
                                uint32_t observation = iter->first;
                                // THIS CALL IS SLOW
                                // TODO speed this up
                                uint64_t idNextBelief = getBeliefAfterActionAndObservation(refinementComponents->beliefList, refinementComponents->beliefIsTarget,
                                                                                           targetObservations, refinementComponents->beliefList[currId], action, observation,
                                                                                           nextBeliefId);
                                nextBeliefId = refinementComponents->beliefList.size();
                                //Triangulate here and put the possibly resulting belief in the grid
                                std::vector<std::map<uint64_t, ValueType>> subSimplex;
                                std::vector<ValueType> lambdas;

                                if (options.cacheSubsimplices && subSimplexCache.count(idNextBelief) > 0) {
                                    subSimplex = subSimplexCache[idNextBelief];
                                    lambdas = lambdaCache[idNextBelief];
                                } else {
                                    auto temp = computeSubSimplexAndLambdas(refinementComponents->beliefList[idNextBelief].probabilities,
                                                                            observationResolutionVector[refinementComponents->beliefList[idNextBelief].observation],
                                                                            pomdp.getNumberOfStates());
                                    subSimplex = temp.first;
                                    lambdas = temp.second;
                                    if (options.cacheSubsimplices) {
                                        subSimplexCache[idNextBelief] = subSimplex;
                                        lambdaCache[idNextBelief] = lambdas;
                                    }
                                }

                                for (size_t j = 0; j < lambdas.size(); ++j) {
                                    if (!cc.isEqual(lambdas[j], storm::utility::zero<ValueType>())) {
                                        auto approxId = getBeliefIdInVector(refinementComponents->beliefGrid, observation, subSimplex[j]);
                                        if (approxId == uint64_t(-1)) {
                                            // if the triangulated belief was not found in the list, we place it in the grid and add it to the work list
                                            storm::pomdp::Belief<ValueType> gridBelief = {nextBeliefId, observation, subSimplex[j]};
                                            refinementComponents->beliefList.push_back(gridBelief);
                                            refinementComponents->beliefGrid.push_back(gridBelief);
                                            refinementComponents->beliefIsTarget.push_back(targetObservations.find(observation) != targetObservations.end());
                                            // compute overapproximate value using MDP result map
                                            if (initialBoundMapsSet) {
                                                auto tempWeightedSumOver = storm::utility::zero<ValueType>();
                                                auto tempWeightedSumUnder = storm::utility::zero<ValueType>();
                                                for (uint64_t i = 0; i < subSimplex[j].size(); ++i) {
                                                    tempWeightedSumOver += subSimplex[j][i] * storm::utility::convertNumber<ValueType>(initialOverMap[i]);
                                                    tempWeightedSumUnder += subSimplex[j][i] * storm::utility::convertNumber<ValueType>(initialUnderMap[i]);
                                                }
                                                weightedSumOverMap[nextBeliefId] = tempWeightedSumOver;
                                                weightedSumUnderMap[nextBeliefId] = tempWeightedSumUnder;
                                            }
                                            beliefsToBeExpanded.push_back(nextBeliefId);
                                            refinementComponents->overApproxBeliefStateMap.insert(bsmap_type::value_type(nextBeliefId, nextStateId));
                                            transitionInActionBelief[nextStateId] = iter->second * lambdas[j];
                                            ++nextBeliefId;
                                            ++nextStateId;
                                        } else {
                                            transitionInActionBelief[refinementComponents->overApproxBeliefStateMap.left.at(approxId)] = iter->second * lambdas[j];
                                        }
                                    }
                                }
                            }
                            if (!transitionInActionBelief.empty()) {
                                transitionsStateActionPair[std::make_pair(refinementComponents->overApproxBeliefStateMap.left.at(currId), action)] = transitionInActionBelief;
                            }
                        }
                    }
                    if (storm::utility::resources::isTerminate()) {
                        statistics.overApproximationBuildAborted = true;
                        break;
                    }
                }

                statistics.overApproximationStates = nextStateId;
                if (storm::utility::resources::isTerminate()) {
                    statistics.overApproximationBuildTime.stop();
                    // Return the result from the old refinement step
                    return refinementComponents;
                }
                storm::models::sparse::StateLabeling mdpLabeling(nextStateId);
                mdpLabeling.addLabel("init");
                mdpLabeling.addLabel("target");
                mdpLabeling.addLabelToState("init", refinementComponents->overApproxBeliefStateMap.left.at(refinementComponents->initialBeliefId));
                mdpLabeling.addLabelToState("target", 1);
                uint_fast64_t currentRow = 0;
                uint_fast64_t currentRowGroup = 0;
                storm::storage::SparseMatrixBuilder<ValueType> smb(0, nextStateId, 0, false, true);
                auto oldTransitionMatrix = refinementComponents->overApproxModelPtr->getTransitionMatrix();
                smb.newRowGroup(currentRow);
                smb.addNextValue(currentRow, 0, storm::utility::one<ValueType>());
                ++currentRow;
                ++currentRowGroup;
                smb.newRowGroup(currentRow);
                smb.addNextValue(currentRow, 1, storm::utility::one<ValueType>());
                ++currentRow;
                ++currentRowGroup;
                for (uint64_t state = 2; state < nextStateId; ++state) {
                    smb.newRowGroup(currentRow);
                    //STORM_PRINT("Loop State: " << state << std::endl)
                    uint64_t numChoices = pomdp.getNumberOfChoices(
                            pomdp.getStatesWithObservation(refinementComponents->beliefList[refinementComponents->overApproxBeliefStateMap.right.at(state)].observation).front());
                    bool isTarget = refinementComponents->beliefIsTarget[refinementComponents->overApproxBeliefStateMap.right.at(state)];
                    for (uint64_t action = 0; action < numChoices; ++action) {
                        if (transitionsStateActionPair.find(std::make_pair(state, action)) == transitionsStateActionPair.end()) {
                            for (auto const &entry : oldTransitionMatrix.getRow(state, action)) {
                                smb.addNextValue(currentRow, entry.getColumn(), entry.getValue());
                            }
                        } else {
                            for (auto const &iter : transitionsStateActionPair[std::make_pair(state, action)]) {
                                smb.addNextValue(currentRow, iter.first, iter.second);
                            }
                        }
                        ++currentRow;
                        if (isTarget) {
                            // If the state is a target, we only have one action, thus we add the target label and stop the iteration
                            mdpLabeling.addLabelToState("target", state);
                            break;
                        }
                        if (stoppedExplorationStateSet.find(state) != stoppedExplorationStateSet.end()) {
                            break;
                        }
                    }
                    ++currentRowGroup;
                }
                storm::storage::sparse::ModelComponents<ValueType, RewardModelType> modelComponents(smb.build(), mdpLabeling);
                storm::models::sparse::Mdp<ValueType, RewardModelType> overApproxMdp(modelComponents);
                if (computeRewards) {
                    storm::models::sparse::StandardRewardModel<ValueType> mdpRewardModel(boost::none, std::vector<ValueType>(modelComponents.transitionMatrix.getRowCount()));
                    for (auto const &iter : refinementComponents->overApproxBeliefStateMap.left) {
                        auto currentBelief = refinementComponents->beliefList[iter.first];
                        auto representativeState = pomdp.getStatesWithObservation(currentBelief.observation).front();
                        for (uint64_t action = 0; action < overApproxMdp.getNumberOfChoices(iter.second); ++action) {
                            // Add the reward
                            mdpRewardModel.setStateActionReward(overApproxMdp.getChoiceIndex(storm::storage::StateActionPair(iter.second, action)),
                                                                getRewardAfterAction(pomdp.getChoiceIndex(storm::storage::StateActionPair(representativeState, action)),
                                                                                     currentBelief));
                        }
                    }
                    overApproxMdp.addRewardModel("std", mdpRewardModel);
                    overApproxMdp.restrictRewardModels(std::set<std::string>({"std"}));
                }
                overApproxMdp.printModelInformationToStream(std::cout);
                statistics.overApproximationBuildTime.stop();
                STORM_PRINT("Over Approximation MDP build took " << statistics.overApproximationBuildTime << " seconds." << std::endl);
                
                auto model = std::make_shared<storm::models::sparse::Mdp<ValueType, RewardModelType>>(overApproxMdp);
                auto modelPtr = std::static_pointer_cast<storm::models::sparse::Model<ValueType, RewardModelType>>(model);
                std::string propertyString = computeRewards ? "R" : "P";
                propertyString += min ? "min" : "max";
                propertyString += "=? [F \"target\"]";
                std::vector<storm::jani::Property> propertyVector = storm::api::parseProperties(propertyString);
                std::shared_ptr<storm::logic::Formula const> property = storm::api::extractFormulasFromProperties(propertyVector).front();
                auto task = storm::api::createTask<ValueType>(property, false);
                statistics.overApproximationCheckTime.start();
                std::unique_ptr<storm::modelchecker::CheckResult> res(storm::api::verifyWithSparseEngine<ValueType>(model, task));
                statistics.overApproximationCheckTime.stop();
                if (storm::utility::resources::isTerminate() && !res) {
                    return refinementComponents; // Return the result from the previous iteration
                }
                STORM_PRINT("Time Overapproximation: " << statistics.overApproximationCheckTime << std::endl)
                STORM_LOG_ASSERT(res, "Result not exist.");
                res->filter(storm::modelchecker::ExplicitQualitativeCheckResult(storm::storage::BitVector(overApproxMdp.getNumberOfStates(), true)));
                auto overApproxResultMap = res->asExplicitQuantitativeCheckResult<ValueType>().getValueMap();
                auto overApprox = overApproxResultMap[refinementComponents->overApproxBeliefStateMap.left.at(refinementComponents->initialBeliefId)];

                //auto underApprox = weightedSumUnderMap[initialBelief.id];
                auto underApproxComponents = computeUnderapproximation(refinementComponents->beliefList, refinementComponents->beliefIsTarget, targetObservations,
                                                                       refinementComponents->initialBeliefId, min, computeRewards, maxUaModelSize);
                STORM_PRINT("Over-Approximation Result: " << overApprox << std::endl);
                if (storm::utility::resources::isTerminate() && !underApproxComponents) {
                    return std::make_unique<RefinementComponents<ValueType>>(
                        RefinementComponents<ValueType>{modelPtr, overApprox, refinementComponents->underApproxValue, overApproxResultMap, {}, refinementComponents->beliefList, refinementComponents->beliefGrid, refinementComponents->beliefIsTarget, refinementComponents->overApproxBeliefStateMap, {}, refinementComponents->initialBeliefId});
                }
                STORM_PRINT("Under-Approximation Result: " << underApproxComponents->underApproxValue << std::endl);

                return std::make_shared<RefinementComponents<ValueType>>(
                        RefinementComponents<ValueType>{modelPtr, overApprox, underApproxComponents->underApproxValue, overApproxResultMap,
                                                        underApproxComponents->underApproxMap, refinementComponents->beliefList, refinementComponents->beliefGrid,
                                                        refinementComponents->beliefIsTarget, refinementComponents->overApproxBeliefStateMap,
                                                        underApproxComponents->underApproxBeliefStateMap, refinementComponents->initialBeliefId});
            }

            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<POMDPCheckResult<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeReachabilityRewardOTF(std::set<uint32_t> const &targetObservations, bool min) {
                std::vector<uint64_t> observationResolutionVector(pomdp.getNrObservations(), options.initialGridResolution);
              //  return computeReachabilityOTF(targetObservations, min, observationResolutionVector, true);
            }

            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<POMDPCheckResult<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeReachabilityProbabilityOTF(std::set<uint32_t> const &targetObservations, bool min) {
                std::vector<uint64_t> observationResolutionVector(pomdp.getNrObservations(), options.initialGridResolution);
              //  return computeReachabilityOTF(targetObservations, min, observationResolutionVector, false);
            }
            
            
            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<UnderApproxComponents<ValueType, RewardModelType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeUnderapproximation(std::vector<storm::pomdp::Belief<ValueType>> &beliefList,
                                                                                                std::vector<bool> &beliefIsTarget,
                                                                                                std::set<uint32_t> const &targetObservations,
                                                                                                uint64_t initialBeliefId, bool min,
                                                                                                bool computeRewards, uint64_t maxModelSize) {
                std::set<uint64_t> visitedBelieves;
                std::deque<uint64_t> beliefsToBeExpanded;
                bsmap_type beliefStateMap;
                std::vector<std::vector<std::map<uint64_t, ValueType>>> transitions = {{{{0, storm::utility::one<ValueType>()}}},
                                                                                       {{{1, storm::utility::one<ValueType>()}}}};
                std::vector<uint64_t> targetStates = {1};

                uint64_t stateId = 2;
                beliefStateMap.insert(bsmap_type::value_type(initialBeliefId, stateId));
                ++stateId;
                uint64_t nextId = beliefList.size();
                uint64_t counter = 0;

                statistics.underApproximationBuildTime.start();
                // Expand the believes
                visitedBelieves.insert(initialBeliefId);
                beliefsToBeExpanded.push_back(initialBeliefId);
                while (!beliefsToBeExpanded.empty()) {
                    //TODO think of other ways to stop exploration besides model size
                    auto currentBeliefId = beliefsToBeExpanded.front();
                    uint64_t numChoices = pomdp.getNumberOfChoices(pomdp.getStatesWithObservation(beliefList[currentBeliefId].observation).front());
                    // for targets, we only consider one action with one transition
                    if (beliefIsTarget[currentBeliefId]) {
                        // add a self-loop to target states
                        targetStates.push_back(beliefStateMap.left.at(currentBeliefId));
                        transitions.push_back({{{beliefStateMap.left.at(currentBeliefId), storm::utility::one<ValueType>()}}});
                    } else if (counter > maxModelSize) {
                        transitions.push_back({{{0, storm::utility::one<ValueType>()}}});
                    } else {
                        // Iterate over all actions and add the corresponding transitions
                        std::vector<std::map<uint64_t, ValueType>> actionTransitionStorage;
                        //TODO add a way to extract the actions from the over-approx and use them here?
                        for (uint64_t action = 0; action < numChoices; ++action) {
                            std::map<uint64_t, ValueType> transitionsInStateWithAction;
                            std::map<uint32_t, ValueType> observationProbabilities = computeObservationProbabilitiesAfterAction(beliefList[currentBeliefId], action);
                            for (auto iter = observationProbabilities.begin(); iter != observationProbabilities.end(); ++iter) {
                                uint32_t observation = iter->first;
                                uint64_t nextBeliefId = getBeliefAfterActionAndObservation(beliefList, beliefIsTarget, targetObservations, beliefList[currentBeliefId],
                                                                                           action,
                                                                                           observation, nextId);
                                nextId = beliefList.size();
                                if (visitedBelieves.insert(nextBeliefId).second) {
                                    beliefStateMap.insert(bsmap_type::value_type(nextBeliefId, stateId));
                                    ++stateId;
                                    beliefsToBeExpanded.push_back(nextBeliefId);
                                    ++counter;
                                }
                                transitionsInStateWithAction[beliefStateMap.left.at(nextBeliefId)] = iter->second;
                            }
                            actionTransitionStorage.push_back(transitionsInStateWithAction);
                        }
                        transitions.push_back(actionTransitionStorage);
                    }
                    beliefsToBeExpanded.pop_front();
                    if (storm::utility::resources::isTerminate()) {
                        statistics.underApproximationBuildAborted = true;
                        break;
                    }
                }
                statistics.underApproximationStates = transitions.size();
                if (storm::utility::resources::isTerminate()) {
                    statistics.underApproximationBuildTime.stop();
                    return nullptr;
                }
                
                storm::models::sparse::StateLabeling labeling(transitions.size());
                labeling.addLabel("init");
                labeling.addLabel("target");
                labeling.addLabelToState("init", 0);
                for (auto targetState : targetStates) {
                    labeling.addLabelToState("target", targetState);
                }

                std::shared_ptr<storm::models::sparse::Model<ValueType, RewardModelType>> model;
                auto transitionMatrix = buildTransitionMatrix(transitions);
                if (transitionMatrix.getRowCount() == transitionMatrix.getRowGroupCount()) {
                    transitionMatrix.makeRowGroupingTrivial();
                }
                storm::storage::sparse::ModelComponents<ValueType, RewardModelType> modelComponents(transitionMatrix, labeling);
                storm::models::sparse::Mdp<ValueType, RewardModelType> underApproxMdp(modelComponents);
                if (computeRewards) {
                    storm::models::sparse::StandardRewardModel<ValueType> rewardModel(boost::none, std::vector<ValueType>(modelComponents.transitionMatrix.getRowCount()));
                    for (auto const &iter : beliefStateMap.left) {
                        auto currentBelief = beliefList[iter.first];
                        auto representativeState = pomdp.getStatesWithObservation(currentBelief.observation).front();
                        for (uint64_t action = 0; action < underApproxMdp.getNumberOfChoices(iter.second); ++action) {
                            // Add the reward
                            rewardModel.setStateActionReward(underApproxMdp.getChoiceIndex(storm::storage::StateActionPair(iter.second, action)),
                                                             getRewardAfterAction(pomdp.getChoiceIndex(storm::storage::StateActionPair(representativeState, action)),
                                                                                  currentBelief));
                        }
                    }
                    underApproxMdp.addRewardModel("std", rewardModel);
                    underApproxMdp.restrictRewardModels(std::set<std::string>({"std"}));
                }
                model = std::make_shared<storm::models::sparse::Mdp<ValueType, RewardModelType>>(underApproxMdp);

                model->printModelInformationToStream(std::cout);
                statistics.underApproximationBuildTime.stop();

                std::string propertyString;
                if (computeRewards) {
                    propertyString = min ? "Rmin=? [F \"target\"]" : "Rmax=? [F \"target\"]";
                } else {
                    propertyString = min ? "Pmin=? [F \"target\"]" : "Pmax=? [F \"target\"]";
                }
                std::vector<storm::jani::Property> propertyVector = storm::api::parseProperties(propertyString);
                std::shared_ptr<storm::logic::Formula const> property = storm::api::extractFormulasFromProperties(propertyVector).front();

                statistics.underApproximationCheckTime.start();
                std::unique_ptr<storm::modelchecker::CheckResult> res(storm::api::verifyWithSparseEngine<ValueType>(model, storm::api::createTask<ValueType>(property, false)));
                statistics.underApproximationCheckTime.stop();
                if (storm::utility::resources::isTerminate() && !res) {
                    return nullptr;
                }
                STORM_LOG_ASSERT(res, "Result does not exist.");
                res->filter(storm::modelchecker::ExplicitQualitativeCheckResult(storm::storage::BitVector(underApproxMdp.getNumberOfStates(), true)));
                auto underApproxResultMap = res->asExplicitQuantitativeCheckResult<ValueType>().getValueMap();
                auto underApprox = underApproxResultMap[beliefStateMap.left.at(initialBeliefId)];

                return std::make_unique<UnderApproxComponents<ValueType>>(UnderApproxComponents<ValueType>{underApprox, underApproxResultMap, beliefStateMap});
            }

            template<typename ValueType, typename RewardModelType>
            std::unique_ptr<UnderApproxComponents<ValueType, RewardModelType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeUnderapproximation(std::shared_ptr<storm::storage::BeliefManager<storm::models::sparse::Pomdp<ValueType>>> beliefManager,
                                                                                                std::set<uint32_t> const &targetObservations, bool min,
                                                                                                bool computeRewards, uint64_t maxModelSize, std::vector<ValueType> const& lowerPomdpValueBounds, std::vector<ValueType> const& upperPomdpValueBounds) {
                // Build the belief MDP until enough states are explored.
                //TODO think of other ways to stop exploration besides model size

                statistics.underApproximationBuildTime.start();
                storm::builder::BeliefMdpExplorer<storm::models::sparse::Pomdp<ValueType>> explorer(beliefManager, lowerPomdpValueBounds, upperPomdpValueBounds);
                if (computeRewards) {
                    explorer.startNewExploration(storm::utility::zero<ValueType>());
                } else {
                    explorer.startNewExploration(storm::utility::one<ValueType>(), storm::utility::zero<ValueType>());
                }
                
                // Expand the beliefs to generate the grid on-the-fly
                if (options.explorationThreshold > storm::utility::zero<ValueType>()) {
                    STORM_PRINT("Exploration threshold: " << options.explorationThreshold << std::endl)
                }
                while (explorer.hasUnexploredState()) {
                    uint64_t currId = explorer.exploreNextState();
                    
                    uint32_t currObservation = beliefManager->getBeliefObservation(currId);
                    if (targetObservations.count(currObservation) != 0) {
                        explorer.setCurrentStateIsTarget();
                        explorer.addSelfloopTransition();
                    } else {
                        bool stopExploration = false;
                        if (storm::utility::abs<ValueType>(explorer.getUpperValueBoundAtCurrentState() - explorer.getLowerValueBoundAtCurrentState()) < options.explorationThreshold) {
                            stopExploration = true;
                            explorer.setCurrentStateIsTruncated();
                        } else if (explorer.getCurrentNumberOfMdpStates() >= maxModelSize) {
                            stopExploration = true;
                            explorer.setCurrentStateIsTruncated();
                        }
                        for (uint64 action = 0, numActions = beliefManager->getBeliefNumberOfChoices(currId); action < numActions; ++action) {
                            ValueType truncationProbability = storm::utility::zero<ValueType>();
                            ValueType truncationValueBound = storm::utility::zero<ValueType>();
                            auto successors = beliefManager->expand(currId, action);
                            for (auto const& successor : successors) {
                                bool added = explorer.addTransitionToBelief(action, successor.first, successor.second, stopExploration);
                                if (!added) {
                                    STORM_LOG_ASSERT(stopExploration, "Didn't add a transition although exploration shouldn't be stopped.");
                                    // We did not explore this successor state. Get a bound on the "missing" value
                                    truncationProbability += successor.second;
                                    truncationValueBound += successor.second * (min ? explorer.computeUpperValueBoundAtBelief(successor.first) : explorer.computeLowerValueBoundAtBelief(successor.first));
                                }
                            }
                            if (stopExploration) {
                                if (computeRewards) {
                                    explorer.addTransitionsToExtraStates(action, truncationProbability);
                                } else {
                                    explorer.addTransitionsToExtraStates(action, truncationValueBound, truncationProbability - truncationValueBound);
                                }
                            }
                            if (computeRewards) {
                                // The truncationValueBound will be added on top of the reward introduced by the current belief state.
                                explorer.computeRewardAtCurrentState(action, truncationValueBound);
                            }
                        }
                    }
                    if (storm::utility::resources::isTerminate()) {
                        statistics.underApproximationBuildAborted = true;
                        break;
                    }
                }
                statistics.underApproximationStates = explorer.getCurrentNumberOfMdpStates();
                if (storm::utility::resources::isTerminate()) {
                    statistics.underApproximationBuildTime.stop();
                    return nullptr;
                }
                
                explorer.finishExploration();
                statistics.underApproximationBuildTime.stop();
                STORM_PRINT("Under Approximation MDP build took " << statistics.underApproximationBuildTime << " seconds." << std::endl);
                explorer.getExploredMdp()->printModelInformationToStream(std::cout);

                statistics.underApproximationCheckTime.start();
                explorer.computeValuesOfExploredMdp(min ? storm::solver::OptimizationDirection::Minimize : storm::solver::OptimizationDirection::Maximize);
                statistics.underApproximationCheckTime.stop();

                STORM_PRINT("Time Underapproximation: " <<  statistics.underApproximationCheckTime << " seconds." << std::endl);
                STORM_PRINT("Under-Approximation Result: " << explorer.getComputedValueAtInitialState() << std::endl);
                
                return std::make_unique<UnderApproxComponents<ValueType>>(UnderApproxComponents<ValueType>{explorer.getComputedValueAtInitialState(), {}, {}});
            }
            

            template<typename ValueType, typename RewardModelType>
            storm::storage::SparseMatrix<ValueType>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::buildTransitionMatrix(std::vector<std::vector<std::map<uint64_t, ValueType>>> &transitions) {
                uint_fast64_t currentRow = 0;
                uint_fast64_t currentRowGroup = 0;
                uint64_t nrColumns = transitions.size();
                uint64_t nrRows = 0;
                uint64_t nrEntries = 0;
                for (auto const &actionTransitions : transitions) {
                    for (auto const &map : actionTransitions) {
                        nrEntries += map.size();
                        ++nrRows;
                    }
                }
                storm::storage::SparseMatrixBuilder<ValueType> smb(nrRows, nrColumns, nrEntries, true, true);
                for (auto const &actionTransitions : transitions) {
                    smb.newRowGroup(currentRow);
                    for (auto const &map : actionTransitions) {
                        for (auto const &transition : map) {
                            smb.addNextValue(currentRow, transition.first, transition.second);
                        }
                        ++currentRow;
                    }
                    ++currentRowGroup;
                }
                return smb.build();
            }

            template<typename ValueType, typename RewardModelType>
            uint64_t ApproximatePOMDPModelchecker<ValueType, RewardModelType>::getBeliefIdInVector(
                    std::vector<storm::pomdp::Belief<ValueType>> const &grid, uint32_t observation,
                    std::map<uint64_t, ValueType> &probabilities) {
                // TODO This one is quite slow
                for (auto const &belief : grid) {
                    if (belief.observation == observation) {
                        bool same = true;
                        for (auto const &probEntry : belief.probabilities) {
                            if (probabilities.find(probEntry.first) == probabilities.end()) {
                                same = false;
                                break;
                            }
                            if (!cc.isEqual(probEntry.second, probabilities[probEntry.first])) {
                                same = false;
                                break;
                            }
                        }
                        if (same) {
                            return belief.id;
                        }
                    }
                }
                return -1;
            }

            template<typename ValueType, typename RewardModelType>
            storm::pomdp::Belief<ValueType> ApproximatePOMDPModelchecker<ValueType, RewardModelType>::getInitialBelief(uint64_t id) {
                STORM_LOG_ASSERT(pomdp.getInitialStates().getNumberOfSetBits() < 2,
                                 "POMDP contains more than one initial state");
                STORM_LOG_ASSERT(pomdp.getInitialStates().getNumberOfSetBits() == 1,
                                 "POMDP does not contain an initial state");
                std::map<uint64_t, ValueType> distribution;
                uint32_t observation = 0;
                for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
                    if (pomdp.getInitialStates()[state] == 1) {
                        distribution[state] = storm::utility::one<ValueType>();
                        observation = pomdp.getObservation(state);
                        break;
                    }
                }
                return storm::pomdp::Belief<ValueType>{id, observation, distribution};
            }

            template<typename ValueType, typename RewardModelType>
            std::pair<std::vector<std::map<uint64_t, ValueType>>, std::vector<ValueType>>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeSubSimplexAndLambdas(
                    std::map<uint64_t, ValueType> &probabilities, uint64_t resolution, uint64_t nrStates) {

                //TODO this can also be simplified using the sparse vector interpretation

                // This is the Freudenthal Triangulation as described in Lovejoy (a whole lotta math)
                // Variable names are based on the paper
                std::vector<ValueType> x(nrStates);
                std::vector<ValueType> v(nrStates);
                std::vector<ValueType> d(nrStates);
                auto convResolution = storm::utility::convertNumber<ValueType>(resolution);

                for (size_t i = 0; i < nrStates; ++i) {
                    for (auto const &probEntry : probabilities) {
                        if (probEntry.first >= i) {
                            x[i] += convResolution * probEntry.second;
                        }
                    }
                    v[i] = storm::utility::floor(x[i]);
                    d[i] = x[i] - v[i];
                }

                auto p = storm::utility::vector::getSortedIndices(d);

                std::vector<std::vector<ValueType>> qs(nrStates, std::vector<ValueType>(nrStates));
                for (size_t i = 0; i < nrStates; ++i) {
                    if (i == 0) {
                        for (size_t j = 0; j < nrStates; ++j) {
                            qs[i][j] = v[j];
                        }
                    } else {
                        for (size_t j = 0; j < nrStates; ++j) {
                            if (j == p[i - 1]) {
                                qs[i][j] = qs[i - 1][j] + storm::utility::one<ValueType>();
                            } else {
                                qs[i][j] = qs[i - 1][j];
                            }
                        }
                    }
                }
                std::vector<std::map<uint64_t, ValueType>> subSimplex(nrStates);
                for (size_t j = 0; j < nrStates; ++j) {
                    for (size_t i = 0; i < nrStates - 1; ++i) {
                        if (cc.isLess(storm::utility::zero<ValueType>(), qs[j][i] - qs[j][i + 1])) {
                            subSimplex[j][i] = (qs[j][i] - qs[j][i + 1]) / convResolution;
                        }
                    }

                    if (cc.isLess(storm::utility::zero<ValueType>(), qs[j][nrStates - 1])) {
                        subSimplex[j][nrStates - 1] = qs[j][nrStates - 1] / convResolution;
                    }
                }

                std::vector<ValueType> lambdas(nrStates, storm::utility::zero<ValueType>());
                auto sum = storm::utility::zero<ValueType>();
                for (size_t i = 1; i < nrStates; ++i) {
                    lambdas[i] = d[p[i - 1]] - d[p[i]];
                    sum += d[p[i - 1]] - d[p[i]];
                }
                lambdas[0] = storm::utility::one<ValueType>() - sum;

                return std::make_pair(subSimplex, lambdas);
            }


            template<typename ValueType, typename RewardModelType>
            std::map<uint32_t, ValueType>
            ApproximatePOMDPModelchecker<ValueType, RewardModelType>::computeObservationProbabilitiesAfterAction(
                    storm::pomdp::Belief<ValueType> &belief,
                    uint64_t actionIndex) {
                std::map<uint32_t, ValueType> res;
                // the id is not important here as we immediately discard the belief (very hacky, I don't like it either)
                std::map<uint64_t, ValueType> postProbabilities;
                for (auto const &probEntry : belief.probabilities) {
                    uint64_t state = probEntry.first;
                    auto row = pomdp.getTransitionMatrix().getRow(pomdp.getChoiceIndex(storm::storage::StateActionPair(state, actionIndex)));
                    for (auto const &entry : row) {
                        if (entry.getValue() > 0) {
                            postProbabilities[entry.getColumn()] += belief.probabilities[state] * entry.getValue();
                        }
                    }
                }
                for (auto const &probEntry : postProbabilities) {
                    uint32_t observation = pomdp.getObservation(probEntry.first);
                    if (res.count(observation) == 0) {
                        res[observation] = probEntry.second;
                    } else {
                        res[observation] += probEntry.second;
                    }
                }

                return res;
            }

            template<typename ValueType, typename RewardModelType>
            uint64_t ApproximatePOMDPModelchecker<ValueType, RewardModelType>::getBeliefAfterActionAndObservation(std::vector<storm::pomdp::Belief<ValueType>> &beliefList,
                    std::vector<bool> &beliefIsTarget, std::set<uint32_t> const &targetObservations, storm::pomdp::Belief<ValueType> &belief, uint64_t actionIndex,
                    uint32_t observation, uint64_t id) {
                std::map<uint64_t, ValueType> distributionAfter;
                for (auto const &probEntry : belief.probabilities) {
                    uint64_t state = probEntry.first;
                    auto row = pomdp.getTransitionMatrix().getRow(pomdp.getChoiceIndex(storm::storage::StateActionPair(state, actionIndex)));
                    for (auto const &entry : row) {
                        if (pomdp.getObservation(entry.getColumn()) == observation) {
                            distributionAfter[entry.getColumn()] += belief.probabilities[state] * entry.getValue();
                        }
                    }
                }
                // We have to normalize the distribution
                auto sum = storm::utility::zero<ValueType>();
                for (auto const &entry : distributionAfter) {
                    sum += entry.second;
                }

                for (auto const &entry : distributionAfter) {
                    distributionAfter[entry.first] /= sum;
                }
                if (getBeliefIdInVector(beliefList, observation, distributionAfter) != uint64_t(-1)) {
                    auto res = getBeliefIdInVector(beliefList, observation, distributionAfter);
                    return res;
                } else {
                    beliefList.push_back(storm::pomdp::Belief<ValueType>{id, observation, distributionAfter});
                    beliefIsTarget.push_back(targetObservations.find(observation) != targetObservations.end());
                    return id;
                }
            }

            template<typename ValueType, typename RewardModelType>
            ValueType ApproximatePOMDPModelchecker<ValueType, RewardModelType>::getRewardAfterAction(uint64_t action, std::map<uint64_t, ValueType> const& belief) {
                auto result = storm::utility::zero<ValueType>();
                for (auto const &probEntry : belief) {
                    result += probEntry.second * pomdp.getUniqueRewardModel().getTotalStateActionReward(probEntry.first, action, pomdp.getTransitionMatrix());
                }
                return result;
            }

            template<typename ValueType, typename RewardModelType>
            ValueType ApproximatePOMDPModelchecker<ValueType, RewardModelType>::getRewardAfterAction(uint64_t action, storm::pomdp::Belief<ValueType> const& belief) {
                auto result = storm::utility::zero<ValueType>();
                for (auto const &probEntry : belief.probabilities) {
                    result += probEntry.second * pomdp.getUniqueRewardModel().getTotalStateActionReward(probEntry.first, action, pomdp.getTransitionMatrix());
                }
                return result;
            }


            template
            class ApproximatePOMDPModelchecker<double>;

#ifdef STORM_HAVE_CARL

            template
            class ApproximatePOMDPModelchecker<storm::RationalNumber>;

#endif
        }
    }
}
