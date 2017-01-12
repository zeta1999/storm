#include "storm/models/sparse/StandardRewardModel.h"

#include "storm/utility/vector.h"

#include "storm/exceptions/InvalidOperationException.h"

#include "storm/adapters/CarlAdapter.h"

namespace storm {
    namespace models {
        namespace sparse {
            template<typename ValueType>
            StandardRewardModel<ValueType>::StandardRewardModel(boost::optional<std::vector<ValueType>> const& optionalStateRewardVector,
                                                                boost::optional<std::vector<ValueType>> const& optionalStateActionRewardVector,
                                                                boost::optional<storm::storage::SparseMatrix<ValueType>> const& optionalTransitionRewardMatrix)
            : optionalStateRewardVector(optionalStateRewardVector), optionalStateActionRewardVector(optionalStateActionRewardVector), optionalTransitionRewardMatrix(optionalTransitionRewardMatrix) {
                // Intentionally left empty.
            }
            
            template<typename ValueType>
            StandardRewardModel<ValueType>::StandardRewardModel(boost::optional<std::vector<ValueType>>&& optionalStateRewardVector,
                                                                boost::optional<std::vector<ValueType>>&& optionalStateActionRewardVector,
                                                                boost::optional<storm::storage::SparseMatrix<ValueType>>&& optionalTransitionRewardMatrix)
            : optionalStateRewardVector(std::move(optionalStateRewardVector)), optionalStateActionRewardVector(std::move(optionalStateActionRewardVector)), optionalTransitionRewardMatrix(std::move(optionalTransitionRewardMatrix)) {
                // Intentionally left empty.
            }
            
            template<typename ValueType>
            bool StandardRewardModel<ValueType>::hasStateRewards() const {
                return static_cast<bool>(this->optionalStateRewardVector);
            }
            
            template<typename ValueType>
            bool StandardRewardModel<ValueType>::hasOnlyStateRewards() const {
                return static_cast<bool>(this->optionalStateRewardVector) && !static_cast<bool>(this->optionalStateActionRewardVector) && !static_cast<bool>(this->optionalTransitionRewardMatrix);
            }
            
            template<typename ValueType>
            std::vector<ValueType> const& StandardRewardModel<ValueType>::getStateRewardVector() const {
                STORM_LOG_ASSERT(this->hasStateRewards(), "No state rewards available.");
                return this->optionalStateRewardVector.get();
            }

            template<typename ValueType>
            std::vector<ValueType>& StandardRewardModel<ValueType>::getStateRewardVector() {
                STORM_LOG_ASSERT(this->hasStateRewards(), "No state rewards available.");
                return this->optionalStateRewardVector.get();
            }

            template<typename ValueType>
            boost::optional<std::vector<ValueType>> const& StandardRewardModel<ValueType>::getOptionalStateRewardVector() const {
                return this->optionalStateRewardVector;
            }

            template<typename ValueType>
            ValueType const& StandardRewardModel<ValueType>::getStateReward(uint_fast64_t state) const {
                STORM_LOG_ASSERT(this->hasStateRewards(), "No state rewards available.");
                STORM_LOG_ASSERT(state < this->optionalStateRewardVector.get().size(), "Invalid state.");
                return this->optionalStateRewardVector.get()[state];
            }

            template<typename ValueType>
            template<typename T>
            void StandardRewardModel<ValueType>::setStateReward(uint_fast64_t state, T const & newReward) {
                STORM_LOG_ASSERT(this->hasStateRewards(), "No state rewards available.");
                STORM_LOG_ASSERT(state < this->optionalStateRewardVector.get().size(), "Invalid state.");
                this->optionalStateRewardVector.get()[state] = newReward;
            }

            template<typename ValueType>
            bool StandardRewardModel<ValueType>::hasStateActionRewards() const {
                return static_cast<bool>(this->optionalStateActionRewardVector);
            }
            
