#include "src/storage/bisimulation/NondeterministicModelBisimulationDecomposition.h"

#include "src/models/sparse/Mdp.h"
#include "src/models/sparse/StandardRewardModel.h"

#include "src/utility/graph.h"

#include "src/utility/macros.h"
#include "src/exceptions/IllegalFunctionCallException.h"

#include "src/adapters/CarlAdapter.h"

namespace storm {
    namespace storage {
        
        using namespace bisimulation;
        
        template<typename ModelType>
        NondeterministicModelBisimulationDecomposition<ModelType>::NondeterministicModelBisimulationDecomposition(ModelType const& model, typename BisimulationDecomposition<ModelType, NondeterministicModelBisimulationDecomposition::BlockDataType>::Options const& options) : BisimulationDecomposition<ModelType, NondeterministicModelBisimulationDecomposition::BlockDataType>(model, model.getTransitionMatrix().transpose(false), options), choiceToStateMapping(model.getNumberOfChoices()), quotientDistributions(model.getNumberOfChoices()), orderedQuotientDistributions(model.getNumberOfChoices()) {
            STORM_LOG_THROW(options.type == BisimulationType::Strong, storm::exceptions::IllegalFunctionCallException, "Weak bisimulation is currently not supported for nondeterministic models.");
        }
        
