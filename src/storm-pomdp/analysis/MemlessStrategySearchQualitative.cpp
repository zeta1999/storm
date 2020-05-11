#include "storm-pomdp/analysis/MemlessStrategySearchQualitative.h"
#include "storm/utility/file.h"

#include "storm-pomdp/analysis/QualitativeStrategySearchNaive.h"
#include "storm-pomdp/analysis/QualitativeAnalysis.h"
#include "storm-pomdp/analysis/QualitativeAnalysisOnGraphs.h"

namespace storm {
    namespace pomdp {

        namespace detail {
            void printRelevantInfoFromModel(std::shared_ptr<storm::solver::SmtSolver::ModelReference> const& model, std::vector<storm::expressions::Variable> const& reachVars, std::vector<storm::expressions::Variable> const& continuationVars) {
                uint64_t i = 0;
                std::stringstream ss;
                STORM_LOG_TRACE("states which we have now: ");
                for (auto const& rv : reachVars) {
                    if (model->getBooleanValue(rv)) {
                        ss << " " << i;
                    }
                    ++i;
                }
                STORM_LOG_TRACE(ss.str());
                i = 0;
                STORM_LOG_TRACE("states from which we continue: ");
                std::stringstream ss2;
                for (auto const& rv : continuationVars) {
                    if (model->getBooleanValue(rv)) {
                        ss2 << " " << i;
                    }
                    ++i;
                }
                STORM_LOG_TRACE(ss2.str());
            }
        }

        template <typename ValueType>
        void MemlessStrategySearchQualitative<ValueType>::Statistics::print() const {
            STORM_PRINT_AND_LOG("Total time: " << totalTimer << std::endl);
            STORM_PRINT_AND_LOG("SAT Calls " << satCalls << std::endl);
            STORM_PRINT_AND_LOG("SAT Calls time: " << smtCheckTimer << std::endl);
            STORM_PRINT_AND_LOG("Outer iterations: " << outerIterations << std::endl);
            STORM_PRINT_AND_LOG("Solver initialization time: " << initializeSolverTimer << std::endl);
            STORM_PRINT_AND_LOG("Obtain partial scheduler time: " << evaluateExtensionSolverTime << std::endl);
            STORM_PRINT_AND_LOG("Update solver to extend partial scheduler time: " << encodeExtensionSolverTime << std::endl);
            STORM_PRINT_AND_LOG("Update solver with new scheduler time: " << updateNewStrategySolverTime << std::endl);
            STORM_PRINT_AND_LOG("Winning regions update time: " << winningRegionUpdatesTimer << std::endl);
            STORM_PRINT_AND_LOG("Graph search time: " << graphSearchTime << std::endl);
        }

        template <typename ValueType>
        MemlessStrategySearchQualitative<ValueType>::MemlessStrategySearchQualitative(storm::models::sparse::Pomdp<ValueType> const& pomdp,
                    storm::storage::BitVector const& targetStates,
                    storm::storage::BitVector const& surelyReachSinkStates,
                    std::shared_ptr<storm::utility::solver::SmtSolverFactory>& smtSolverFactory,
                    MemlessSearchOptions const& options) :
            pomdp(pomdp),
            surelyReachSinkStates(surelyReachSinkStates),
            targetStates(targetStates),
            options(options),
            smtSolverFactory(smtSolverFactory)
        {
            this->expressionManager = std::make_shared<storm::expressions::ExpressionManager>();
            smtSolver = smtSolverFactory->create(*expressionManager);
            // Initialize states per observation.
            for (uint64_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                statesPerObservation.push_back(std::vector<uint64_t>()); // Consider using bitvectors instead.
                reachVarExpressionsPerObservation.push_back(std::vector<storm::expressions::Expression>());
            }
            uint64_t state = 0;
            for (auto obs : pomdp.getObservations()) {
                statesPerObservation.at(obs).push_back(state++);
            }
            // Initialize winning region
            std::vector<uint64_t> nrStatesPerObservation;
            for (auto const &states : statesPerObservation) {
                nrStatesPerObservation.push_back(states.size());
            }
            winningRegion = WinningRegion(nrStatesPerObservation);
            if(options.validateResult || options.validateEveryStep) {
                STORM_LOG_WARN("The validator should only be created when the option is set.");
                validator = std::make_shared<WinningRegionQueryInterface<ValueType>>(pomdp, winningRegion);
            }
        }