            template<typename ValueType>
            std::vector<ValueType> const& StandardRewardModel<ValueType>::getStateActionRewardVector() const {
                STORM_LOG_ASSERT(this->hasStateActionRewards(), "No state action rewards available.");
                return this->optionalStateActionRewardVector.get();
            }
            
            template<typename ValueType>
            std::vector<ValueType>& StandardRewardModel<ValueType>::getStateActionRewardVector() {
                STORM_LOG_ASSERT(this->hasStateActionRewards(), "No state action rewards available.");
                return this->optionalStateActionRewardVector.get();
            }

            template<typename ValueType>
            ValueType const& StandardRewardModel<ValueType>::getStateActionReward(uint_fast64_t choiceIndex) const {
                STORM_LOG_ASSERT(this->hasStateActionRewards(), "No state action rewards available.");
                STORM_LOG_ASSERT(choiceIndex < this->optionalStateActionRewardVector.get().size(), "Invalid choiceIndex.");
                return this->optionalStateActionRewardVector.get()[choiceIndex];
            }

            template<typename ValueType>
            template<typename T>
            void StandardRewardModel<ValueType>::setStateActionReward(uint_fast64_t choiceIndex, T const &newValue) {
                STORM_LOG_ASSERT(this->hasStateActionRewards(), "No state action rewards available.");
                STORM_LOG_ASSERT(choiceIndex < this->optionalStateActionRewardVector.get().size(), "Invalid choiceIndex.");
                this->optionalStateActionRewardVector.get()[choiceIndex] = newValue;
            }

            template<typename ValueType>
            boost::optional<std::vector<ValueType>> const& StandardRewardModel<ValueType>::getOptionalStateActionRewardVector() const {
                return this->optionalStateActionRewardVector;
            }

            template<typename ValueType>
            bool StandardRewardModel<ValueType>::hasTransitionRewards() const {
                return static_cast<bool>(this->optionalTransitionRewardMatrix);
            }
            
            template<typename ValueType>
            storm::storage::SparseMatrix<ValueType> const& StandardRewardModel<ValueType>::getTransitionRewardMatrix() const {
                return this->optionalTransitionRewardMatrix.get();
            }

            template<typename ValueType>
            storm::storage::SparseMatrix<ValueType>& StandardRewardModel<ValueType>::getTransitionRewardMatrix() {
                return this->optionalTransitionRewardMatrix.get();
            }

            template<typename ValueType>
            boost::optional<storm::storage::SparseMatrix<ValueType>> const& StandardRewardModel<ValueType>::getOptionalTransitionRewardMatrix() const {
                return this->optionalTransitionRewardMatrix;
            }
            
            template<typename ValueType>
            StandardRewardModel<ValueType> StandardRewardModel<ValueType>::restrictActions(storm::storage::BitVector const& enabledActions) const {
                boost::optional<std::vector<ValueType>> newStateRewardVector(this->getOptionalStateRewardVector());
                boost::optional<std::vector<ValueType>> newStateActionRewardVector;
                if (this->hasStateActionRewards()) {
                    newStateActionRewardVector = std::vector<ValueType>(enabledActions.getNumberOfSetBits());
                    storm::utility::vector::selectVectorValues(newStateActionRewardVector.get(), enabledActions, this->getStateActionRewardVector());
                }
                boost::optional<storm::storage::SparseMatrix<ValueType>> newTransitionRewardMatrix;
                if (this->hasTransitionRewards()) {
                    newTransitionRewardMatrix = this->getTransitionRewardMatrix().restrictRows(enabledActions);
                }
                return StandardRewardModel(std::move(newStateRewardVector), std::move(newStateActionRewardVector), std::move(newTransitionRewardMatrix));
            }
            
