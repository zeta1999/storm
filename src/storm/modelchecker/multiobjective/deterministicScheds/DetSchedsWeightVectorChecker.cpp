#include "storm/modelchecker/multiobjective/deterministicScheds/DetSchedsWeightVectorChecker.h"

namespace storm {
    namespace modelchecker {
        namespace multiobjective {
            
            template <typename ModelType>
            DetSchedsWeightVectorChecker<ModelType>::DetSchedsWeightVectorChecker(std::shared_ptr<MultiObjectiveSchedulerEvaluator<ModelType>> const& schedulerEvaluator) : schedulerEvaluator(schedulerEvaluator) {
                // Intentionally left empty;
            }
            
            template <typename ModelType>
            std::vector<std::vector<typename DetSchedsWeightVectorChecker<ModelType>::ValueType>> DetSchedsWeightVectorChecker<ModelType>::check(Environment const& env, std::vector<ValueType> const& weightVector) {
                std::vector<std::vector<ValueType>> resultStack;
                auto const& transitionMatrix = schedulerEvaluator->getModel().getTransitionMatrix();
                auto const& choiceIndices = schedulerEvaluator->getModel().getNondeterministicChoiceIndices();
                
                uint64_t const numObjectives = weightVector.size();
                
                // perform policy-iteration and store the intermediate results on the stack
                do {
                    schedulerEvaluator->check(env);
                    resultStack.push_back(schedulerEvaluator->getInitialStateResults());
                    
                    auto const& stateResults = schedulerEvaluator->getResults();
                    
                    // Check if scheduler choices can be improved
                    auto const& scheduler = schedulerEvaluator->getScheduler();
                    for (uint64_t state = 0; state < scheduler.size(); ++state) {
                        uint64_t choiceOffset = choiceIndices[state];
                        uint64_t numChoices = choiceIndices[state + 1] - choiceOffset;
                        uint64_t currChoice = scheduler[state];
                        
                        ValueType currChoiceValue = storm::utility::zero<ValueType>();
                        for (uint64_t objIndex = 0; objIndex < numObjectives; ++objIndex) {
                            currChoiceValue += weightVector[objIndex] * stateResults[objIndex][state];
                        }
                        
                        for (uint64_t choice = 0; choice < numChoices; ++choice) {
                            // Skip the currently selected choice
                            if (choice == currChoice) {
                                continue;
                            }
                            
                            ValueType choiceValue = storm::utility::zero<ValueType>();
                            for (uint64_t objIndex = 0; objIndex < numObjectives; ++objIndex) {
                                if (schedulerEvaluator->getSchedulerIndependentStates(objIndex).get(state)) {
                                    choiceValue += weightVector[objIndex] * stateResults[objIndex][state];
                                } else {
                                    ValueType objValue = storm::utility::zero<ValueType>();
                                    for (auto const& entry : transitionMatrix.getRow(choiceOffset + choice)) {
                                        objValue += entry.getValue() * stateResults[objIndex][entry.getColumn()];
                                    }
                                    choiceValue += weightVector[objIndex] * objValue;
                                }
                            }
                            
                            if (choiceValue > currChoiceValue) {
                                schedulerEvaluator->setChoiceAtState(state, choice);
                            }
                        }
                    }
                } while (!schedulerEvaluator->hasCurrentSchedulerBeenChecked());
                
            }
            
            template <typename ModelType>
            std::vector<typename DetSchedsWeightVectorChecker<ModelType>::ValueType> const& DetSchedsWeightVectorChecker<ModelType>::getResultForAllStates(uint64_t objIndex) const {
                return schedulerEvaluator->getResultForObjective(objIndex);
            }
            
            template <typename ModelType>
            std::vector<uint64_t> const& DetSchedsWeightVectorChecker<ModelType>::getScheduler() const {
                return schedulerEvaluator->getScheduler();
            }
            
            
        }
    }
}