        template <typename ValueType>
        void MemlessStrategySearchQualitative<ValueType>::initialize(uint64_t k) {
            STORM_LOG_INFO("Start intializing solver...");
            bool lookaheadConstraintsRequired;
            if (options.forceLookahead) {
                lookaheadConstraintsRequired = true;
            } else {
                lookaheadConstraintsRequired = qualitative::isLookaheadRequired(pomdp,  targetStates, surelyReachSinkStates);
            }

            if (actionSelectionVars.empty()) {
                for (uint64_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                    actionSelectionVars.push_back(std::vector<storm::expressions::Variable>());
                    actionSelectionVarExpressions.push_back(std::vector<storm::expressions::Expression>());
                }
                for (uint64_t stateId = 0; stateId < pomdp.getNumberOfStates(); ++stateId) {
                    reachVars.push_back(expressionManager->declareBooleanVariable("C-" + std::to_string(stateId)));
                    reachVarExpressions.push_back(reachVars.back().getExpression());
                    reachVarExpressionsPerObservation[pomdp.getObservation(stateId)].push_back(
                            reachVarExpressions.back());
                    continuationVars.push_back(
                            expressionManager->declareBooleanVariable("D-" + std::to_string(stateId)));
                    continuationVarExpressions.push_back(continuationVars.back().getExpression());
                }
                // Create the action selection variables.
                uint64_t obs = 0;
                for (auto const &statesForObservation : statesPerObservation) {
                    for (uint64_t a = 0; a < pomdp.getNumberOfChoices(statesForObservation.front()); ++a) {
                        std::string varName = "A-" + std::to_string(obs) + "-" + std::to_string(a);
                        actionSelectionVars.at(obs).push_back(expressionManager->declareBooleanVariable(varName));
                        actionSelectionVarExpressions.at(obs).push_back(
                                actionSelectionVars.at(obs).back().getExpression());
                    }
                    schedulerVariables.push_back(
                            expressionManager->declareBitVectorVariable("scheduler-obs-" + std::to_string(obs),
                                                                        statesPerObservation.size()));
                    schedulerVariableExpressions.push_back(schedulerVariables.back());
                    switchVars.push_back(expressionManager->declareBooleanVariable("S-" + std::to_string(obs)));
                    switchVarExpressions.push_back(switchVars.back().getExpression());
                    observationUpdatedVariables.push_back(
                            expressionManager->declareBooleanVariable("U-" + std::to_string(obs)));
                    observationUpdatedExpressions.push_back(observationUpdatedVariables.back().getExpression());
                    followVars.push_back(expressionManager->declareBooleanVariable("F-"+std::to_string(obs)));
                    followVarExpressions.push_back(followVars.back().getExpression());

                    ++obs;
                }

                for (uint64_t stateId = 0; stateId < pomdp.getNumberOfStates(); ++stateId) {
                    pathVars.push_back(std::vector<storm::expressions::Expression>());
                }
            }

            uint64_t initK = 0;
            if (maxK != std::numeric_limits<uint64_t>::max()) {
                initK = maxK;
            }
            if (initK < k) {
                for (uint64_t stateId = 0; stateId < pomdp.getNumberOfStates(); ++stateId) {
                    if (lookaheadConstraintsRequired) {
                        for (uint64_t i = initK; i < k; ++i) {
                            pathVars[stateId].push_back(expressionManager->declareBooleanVariable(
                                    "P-" + std::to_string(stateId) + "-" + std::to_string(i)).getExpression());
                        }
                    }
                }
            }

            assert(!lookaheadConstraintsRequired || pathVars.size() == pomdp.getNumberOfStates());
            assert(reachVars.size() == pomdp.getNumberOfStates());
            assert(reachVarExpressions.size() == pomdp.getNumberOfStates());


            uint64_t obs = 0;

            for(auto const& statesForObservation : statesPerObservation) {
                if ( pomdp.getNumberOfChoices(statesForObservation.front()) == 1) {
                    ++obs;
                    continue;
                }
                if (options.onlyDeterministicStrategies || statesForObservation.size() == 1) {
                    for (uint64_t a = 0; a < pomdp.getNumberOfChoices(statesForObservation.front()) - 1; ++a) {
                        for (uint64_t b = a + 1; b < pomdp.getNumberOfChoices(statesForObservation.front()); ++b) {
                            smtSolver->add(!(actionSelectionVarExpressions[obs][a]) ||
                                           !(actionSelectionVarExpressions[obs][b]));
                        }
                    }
                }
                ++obs;
            }

            // PAPER COMMENT: 1
            obs = 0;
            for (auto const& actionVars : actionSelectionVarExpressions) {
                std::vector<storm::expressions::Expression> actExprs = actionVars;
                actExprs.push_back(followVarExpressions[obs]);
                smtSolver->add(storm::expressions::disjunction(actExprs));
                for (auto const& av : actionVars) {
                    smtSolver->add(!followVarExpressions[obs] || !av);
                }
                ++obs;
            }



            // Update at least one observation.
            // PAPER COMMENT: 2
            smtSolver->add(storm::expressions::disjunction(observationUpdatedExpressions));

            // PAPER COMMENT: 3
            if (lookaheadConstraintsRequired) {
                for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
                    if (targetStates.get(state)) {
                        smtSolver->add(pathVars[state][0]);
                    } else {
                        smtSolver->add(!pathVars[state][0] || followVarExpressions[pomdp.getObservation(state)]);
                    }
                }
            }