            template<typename ValueType>
            template<typename MatrixValueType>
            void StandardRewardModel<ValueType>::reduceToStateBasedRewards(storm::storage::SparseMatrix<MatrixValueType> const& transitionMatrix, bool reduceToStateRewards) {
                if (this->hasTransitionRewards()) {
                    if (this->hasStateActionRewards()) {
                        storm::utility::vector::addVectors<ValueType>(this->getStateActionRewardVector(), transitionMatrix.getPointwiseProductRowSumVector(this->getTransitionRewardMatrix()), this->getStateActionRewardVector());
                        this->optionalTransitionRewardMatrix = boost::none;
                    } else {
                        this->optionalStateActionRewardVector = transitionMatrix.getPointwiseProductRowSumVector(this->getTransitionRewardMatrix());
                    }
                }
                
                if (reduceToStateRewards && this->hasStateActionRewards()) {
                    STORM_LOG_THROW(transitionMatrix.getRowGroupCount() == this->getStateActionRewardVector().size(), storm::exceptions::InvalidOperationException, "The reduction to state rewards is only possible if the size of the action reward vector equals the number of states.");
                    if (this->hasStateRewards()) {
                        storm::utility::vector::addVectors<ValueType>(this->getStateActionRewardVector(), this->getStateRewardVector(), this->getStateRewardVector());
                    } else {
                        this->optionalStateRewardVector = std::move(this->optionalStateActionRewardVector);
                    }
                    this->optionalStateActionRewardVector = boost::none;
                }
            }
            
            template<typename ValueType>
            template<typename MatrixValueType>
            std::vector<ValueType> StandardRewardModel<ValueType>::getTotalRewardVector(storm::storage::SparseMatrix<MatrixValueType> const& transitionMatrix) const {
                std::vector<ValueType> result = this->hasTransitionRewards() ? transitionMatrix.getPointwiseProductRowSumVector(this->getTransitionRewardMatrix()) : (this->hasStateActionRewards() ? this->getStateActionRewardVector() : std::vector<ValueType>(transitionMatrix.getRowCount()));
                if (this->hasStateActionRewards() && this->hasTransitionRewards()) {
                    storm::utility::vector::addVectors(result, this->getStateActionRewardVector(), result);
                }
                if (this->hasStateRewards()) {
                    storm::utility::vector::addVectorToGroupedVector(result, this->getStateRewardVector(), transitionMatrix.getRowGroupIndices());
                }
                return result;
            }
            
            template<typename ValueType>
            template<typename MatrixValueType>
            std::vector<ValueType> StandardRewardModel<ValueType>::getTotalRewardVector(storm::storage::SparseMatrix<MatrixValueType> const& transitionMatrix, std::vector<MatrixValueType> const& weights, bool scaleTransAndActions) const {
                std::vector<ValueType> result;
                if (this->hasTransitionRewards()) {
                    result = transitionMatrix.getPointwiseProductRowSumVector(this->getTransitionRewardMatrix());
                    storm::utility::vector::applyPointwise<MatrixValueType, ValueType, ValueType>(weights, this->getStateActionRewardVector(), result, [] (MatrixValueType const& weight, ValueType const& rewardElement, ValueType const& resultElement) { return weight * (resultElement + rewardElement); } );
                } else {
                    result = std::vector<ValueType>(transitionMatrix.getRowCount());
                    if (this->hasStateActionRewards()) {
                        storm::utility::vector::applyPointwise<MatrixValueType, ValueType, ValueType>(weights, this->getStateActionRewardVector(), result, [] (MatrixValueType const& weight, ValueType const& rewardElement, ValueType const& resultElement) { return weight * rewardElement; } );
                    }
                }
                if (this->hasStateRewards()) {
                    storm::utility::vector::addVectorToGroupedVector(result, this->getStateRewardVector(), transitionMatrix.getRowGroupIndices());
                }
                return result;
            }
            
            template<typename ValueType>
            template<typename MatrixValueType>
            std::vector<ValueType> StandardRewardModel<ValueType>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<MatrixValueType> const& transitionMatrix, storm::storage::BitVector const& filter) const {
                std::vector<ValueType> result(numberOfRows);
                if (this->hasTransitionRewards()) {
                    std::vector<ValueType> pointwiseProductRowSumVector = transitionMatrix.getPointwiseProductRowSumVector(this->getTransitionRewardMatrix());
                    storm::utility::vector::selectVectorValues(result, filter, transitionMatrix.getRowGroupIndices(), pointwiseProductRowSumVector);
                }

                if (this->hasStateActionRewards()) {
                    storm::utility::vector::addFilteredVectorGroupsToGroupedVector(result, this->getStateActionRewardVector(), filter, transitionMatrix.getRowGroupIndices());
                }
                if (this->hasStateRewards()) {
                    storm::utility::vector::addFilteredVectorToGroupedVector(result, this->getStateRewardVector(), filter, transitionMatrix.getRowGroupIndices());
                }
                return result;
            }
            