        template<typename ModelType>
        std::pair<storm::storage::BitVector, storm::storage::BitVector> NondeterministicModelBisimulationDecomposition<ModelType>::getStatesWithProbability01() {
            STORM_LOG_THROW(static_cast<bool>(this->options.optimalityType), storm::exceptions::IllegalFunctionCallException, "Can only compute states with probability 0/1 with an optimization direction (min/max).");
            if (this->options.optimalityType.get() == OptimizationDirection::Minimize) {
                return storm::utility::graph::performProb01Min(this->model.getTransitionMatrix(), this->model.getTransitionMatrix().getRowGroupIndices(), this->model.getBackwardTransitions(), this->options.phiStates.get(), this->options.psiStates.get());
            } else {
                return storm::utility::graph::performProb01Max(this->model.getTransitionMatrix(), this->model.getTransitionMatrix().getRowGroupIndices(), this->model.getBackwardTransitions(), this->options.phiStates.get(), this->options.psiStates.get());
            }
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::initialize() {
            this->createChoiceToStateMapping();
            this->initializeQuotientDistributions();
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::createChoiceToStateMapping() {
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();
            for (storm::storage::sparse::state_type state = 0; state < this->model.getNumberOfStates(); ++state) {
                for (uint_fast64_t choice = nondeterministicChoiceIndices[state]; choice < nondeterministicChoiceIndices[state + 1]; ++choice) {
                    choiceToStateMapping[choice] = state;
                }
            }
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::initializeQuotientDistributions() {
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();
            for (auto choice = 0; choice < nondeterministicChoiceIndices.back(); ++choice) {
                for (auto entry : this->model.getTransitionMatrix().getRow(choice)) {
                    if (!this->comparator.isZero(entry.getValue())) {
                        this->quotientDistributions[choice].addProbability(this->partition.getBlock(entry.getColumn()).getId(), entry.getValue());
                    }
                }
                orderedQuotientDistributions[choice] = &this->quotientDistributions[choice];
            }
            
            for (auto state = 0; state < this->model.getNumberOfStates(); ++state) {
                updateOrderedQuotientDistributions(state);
            }
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::updateOrderedQuotientDistributions(storm::storage::sparse::state_type state) {
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();
            std::sort(this->orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state], this->orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state + 1],
                      [this] (storm::storage::Distribution<ValueType> const* dist1, storm::storage::Distribution<ValueType> const* dist2) {
                          return dist1->less(*dist2, this->comparator);
                      });
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::buildQuotient() {
            STORM_LOG_ASSERT(false, "Not yet implemented");
            // TODO
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::possiblyNeedsRefinement(bisimulation::Block<BlockDataType> const& block) const {
            return block.getNumberOfStates() > 1 && !block.data().absorbing();
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::updateQuotientDistributionsOfPredecessors(Block<BlockDataType> const& newBlock, Block<BlockDataType> const& oldBlock, std::deque<Block<BlockDataType>*>& splitterQueue) {
            uint_fast64_t lastState = 0;
            bool lastStateInitialized = false;
            
            for (auto stateIt = this->partition.begin(newBlock), stateIte = this->partition.end(newBlock); stateIt != stateIte; ++stateIt) {
                for (auto predecessorEntry : this->backwardTransitions.getRow(*stateIt)) {
                    if (this->comparator.isZero(predecessorEntry.getValue())) {
                        continue;
                    }
                    
                    storm::storage::sparse::state_type predecessorChoice = predecessorEntry.getColumn();
                    storm::storage::sparse::state_type predecessorState = choiceToStateMapping[predecessorChoice];
                    Block<BlockDataType>& predecessorBlock = this->partition.getBlock(predecessorState);
                    
                    // If the predecessor block is not marked as to-refined, we do so now.
                    if (!predecessorBlock.data().splitter()) {
                        predecessorBlock.data().setSplitter();
                        splitterQueue.push_back(&predecessorBlock);
                    }
                    
                    if (lastStateInitialized) {
                        // If we have skipped to the choices of the next state, we need to repair the order of the
                        // distributions for the last state.
                        if (lastState != predecessorState) {
                            updateOrderedQuotientDistributions(lastState);
                            lastState = predecessorState;
                        }
                    } else {
                        lastStateInitialized = true;
                        lastState = choiceToStateMapping[predecessorChoice];
                    }
                    
                    // Now shift the probability from this transition from the old block to the new one.
                    this->quotientDistributions[predecessorChoice].shiftProbability(oldBlock.getId(), newBlock.getId(), predecessorEntry.getValue());
                }
            }
            
            if (lastStateInitialized) {
                updateOrderedQuotientDistributions(lastState);
            }
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::checkQuotientDistributions() const {
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();
            for (auto state = 0; state < this->model.getNumberOfStates(); ++state) {
                for (auto choice = nondeterministicChoiceIndices[state]; choice < nondeterministicChoiceIndices[state + 1]; ++choice) {
                    storm::storage::Distribution<ValueType> distribution;
                    for (auto const& element : this->model.getTransitionMatrix().getRow(choice)) {
                        distribution.addProbability(this->partition.getBlock(element.getColumn()).getId(), element.getValue());
                    }

                    if (!distribution.equals(quotientDistributions[choice])) {
                        std::cout << "the distributions for choice " << choice << " of state " << state << " do not match." << std::endl;
                        std::cout << "is: " << quotientDistributions[choice] << " but should be " << distribution << std::endl;
                        exit(-1);
                    }

                    bool less1 = distribution.less(quotientDistributions[choice], this->comparator);
                    bool less2 = quotientDistributions[choice].less(distribution, this->comparator);
                    
                    if (distribution.equals(quotientDistributions[choice]) && (less1 || less2)) {
                        std::cout << "mismatch of equality and less for " << std::endl;
                        std::cout << quotientDistributions[choice] << " vs " << distribution << std::endl;
                        exit(-1);
                    }
                }
                
                for (auto choice = nondeterministicChoiceIndices[state]; choice < nondeterministicChoiceIndices[state + 1] - 1; ++choice) {
                    if (orderedQuotientDistributions[choice + 1]->less(*orderedQuotientDistributions[choice], this->comparator)) {
                        std::cout << "choice " << (choice+1) << " is less than predecessor" << std::endl;
                        std::cout << *orderedQuotientDistributions[choice] << " should be less than " << *orderedQuotientDistributions[choice + 1] << std::endl;
                        exit(-1);
                    }
                }
            }
            return true;
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::printDistributions(uint_fast64_t state) const {
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();
            for (auto choice = nondeterministicChoiceIndices[state]; choice < nondeterministicChoiceIndices[state + 1]; ++choice) {
                std::cout << quotientDistributions[choice] << std::endl;
            }
            return true;
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::checkBlockStable(bisimulation::Block<BlockDataType> const& newBlock) const {
            std::cout << "checking stability of new block " << newBlock.getId() << " of size " << newBlock.getNumberOfStates() << std::endl;
            for (auto stateIt1 = this->partition.begin(newBlock), stateIte1 = this->partition.end(newBlock); stateIt1 != stateIte1; ++stateIt1) {
                for (auto stateIt2 = this->partition.begin(newBlock), stateIte2 = this->partition.end(newBlock); stateIt2 != stateIte2; ++stateIt2) {
                    bool less1 = quotientDistributionsLess(*stateIt1, *stateIt2);
                    bool less2 = quotientDistributionsLess(*stateIt2, *stateIt1);
                    if (less1 || less2) {
                        std::cout << "the partition is not stable for the states " << *stateIt1 << " and " << *stateIt2 << std::endl;
                        std::cout << "less1 " << less1 << " and less2 " << less2 << std::endl;
                        
                        std::cout << "distributions of state " << *stateIt1 << std::endl;
                        this->printDistributions(*stateIt1);
                        std::cout << "distributions of state " << *stateIt2 << std::endl;
                        this->printDistributions(*stateIt2);
                        exit(-1);
                    }
                }
            }
            return true;
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::checkDistributionsDifferent(bisimulation::Block<BlockDataType> const& block, storm::storage::sparse::state_type end) const {
            for (auto stateIt1 = this->partition.begin(block), stateIte1 = this->partition.end(block); stateIt1 != stateIte1; ++stateIt1) {
                for (auto stateIt2 = this->partition.begin() + block.getEndIndex(), stateIte2 = this->partition.begin() + end; stateIt2 != stateIte2; ++stateIt2) {
                    if (!quotientDistributionsLess(*stateIt1, *stateIt2)) {
                        std::cout << "distributions are not less, even though they should be!" << std::endl;
                        exit(-3);
                    } else {
                        std::cout << "less:" << std::endl;
                        this->printDistributions(*stateIt1);
                        std::cout << "and" << std::endl;
                        this->printDistributions(*stateIt2);
                    }
                }
            }
            return true;
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::splitBlockAccordingToCurrentQuotientDistributions(Block<BlockDataType>& block, std::deque<Block<BlockDataType>*>& splitterQueue) {
            std::list<Block<BlockDataType>*> newBlocks;
            bool split = this->partition.splitBlock(block,
                                                    [this] (storm::storage::sparse::state_type state1, storm::storage::sparse::state_type state2) {
                                                        bool result = quotientDistributionsLess(state1, state2);
//                                                        std::cout << state1 << " is " << (!result ? "not" : "") << " smaller than " << state2 << std::endl;
                                                        return result;
                                                    },
                                                    [this, &block, &splitterQueue, &newBlocks] (Block<BlockDataType>& newBlock) {
                                                        newBlocks.push_back(&newBlock);
                                                        
//                                                        this->checkBlockStable(newBlock);
//                                                        this->checkDistributionsDifferent(block, block.getEndIndex());
//                                                        this->checkQuotientDistributions();
                                                    });
            
            // The quotient distributions of the predecessors of block do not need to be updated, since the probability
            // will go to the block with the same id as before.
            
//            std::cout << "partition after split: " << std::endl;
//            this->partition.print();
            
            // defer updating the quotient distributions until *after* all splits, because
            // it otherwise influences the subsequent splits!
            for (auto el : newBlocks) {
                this->updateQuotientDistributionsOfPredecessors(*el, block, splitterQueue);
            }
            
//            this->checkQuotientDistributions();
            
            return split;
        }
        
        template<typename ModelType>
        bool NondeterministicModelBisimulationDecomposition<ModelType>::quotientDistributionsLess(storm::storage::sparse::state_type state1, storm::storage::sparse::state_type state2) const {
            STORM_LOG_TRACE("Comparing the quotient distributions of state " << state1 << " and " << state2 << ".");
            std::vector<uint_fast64_t> nondeterministicChoiceIndices = this->model.getTransitionMatrix().getRowGroupIndices();

//            std::cout << "distributions of state " << state1 << std::endl;
//            this->printDistributions(state1);
//            std::cout << "distributions of state " << state2 << std::endl;
//            this->printDistributions(state2);
            
            auto firstIt = orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state1];
            auto firstIte = orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state1 + 1];
            auto secondIt = orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state2];
            auto secondIte = orderedQuotientDistributions.begin() + nondeterministicChoiceIndices[state2 + 1];
            
            for (; firstIt != firstIte && secondIt != secondIte; ++firstIt, ++secondIt) {
                // If the current distributions are in a less-than relationship, we can return a result.
                if ((*firstIt)->less(**secondIt, this->comparator)) {
                    return true;
                } else if ((*secondIt)->less(**firstIt, this->comparator)) {
                    return false;
                }
                
                // If the distributions matched, we need to advance both distribution iterators to the next distribution
                // that is larger.
                while (firstIt != firstIte && std::next(firstIt) != firstIte && !(*firstIt)->less(**std::next(firstIt), this->comparator)) {
                    ++firstIt;
                }
                while (secondIt != secondIte && std::next(secondIt) != secondIte && !(*secondIt)->less(**std::next(secondIt), this->comparator)) {
                    ++secondIt;
                }
            }
            
            if (firstIt == firstIte && secondIt != secondIte) {
                return true;
            }
            return false;
        }
        
        template<typename ModelType>
        void NondeterministicModelBisimulationDecomposition<ModelType>::refinePartitionBasedOnSplitter(bisimulation::Block<BlockDataType>& splitter, std::deque<bisimulation::Block<BlockDataType>*>& splitterQueue) {
            if (!possiblyNeedsRefinement(splitter)) {
                return;
            }
            
            STORM_LOG_TRACE("Refining block " << splitter.getId());
            
            splitBlockAccordingToCurrentQuotientDistributions(splitter, splitterQueue);
        }
        
        template class NondeterministicModelBisimulationDecomposition<storm::models::sparse::Mdp<double>>;
        
#ifdef STORM_HAVE_CARL
        template class NondeterministicModelBisimulationDecomposition<storm::models::sparse::Mdp<storm::RationalFunction>>;
#endif
        
    }
}