            // PAPER COMMENT: 4
            uint64_t rowindex = 0;
            for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
                if (targetStates.get(state) || surelyReachSinkStates.get(state)) {
                    rowindex += pomdp.getNumberOfChoices(state);
                    continue;
                }
                for (uint64_t action = 0; action < pomdp.getNumberOfChoices(state); ++action) {
                    std::vector<storm::expressions::Expression> subexprreachSwitch;
                    std::vector<storm::expressions::Expression> subexprreachNoSwitch;
                    subexprreachSwitch.push_back(!reachVarExpressions[state]);
                    subexprreachSwitch.push_back(!actionSelectionVarExpressions[pomdp.getObservation(state)][action]);
                    subexprreachSwitch.push_back(!switchVarExpressions[pomdp.getObservation(state)]);
                    subexprreachNoSwitch.push_back(!reachVarExpressions[state]);
                    subexprreachNoSwitch.push_back(!actionSelectionVarExpressions[pomdp.getObservation(state)][action]);
                    subexprreachNoSwitch.push_back(switchVarExpressions[pomdp.getObservation(state)]);
                    for (auto const &entries : pomdp.getTransitionMatrix().getRow(rowindex)) {
                        if (pomdp.getObservation(entries.getColumn() == pomdp.getObservation(state))) {
                            subexprreachSwitch.push_back(continuationVarExpressions.at(entries.getColumn()));
                        } else {
                            subexprreachSwitch.push_back(reachVarExpressions.at(entries.getColumn()));
                        }
                        smtSolver->add(storm::expressions::disjunction(subexprreachSwitch));
                        subexprreachSwitch.pop_back();
                        subexprreachNoSwitch.push_back(reachVarExpressions.at(entries.getColumn()));
                        smtSolver->add(storm::expressions::disjunction(subexprreachNoSwitch));
                        subexprreachNoSwitch.pop_back();
                    }

                    rowindex++;
                }
            }



            rowindex = 0;
            for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
                // PAPER COMMENT 5
                if (surelyReachSinkStates.get(state)) {
                    smtSolver->add(!reachVarExpressions[state]);
                    smtSolver->add(!continuationVarExpressions[state]);
                    if (lookaheadConstraintsRequired) {
                        for (uint64_t j = 1; j < k; ++j) {
                            smtSolver->add(!pathVars[state][j]);
                        }
                    }
                    rowindex += pomdp.getNumberOfChoices(state);
                } else if(!targetStates.get(state)) {
                    if (lookaheadConstraintsRequired) {
                        // PAPER COMMENT 6
                        smtSolver->add(storm::expressions::implies(reachVarExpressions.at(state), pathVars.at(state).back()));

                        // PAPER COMMENT 7
                        std::vector<std::vector<std::vector<storm::expressions::Expression>>> pathsubsubexprs;
                        for (uint64_t j = 1; j < k; ++j) {
                            pathsubsubexprs.push_back(std::vector<std::vector<storm::expressions::Expression>>());
                            for (uint64_t action = 0; action < pomdp.getNumberOfChoices(state); ++action) {
                                pathsubsubexprs.back().push_back(std::vector<storm::expressions::Expression>());
                            }
                        }

                        for (uint64_t action = 0; action < pomdp.getNumberOfChoices(state); ++action) {
                            std::vector<storm::expressions::Expression> subexprreach;
                            for (auto const &entries : pomdp.getTransitionMatrix().getRow(rowindex)) {
                                for (uint64_t j = 1; j < k; ++j) {
                                    pathsubsubexprs[j - 1][action].push_back(pathVars[entries.getColumn()][j - 1]);
                                }
                            }
                            rowindex++;
                        }

                        for (uint64_t j = 1; j < k; ++j) {
                            std::vector<storm::expressions::Expression> pathsubexprs;

                            for (uint64_t action = 0; action < pomdp.getNumberOfChoices(state); ++action) {
                                pathsubexprs.push_back(actionSelectionVarExpressions.at(pomdp.getObservation(state)).at(action) && storm::expressions::disjunction(pathsubsubexprs[j - 1][action]));
                            }
                            pathsubexprs.push_back(switchVarExpressions.at(pomdp.getObservation(state)));
                            pathsubexprs.push_back(followVarExpressions[pomdp.getObservation(state)]);
                            smtSolver->add(storm::expressions::iff(pathVars[state][j], storm::expressions::disjunction(pathsubexprs)));
                        }
                    }
                } else {
                    for (uint64_t j = 1; j < k; ++j) {
                        smtSolver->add(pathVars[state][j]);
                    }
                    rowindex += pomdp.getNumberOfChoices(state);
                }
            }

            // PAPER COMMENT 8
            obs = 0;
            for(auto const& statesForObservation : statesPerObservation) {
                for(auto const& state : statesForObservation) {
                    if (!targetStates.get(state)) {
                        smtSolver->add(!continuationVars[state] || schedulerVariableExpressions[obs] > 0);
                        smtSolver->add(!reachVarExpressions[state] || !followVarExpressions[obs] || schedulerVariableExpressions[obs] > 0);
                    }
                }
                ++obs;
            }

            // PAPER COMMENT 9
            for (uint64_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                smtSolver->add(storm::expressions::implies(switchVarExpressions[obs], storm::expressions::disjunction(reachVarExpressionsPerObservation[obs])));
            }
            // PAPER COMMENT 10