            template<typename ValueType>
            std::vector<ValueType> StandardRewardModel<ValueType>::getTotalStateActionRewardVector(uint_fast64_t numberOfRows, std::vector<uint_fast64_t> const& rowGroupIndices) const {
                std::vector<ValueType> result = this->hasStateActionRewards() ? this->getStateActionRewardVector() : std::vector<ValueType>(numberOfRows);
                if (this->hasStateRewards()) {
                    storm::utility::vector::addVectorToGroupedVector(result, this->getStateRewardVector(), rowGroupIndices);
                }
                return result;
            }
            
            template<typename ValueType>
            std::vector<ValueType> StandardRewardModel<ValueType>::getTotalStateActionRewardVector(uint_fast64_t numberOfRows, std::vector<uint_fast64_t> const& rowGroupIndices, storm::storage::BitVector const& filter) const {
                std::vector<ValueType> result(numberOfRows);
                if (this->hasStateRewards()) {
                    storm::utility::vector::selectVectorValuesRepeatedly(result, filter, rowGroupIndices, this->getStateRewardVector());
                }
                if (this->hasStateActionRewards()) {
                    storm::utility::vector::addFilteredVectorGroupsToGroupedVector(result, this->getStateActionRewardVector(), filter, rowGroupIndices);
                }
                return result;
            }
            
            template<typename ValueType>
            void StandardRewardModel<ValueType>::setStateActionRewardValue(uint_fast64_t row, ValueType const& value) {
                this->optionalStateActionRewardVector.get()[row] = value;
            }
            
            template<typename ValueType>
            bool StandardRewardModel<ValueType>::empty() const {
                return !(static_cast<bool>(this->optionalStateRewardVector) || static_cast<bool>(this->optionalStateActionRewardVector) || static_cast<bool>(this->optionalTransitionRewardMatrix));
            }
            
            template<typename ValueType>
            bool StandardRewardModel<ValueType>::isAllZero() const {
                if(hasStateRewards() && !std::all_of(getStateRewardVector().begin(), getStateRewardVector().end(), storm::utility::isZero<ValueType>)) {
                    return false;
                }
                if(hasStateActionRewards() && !std::all_of(getStateActionRewardVector().begin(), getStateActionRewardVector().end(), storm::utility::isZero<ValueType>)) {
                    return false;
                }
                if(hasTransitionRewards() && !std::all_of(getTransitionRewardMatrix().begin(), getTransitionRewardMatrix().end(), [](storm::storage::MatrixEntry<storm::storage::SparseMatrixIndexType, ValueType> entry){ return storm::utility::isZero(entry.getValue()); })) {
                    return false;
                }
                return true;
            }



            template<typename ValueType>
            bool StandardRewardModel<ValueType>::isCompatible(uint_fast64_t nrStates, uint_fast64_t nrChoices) const {
                if(hasStateRewards()) {
                    if(optionalStateRewardVector.get().size() != nrStates) return false;
                }
                if(hasStateActionRewards()) {
                    if(optionalStateActionRewardVector.get().size() != nrChoices) return false;
                }
                return true;
            }


            template<typename ValueType>
            std::size_t StandardRewardModel<ValueType>::getSizeInBytes() const {
                std::size_t result = 0;
                if (this->hasStateRewards()) {
                    result += this->getStateRewardVector().size() * sizeof(ValueType);
                }
                if (this->hasStateActionRewards()) {
                    result += this->getStateActionRewardVector().size() * sizeof(ValueType);
                }
                if (this->hasTransitionRewards()) {
                    result += this->getTransitionRewardMatrix().getSizeInBytes();
                }
                return result;
            }
            
            template <typename ValueType>
            std::ostream& operator<<(std::ostream& out, StandardRewardModel<ValueType> const& rewardModel) {
                out << std::boolalpha << "reward model [state reward: "
                << rewardModel.hasStateRewards()
                << ", state-action rewards: "
                << rewardModel.hasStateActionRewards()
                << ", transition rewards: "
                << rewardModel.hasTransitionRewards()
                << "]"
                << std::noboolalpha;
                return out;
            }

            std::set<storm::RationalFunctionVariable> getRewardModelParameters(StandardRewardModel<storm::RationalFunction> const& rewModel) {
                std::set<storm::RationalFunctionVariable> vars;
                if (rewModel.hasTransitionRewards()) {
                    vars = storm::storage::getVariables(rewModel.getTransitionRewardMatrix());
                }
                if (rewModel.hasStateActionRewards()) {
                    std::set<storm::RationalFunctionVariable> tmp =  storm::utility::vector::getVariables(rewModel.getStateActionRewardVector());
                    vars.insert(tmp.begin(), tmp.end());
                }
                if (rewModel.hasStateRewards()) {
                    std::set<storm::RationalFunctionVariable> tmp = storm::utility::vector::getVariables(rewModel.getStateRewardVector());
                    vars.insert(tmp.begin(), tmp.end());
                }
                return vars;

            }

            // Explicitly instantiate the class.
            template std::vector<double> StandardRewardModel<double>::getTotalRewardVector(storm::storage::SparseMatrix<double> const& transitionMatrix) const;
            template std::vector<double> StandardRewardModel<double>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<double> const& transitionMatrix, storm::storage::BitVector const& filter) const;
            template std::vector<double> StandardRewardModel<double>::getTotalRewardVector(storm::storage::SparseMatrix<double> const& transitionMatrix, std::vector<double> const& weights, bool scaleTransAndActions) const;
            template void StandardRewardModel<double>::reduceToStateBasedRewards(storm::storage::SparseMatrix<double> const& transitionMatrix, bool reduceToStateRewards);
            template void StandardRewardModel<double>::setStateActionReward(uint_fast64_t choiceIndex, double const & newValue);
            template void StandardRewardModel<double>::setStateReward(uint_fast64_t state, double const & newValue);
            template class StandardRewardModel<double>;
            template std::ostream& operator<<<double>(std::ostream& out, StandardRewardModel<double> const& rewardModel);
            
            template std::vector<float> StandardRewardModel<float>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<float> const& transitionMatrix, storm::storage::BitVector const& filter) const;
            template std::vector<float> StandardRewardModel<float>::getTotalRewardVector(storm::storage::SparseMatrix<float> const& transitionMatrix) const;
            template std::vector<float> StandardRewardModel<float>::getTotalRewardVector(storm::storage::SparseMatrix<float> const& transitionMatrix, std::vector<float> const& weights, bool scaleTransAndActions) const;
            template void StandardRewardModel<float>::reduceToStateBasedRewards(storm::storage::SparseMatrix<float> const& transitionMatrix, bool reduceToStateRewards);
            template void StandardRewardModel<float>::setStateActionReward(uint_fast64_t choiceIndex, float const & newValue);
            template void StandardRewardModel<float>::setStateReward(uint_fast64_t state, float const & newValue);
            template class StandardRewardModel<float>;
            template std::ostream& operator<<<float>(std::ostream& out, StandardRewardModel<float> const& rewardModel);