//            if (!lookaheadConstraintsRequired) {
//                uint64_t rowIndex = 0;
//                for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
//                    uint64_t enabledActions = pomdp.getNumberOfChoices(state);
//                    if (!surelyReachSinkStates.get(state)) {
//                        std::vector<storm::expressions::Expression> successorVars;
//                        for (uint64_t act = 0; act < enabledActions; ++act) {
//                            for (auto const &entries : pomdp.getTransitionMatrix().getRow(rowIndex)) {
//                                successorVars.push_back(reachVarExpressions[entries.getColumn()]);
//                            }
//                            rowIndex++;
//                        }
//                        successorVars.push_back(!switchVars[pomdp.getObservation(state)]);
//                        smtSolver->add(storm::expressions::implies(storm::expressions::conjunction(successorVars), reachVarExpressions[state]));
//                    } else {
//                        rowIndex += enabledActions;
//                    }
//                }
//            } else {
//                STORM_LOG_WARN("Some optimization not implemented yet.");
//            }
            // TODO: Update found schedulers if k is increased.
        }

        template<typename ValueType>
        uint64_t MemlessStrategySearchQualitative<ValueType>::getOffsetFromObservation(uint64_t state, uint64_t observation) const {
            if(!useFindOffset) {
                STORM_LOG_WARN("This code is slow and should only be used for debugging.");
                useFindOffset = true;
            }
            uint64_t offset = 0;
            for(uint64_t s : statesPerObservation[observation]) {
                if (s == state) {
                    return offset;
                }
                ++offset;
            }
            assert(false); // State should have occured.
            return 0;
        }

        template <typename ValueType>
        bool MemlessStrategySearchQualitative<ValueType>::analyze(uint64_t k, storm::storage::BitVector const& oneOfTheseStates, storm::storage::BitVector const& allOfTheseStates) {
            std::cout << "Surely reach sink states: " << surelyReachSinkStates << std::endl;
            std::cout << "Target states " << targetStates << std::endl;
            std::cout << (~surelyReachSinkStates & ~targetStates) << std::endl;
            stats.initializeSolverTimer.start();
            // TODO: When do we need to reinitialize? When the solver has been reset.
            initialize(k);
            maxK = k;


            stats.winningRegionUpdatesTimer.start();
            storm::storage::BitVector updated(pomdp.getNrObservations());
            // TODO CODE DUPLICATION WITH UPDATE, PUT IN PROCEDURE
            storm::storage::BitVector potentialWinner(pomdp.getNrObservations());
            storm::storage::BitVector observationsWithPartialWinners(pomdp.getNrObservations());
            for(uint64_t observation = 0; observation < pomdp.getNrObservations(); ++observation) {
                if (winningRegion.observationIsWinning(observation)) {
                    continue;
                }
                bool observationIsWinning = true;
                for (uint64_t state : statesPerObservation[observation]) {
                    if(!targetStates.get(state)) {
                        observationIsWinning = false;
                        observationsWithPartialWinners.set(observation);
                    } else {
                        potentialWinner.set(observation);
                    }
                }
                if(observationIsWinning) {
                    STORM_LOG_TRACE("Observation " << observation << " is winning.");
                    stats.incrementGraphBasedWinningObservations();
                    winningRegion.setObservationIsWinning(observation);
                    updated.set(observation);
                }
            }
            STORM_LOG_INFO("Graph based winning obs: " << stats.getGraphBasedwinningObservations());
            observationsWithPartialWinners &= potentialWinner;
            for (auto const& observation : observationsWithPartialWinners) {

                uint64_t nrStatesForObs  = statesPerObservation[observation].size();
                storm::storage::BitVector update(nrStatesForObs);
                for (uint64_t i = 0; i < nrStatesForObs; ++i ) {
                    uint64_t state = statesPerObservation[observation][i];
                    if(targetStates.get(state)) {
                        update.set(i);
                    }
                }
                assert(!update.empty());
                STORM_LOG_TRACE("Extend winning region for observation " << observation << " with target states/offsets" << update);
                winningRegion.addTargetStates(observation, update);
                assert(winningRegion.query(observation,update));// "Cannot continue: No scheduler known for state " << i << " (observation " << obs << ").");

                updated.set(observation);
            }
            for (auto const& state : targetStates) {
                STORM_LOG_ASSERT(winningRegion.isWinning(pomdp.getObservation(state),getOffsetFromObservation(state,pomdp.getObservation(state))), "Target state " << state << " , observation " << pomdp.getObservation(state) << " is not reflected as winning.");
            }
            stats.winningRegionUpdatesTimer.stop();


            uint64_t maximalNrActions = 8;
            STORM_LOG_WARN("We have hardcoded (an upper bound on) the number of actions");
            std::vector<storm::expressions::Expression> atLeastOneOfStates;

            for (uint64_t state : oneOfTheseStates) {
                STORM_LOG_ASSERT(reachVarExpressions.size() > state, "state id " << state << " exceeds number of states (" <<  reachVarExpressions.size() << ")" );
                atLeastOneOfStates.push_back(reachVarExpressions[state]);
            }
            // PAPER COMMENT 11
            if (!atLeastOneOfStates.empty()) {
                smtSolver->add(storm::expressions::disjunction(atLeastOneOfStates));
            }
            smtSolver->push();

            std::set<storm::expressions::Expression> allOfTheseAssumption;

            std::vector<storm::expressions::Expression> updateForObservationExpressions;

            for (uint64_t state : allOfTheseStates) {
                assert(reachVarExpressions.size() > state);
                allOfTheseAssumption.insert(reachVarExpressions[state]);
            }

            if (winningRegion.empty()) {
                // Keep it simple here to help bughunting if necessary.
                for (uint64_t ob = 0; ob < pomdp.getNrObservations(); ++ob) {
                    updateForObservationExpressions.push_back(
                            storm::expressions::disjunction(reachVarExpressionsPerObservation[ob]));
                    schedulerForObs.push_back(0);
                }
            } else {

                uint64_t obs = 0;
                for (auto const &statesForObservation : statesPerObservation) {
                    schedulerForObs.push_back(0);
                    for (auto const& winningSet : winningRegion.getWinningSetsPerObservation(obs)) {
                        for (auto const &stateOffset : ~winningSet) {
                            uint64_t state = statesForObservation[stateOffset];
                            assert(obs < schedulerForObs.size());
                            ++(schedulerForObs[obs]);
                            auto constant = expressionManager->integer(schedulerForObs[obs]);
                            // PAPER COMMENT 14:
                            smtSolver->add(!(continuationVarExpressions[state] &&
                                             (schedulerVariableExpressions[obs] == constant)));
                            smtSolver->add(!(reachVarExpressions[state] &&
                                             followVarExpressions[pomdp.getObservation(state)] &&
                                             (schedulerVariableExpressions[obs] == constant)));

                        }
                    }
                    if (winningRegion.getWinningSetsPerObservation(obs).empty()) {
                        updateForObservationExpressions.push_back(
                                storm::expressions::disjunction(reachVarExpressionsPerObservation[obs]));
                    } else {
                        updateForObservationExpressions.push_back(winningRegion.extensionExpression(obs, reachVarExpressionsPerObservation[obs]));
                    }
                    ++obs;

                }

            }

            for (uint64_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                auto constant = expressionManager->integer(schedulerForObs[obs]);
                smtSolver->add(schedulerVariableExpressions[obs] <= constant);
                smtSolver->add(storm::expressions::iff(observationUpdatedExpressions[obs], updateForObservationExpressions[obs]));
            }



            assert(pomdp.getNrObservations() == schedulerForObs.size());


            InternalObservationScheduler scheduler;
            scheduler.switchObservations = storm::storage::BitVector(pomdp.getNrObservations());
            storm::storage::BitVector newObservations(pomdp.getNrObservations());
            storm::storage::BitVector newObservationsAfterSwitch(pomdp.getNrObservations());
            storm::storage::BitVector observations(pomdp.getNrObservations());
            storm::storage::BitVector observationsAfterSwitch(pomdp.getNrObservations());
            storm::storage::BitVector observationUpdated(pomdp.getNrObservations());
            storm::storage::BitVector uncoveredStates(pomdp.getNumberOfStates());
            storm::storage::BitVector coveredStates(pomdp.getNumberOfStates());
            storm::storage::BitVector coveredStatesAfterSwitch(pomdp.getNumberOfStates());

            stats.initializeSolverTimer.stop();
            STORM_LOG_INFO("Start iterative solver...");

            uint64_t iterations  = 0;
            while(true) {
                stats.incrementOuterIterations();

                scheduler.reset(pomdp.getNrObservations(), maximalNrActions);
                observations.clear();
                observationsAfterSwitch.clear();
                coveredStates.clear();
                coveredStatesAfterSwitch.clear();
                observationUpdated.clear();
                if (!allOfTheseAssumption.empty()) {
                    bool foundResult = this->smtCheck(iterations, allOfTheseAssumption);
                    if (foundResult) {
                        // Consider storing the scheduler
                        return true;
                    }
                }
                bool newSchedulerDiscovered = false;

                while (true) {
                    ++iterations;


                    bool foundScheduler = this->smtCheck(iterations);
                    if (!foundScheduler) {
                        break;
                    }
                    newSchedulerDiscovered = true;
                    stats.evaluateExtensionSolverTime.start();
                    auto const& model = smtSolver->getModel();

                    newObservationsAfterSwitch.clear();
                    newObservations.clear();

                    uint64_t obs = 0;
                    for (auto const& ov : observationUpdatedVariables) {
                        if (!observationUpdated.get(obs) && model->getBooleanValue(ov)) {
                            STORM_LOG_TRACE("New observation updated: " << obs);
                            observationUpdated.set(obs);
                        }
                        obs++;
                    }

//                    for(uint64_t i : targetStates) {
//                        assert(model->getBooleanValue(reachVars[i]));
//                    }

                    uncoveredStates = ~coveredStates;
                    for (uint64_t i : uncoveredStates) {
                        auto const& rv  =reachVars[i];
                        auto const& rvExpr =reachVarExpressions[i];
                        if (model->getBooleanValue(rv)) {
                            STORM_LOG_TRACE("New state: " << i);
                            smtSolver->add(rvExpr);
                            assert(!surelyReachSinkStates.get(i));
                            newObservations.set(pomdp.getObservation(i));
                            coveredStates.set(i);
                        }
                    }

                    storm::storage::BitVector uncoveredStatesAfterSwitch(~coveredStatesAfterSwitch);
                    for (uint64_t i : uncoveredStatesAfterSwitch) {
                        auto const& cv  = continuationVars[i];
                        if (model->getBooleanValue(cv)) {
                            uint64_t obs = pomdp.getObservation(i);
                            STORM_LOG_ASSERT(winningRegion.isWinning(obs,getOffsetFromObservation(i,obs)), "Cannot continue: No scheduler known for state " << i << " (observation " << obs << ").");
                            auto const& cvExpr =continuationVarExpressions[i];
                            smtSolver->add(cvExpr);
                            if (!observationsAfterSwitch.get(obs)) {
                                newObservationsAfterSwitch.set(obs);
                            }

                        }
                    }
                    stats.evaluateExtensionSolverTime.stop();

                    if (options.computeTraceOutput()) {
                        detail::printRelevantInfoFromModel(model, reachVars, continuationVars);
                    }
                    stats.encodeExtensionSolverTime.start();
                    for (auto obs : newObservations) {
                        auto const &actionSelectionVarsForObs = actionSelectionVars[obs];
                        observations.set(obs);
                        for (uint64_t act = 0; act < actionSelectionVarsForObs.size(); ++act) {
                            if (model->getBooleanValue(actionSelectionVarsForObs[act])) {
                                scheduler.actions[obs].set(act);
                                smtSolver->add(actionSelectionVarExpressions[obs][act]);
                            } else {
                                smtSolver->add(!actionSelectionVarExpressions[obs][act]);
                            }
                        }
                        if (model->getBooleanValue(switchVars[obs])) {
                            scheduler.switchObservations.set(obs);
                            smtSolver->add(switchVarExpressions[obs]);
                        } else {
                            smtSolver->add(!switchVarExpressions[obs]);
                        }
                    }
                    for (auto obs : newObservationsAfterSwitch) {
                        observationsAfterSwitch.set(obs);
                        scheduler.schedulerRef[obs] = model->getIntegerValue(schedulerVariables[obs]);
                        smtSolver->add(schedulerVariableExpressions[obs] == expressionManager->integer(scheduler.schedulerRef.back()));
                    }

                    if(options.computeTraceOutput()) {
                        // generates debug output, but here we only want it for trace level.
                        // For consistency, all output on debug level.
                        STORM_LOG_DEBUG("the scheduler so far: ");
                        scheduler.printForObservations(observations,observationsAfterSwitch);
                    }

                    std::vector<storm::expressions::Expression> remainingExpressions;
                    for (auto index : ~coveredStates) {
                        if (observationUpdated.get(pomdp.getObservation(index))) {
                            remainingExpressions.push_back(reachVarExpressions[index]);
                        }
                    }
                    for (auto index : ~observationUpdated) {
                        remainingExpressions.push_back(observationUpdatedExpressions[index]);
                    }


                    if (remainingExpressions.empty()) {
                        stats.encodeExtensionSolverTime.stop();
                        break;
                    }
                    smtSolver->add(storm::expressions::disjunction(remainingExpressions));
                    stats.encodeExtensionSolverTime.stop();
                }
                if (!newSchedulerDiscovered) {
                    break;
                }
                smtSolver->pop();

                if(options.computeDebugOutput()) {
                    printCoveredStates(~coveredStates);
                    // generates info output, but here we only want it for debug level.
                    // For consistency, all output on info level.
                    STORM_LOG_DEBUG("the scheduler: ");
                    scheduler.printForObservations(observations,observationsAfterSwitch);
                }

                stats.winningRegionUpdatesTimer.start();
                storm::storage::BitVector updated(observations.size());
                uint64_t newTargetObservations = 0;
                for (uint64_t observation = 0; observation < pomdp.getNrObservations(); ++observation) {
                    STORM_LOG_TRACE("consider observation " << observation);
                    storm::storage::BitVector update(statesPerObservation[observation].size());
                    uint64_t i = 0;
                    for (uint64_t state : statesPerObservation[observation]) {
                        if (coveredStates.get(state)) {
                            assert(!surelyReachSinkStates.get(state));
                            update.set(i);
                        }
                        ++i;
                    }
                    if(!update.empty()) {
                        STORM_LOG_TRACE("Update Winning Region: Observation " << observation << " with update " << update);
                        bool updateResult = winningRegion.update(observation, update);
                        STORM_LOG_TRACE("Region changed:" << updateResult);
                        if (updateResult) {
                            if (winningRegion.observationIsWinning(observation)) {
                                ++newTargetObservations;
                                for (uint64_t state : statesPerObservation[observation]) {
                                    targetStates.set(state);
                                    assert(!surelyReachSinkStates.get(state));
                                }
                            }
                            updated.set(observation);
                            updateForObservationExpressions[observation] = winningRegion.extensionExpression(observation, reachVarExpressionsPerObservation[observation]);
                        }
                    }
                }
                stats.winningRegionUpdatesTimer.stop();
                if (newTargetObservations>0) {
                    stats.graphSearchTime.start();
                    storm::analysis::QualitativeAnalysisOnGraphs<ValueType> graphanalysis(pomdp);
                    uint64_t targetStatesBefore = targetStates.getNumberOfSetBits();
                    STORM_LOG_INFO("Target states before graph based analysis " << targetStates.getNumberOfSetBits());
                    targetStates = graphanalysis.analyseProb1Max(~surelyReachSinkStates, targetStates);
                    uint64_t targetStatesAfter = targetStates.getNumberOfSetBits();
                    STORM_LOG_INFO("Target states after graph based analysis " << targetStates.getNumberOfSetBits());
                    stats.graphSearchTime.stop();
                    if (targetStatesAfter -  targetStatesBefore > 0) {
                        stats.winningRegionUpdatesTimer.start();
                        // TODO CODE DUPLICATION WITH INIT, PUT IN PROCEDURE
                        storm::storage::BitVector potentialWinner(pomdp.getNrObservations());
                        storm::storage::BitVector observationsWithPartialWinners(pomdp.getNrObservations());
                        for(uint64_t observation = 0; observation < pomdp.getNrObservations(); ++observation) {
                            if (winningRegion.observationIsWinning(observation)) {
                                continue;
                            }
                            bool observationIsWinning = true;
                            for (uint64_t state : statesPerObservation[observation]) {
                                if(!targetStates.get(state)) {
                                    observationIsWinning = false;
                                    observationsWithPartialWinners.set(observation);
                                } else {
                                    potentialWinner.set(observation);
                                }
                            }
                            if(observationIsWinning) {
                                stats.incrementGraphBasedWinningObservations();
                                winningRegion.setObservationIsWinning(observation);
                                updated.set(observation);
                            }
                        }
                        STORM_LOG_INFO("Graph based winning obs: " << stats.getGraphBasedwinningObservations());
                        observationsWithPartialWinners &= potentialWinner;
                        for (auto const& observation : observationsWithPartialWinners) {
                            uint64_t nrStatesForObs  = statesPerObservation[observation].size();
                            storm::storage::BitVector update(nrStatesForObs);
                            for (uint64_t i = 0; i < nrStatesForObs; ++i ) {
                                uint64_t state = statesPerObservation[observation][i];
                                if(targetStates.get(state)) {
                                    update.set(i);
                                }
                            }
                            assert(!update.empty());
                            STORM_LOG_TRACE("Extend winning region for observation " << observation << " with target states/offsets" << update);
                            winningRegion.addTargetStates(observation, update);
                            assert(winningRegion.query(observation,update));//
                            updated.set(observation);
                        }
                        stats.winningRegionUpdatesTimer.stop();

                        if (observationsWithPartialWinners.getNumberOfSetBits() > 0) {
                            STORM_LOG_WARN("This case has been barely tested and likely contains bug");
                            reset();
                            return analyze(k, ~targetStates & ~surelyReachSinkStates);
                        }
                    }

                }
                STORM_LOG_ASSERT(!updated.empty(), "The strategy should be new in at least one place");
                if(options.computeDebugOutput()) {
                    winningRegion.print();
                }
                if(options.validateEveryStep) {
                    STORM_LOG_WARN("Validating every step, for debug purposes only!");
                    validator->validate(surelyReachSinkStates);
                }
                stats.updateNewStrategySolverTime.start();
                for(uint64_t observation : updated) {
                    updateForObservationExpressions[observation] = winningRegion.extensionExpression(observation, reachVarExpressionsPerObservation[observation]);
                }

                uint64_t obs = 0;
                for (auto const &statesForObservation : statesPerObservation) {
                    if (observations.get(obs) && updated.get(obs)) {
                        STORM_LOG_DEBUG("We have a new policy ( " << finalSchedulers.size() << " ) for states with observation " << obs << ".");
                        assert(schedulerForObs.size() > obs);
                        (schedulerForObs[obs])++;
                        STORM_LOG_DEBUG("We now have "  << schedulerForObs[obs] << " policies for states with observation " << obs);

                        for (auto const &state : statesForObservation) {
                            if (!coveredStates.get(state)) {
                                auto constant = expressionManager->integer(schedulerForObs[obs]);
                                // PAPER COMMENT 14:
                                smtSolver->add(!(continuationVarExpressions[state] && (schedulerVariableExpressions[obs] == constant)));
                                smtSolver->add(!(reachVarExpressions[state] && followVarExpressions[pomdp.getObservation(state)] && (schedulerVariableExpressions[obs] == constant)));
                            }
                        }
                    }
                    ++obs;
                }
                finalSchedulers.push_back(scheduler);

                smtSolver->push();

                for (uint64_t obs = 0; obs < pomdp.getNrObservations(); ++obs) {
                    auto constant = expressionManager->integer(schedulerForObs[obs]);
                    // PAPER COMMENT 13
                    smtSolver->add(schedulerVariableExpressions[obs] <= constant);
                    // PAPER COMMENT 12
                    smtSolver->add(storm::expressions::iff(observationUpdatedExpressions[obs], updateForObservationExpressions[obs]));
                }
                stats.updateNewStrategySolverTime.stop();

                STORM_LOG_INFO("... after iteration " << stats.getIterations() << " so far " << stats.getChecks() << " checks." );
            }
            if(options.validateResult) {
                STORM_LOG_WARN("Validating result is a winning region, only for debugging purposes.");
                validator->validate(surelyReachSinkStates);
                STORM_LOG_WARN("Validating result is a maximal winning region, only for debugging purposes.");
                validator->validateIsMaximal(surelyReachSinkStates);
            }
            winningRegion.print();

            if (!allOfTheseStates.empty()) {
                for (uint64_t observation = 0; observation < pomdp.getNrObservations(); ++observation) {
                    storm::storage::BitVector check(statesPerObservation[observation].size());
                    uint64_t i = 0;
                    for (uint64_t state : statesPerObservation[observation]) {
                        if (allOfTheseStates.get(state)) {
                            check.set(i);
                        }
                        ++i;
                    }
                    if (!winningRegion.query(observation, check)) {
                        return false;
                    }
                }
            }
            return true;
        }

        template<typename ValueType>
        void MemlessStrategySearchQualitative<ValueType>::printCoveredStates(storm::storage::BitVector const &remaining) const {

            STORM_LOG_DEBUG("states that are okay");
            for (uint64_t state = 0; state < pomdp.getNumberOfStates(); ++state) {
                if (!remaining.get(state)) {
                    std::cout << " " << state;
                }
            }
            std::cout << std::endl;

        }

        template<typename ValueType>
        void MemlessStrategySearchQualitative<ValueType>::printScheduler(std::vector<InternalObservationScheduler> const& ) {

        }

        template<typename ValueType>
        void MemlessStrategySearchQualitative<ValueType>::finalizeStatistics() {

        }

        template<typename ValueType>
        typename MemlessStrategySearchQualitative<ValueType>::Statistics const& MemlessStrategySearchQualitative<ValueType>::getStatistics() const{
            return stats;
        }

        template <typename ValueType>
        bool MemlessStrategySearchQualitative<ValueType>::smtCheck(uint64_t iteration, std::set<storm::expressions::Expression> const& assumptions) {
            if(options.isExportSATSet()) {
                STORM_LOG_DEBUG("Export SMT Solver Call (" <<iteration << ")");
                std::string filepath = options.getExportSATCallsPath() + "call_" + std::to_string(iteration) + ".smt2";
                std::ofstream filestream;
                storm::utility::openFile(filepath, filestream);
                filestream << smtSolver->getSmtLibString() << std::endl;
                storm::utility::closeFile(filestream);
            }

            STORM_LOG_DEBUG("Call to SMT Solver (" <<iteration << ")");
            storm::solver::SmtSolver::CheckResult result;
            stats.smtCheckTimer.start();
            if (assumptions.empty()) {
                result = smtSolver->check();
            } else {
                result = smtSolver->checkWithAssumptions(assumptions);
            }
            stats.smtCheckTimer.stop();
            stats.incrementSmtChecks();

            if (result == storm::solver::SmtSolver::CheckResult::Unknown) {
                STORM_LOG_THROW(false, storm::exceptions::UnexpectedException, "SMT solver yielded an unexpected result");
            } else if (result == storm::solver::SmtSolver::CheckResult::Unsat) {
                STORM_LOG_DEBUG("Unsatisfiable!");
                return false;
            }

            STORM_LOG_TRACE("Satisfying assignment: ");
            STORM_LOG_TRACE(smtSolver->getModelAsValuation().toString(true));
            return true;
        }

        template class MemlessStrategySearchQualitative<double>;
        template class MemlessStrategySearchQualitative<storm::RationalNumber>;


    }
}