#ifdef STORM_HAVE_CARL
            template std::vector<storm::RationalNumber> StandardRewardModel<storm::RationalNumber>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<storm::RationalNumber> const& transitionMatrix, storm::storage::BitVector const& filter) const;
            template std::vector<storm::RationalNumber> StandardRewardModel<storm::RationalNumber>::getTotalRewardVector(storm::storage::SparseMatrix<storm::RationalNumber> const& transitionMatrix) const;
            template std::vector<storm::RationalNumber> StandardRewardModel<storm::RationalNumber>::getTotalRewardVector(storm::storage::SparseMatrix<storm::RationalNumber> const& transitionMatrix, std::vector<storm::RationalNumber> const& weights, bool scaleTransAndActions) const;
            template void StandardRewardModel<storm::RationalNumber>::reduceToStateBasedRewards(storm::storage::SparseMatrix<storm::RationalNumber> const& transitionMatrix, bool reduceToStateRewards);
            template void StandardRewardModel<storm::RationalNumber>::setStateActionReward(uint_fast64_t choiceIndex, storm::RationalNumber const & newValue);
            template void StandardRewardModel<storm::RationalNumber>::setStateReward(uint_fast64_t state, storm::RationalNumber const & newValue);
            template class StandardRewardModel<storm::RationalNumber>;
            template std::ostream& operator<<<storm::RationalNumber>(std::ostream& out, StandardRewardModel<storm::RationalNumber> const& rewardModel);
            
            template std::vector<storm::RationalFunction> StandardRewardModel<storm::RationalFunction>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<storm::RationalFunction> const& transitionMatrix, storm::storage::BitVector const& filter) const;
            template std::vector<storm::RationalFunction> StandardRewardModel<storm::RationalFunction>::getTotalRewardVector(storm::storage::SparseMatrix<storm::RationalFunction> const& transitionMatrix) const;
            template std::vector<storm::RationalFunction> StandardRewardModel<storm::RationalFunction>::getTotalRewardVector(storm::storage::SparseMatrix<storm::RationalFunction> const& transitionMatrix, std::vector<storm::RationalFunction> const& weights, bool scaleTransAndActions) const;
            template void StandardRewardModel<storm::RationalFunction>::reduceToStateBasedRewards(storm::storage::SparseMatrix<storm::RationalFunction> const& transitionMatrix, bool reduceToStateRewards);
            template void StandardRewardModel<storm::RationalFunction>::setStateActionReward(uint_fast64_t choiceIndex, storm::RationalFunction const & newValue);
            template void StandardRewardModel<storm::RationalFunction>::setStateReward(uint_fast64_t state, storm::RationalFunction const & newValue);
            template class StandardRewardModel<storm::RationalFunction>;
            template std::ostream& operator<<<storm::RationalFunction>(std::ostream& out, StandardRewardModel<storm::RationalFunction> const& rewardModel);
            
            template std::vector<storm::Interval> StandardRewardModel<storm::Interval>::getTotalRewardVector(uint_fast64_t numberOfRows, storm::storage::SparseMatrix<double> const& transitionMatrix, storm::storage::BitVector const& filter) const;
            template std::vector<storm::Interval> StandardRewardModel<storm::Interval>::getTotalRewardVector(storm::storage::SparseMatrix<double> const& transitionMatrix) const;
            template std::vector<storm::Interval> StandardRewardModel<storm::Interval>::getTotalRewardVector(storm::storage::SparseMatrix<double> const& transitionMatrix, std::vector<double> const& weights, bool scaleTransAndActions) const;
            template void StandardRewardModel<storm::Interval>::setStateActionReward(uint_fast64_t choiceIndex, double const & newValue);
            template void StandardRewardModel<storm::Interval>::setStateActionReward(uint_fast64_t choiceIndex, storm::Interval const & newValue);
            template void StandardRewardModel<storm::Interval>::setStateReward(uint_fast64_t state, double const & newValue);
            template void StandardRewardModel<storm::Interval>::setStateReward(uint_fast64_t state, storm::Interval const & newValue);
            template void StandardRewardModel<storm::Interval>::reduceToStateBasedRewards(storm::storage::SparseMatrix<double> const& transitionMatrix, bool reduceToStateRewards);
            template class StandardRewardModel<storm::Interval>;
            template std::ostream& operator<<<storm::Interval>(std::ostream& out, StandardRewardModel<storm::Interval> const& rewardModel);
#endif
        }
        
    }
}
