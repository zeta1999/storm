#pragma once

#include <cstdint>
#include <string>

#include "storm/storage/sparse/StateType.h"
#include "storm/storage/BitVector.h"
#include "storm/storage/expressions/Variable.h"
#include "storm/models/sparse/StateAnnotation.h"
#include "storm/adapters/JsonAdapter.h"

namespace storm {
    namespace storage {
        namespace sparse {
            
            class StateValuationsBuilder;
            
            // A structure holding information about the reachable state space that can be retrieved from the outside.
            class StateValuations : public storm::models::sparse::StateAnnotation {
            public:
                friend class StateValuationsBuilder;
                typedef storm::json<storm::RationalNumber> Json;

                class StateValuation {
                public:
                    friend class StateValuations;
                    StateValuation() = default;
                    StateValuation(std::vector<bool>&& booleanValues, std::vector<int64_t>&& integerValues, std::vector<storm::RationalNumber>&& rationalValues);
    
                private:
                    
                    std::vector<bool> booleanValues;
                    std::vector<int64_t> integerValues;
                    std::vector<storm::RationalNumber> rationalValues;
                };
                

                StateValuations() = default;
                
                virtual ~StateValuations() = default;
                virtual std::string getStateInfo(storm::storage::sparse::state_type const& state) const override;
                
                bool getBooleanValue(storm::storage::sparse::state_type const& stateIndex, storm::expressions::Variable const& booleanVariable) const;
                int64_t const& getIntegerValue(storm::storage::sparse::state_type const& stateIndex, storm::expressions::Variable const& integerVariable) const;
                storm::RationalNumber const& getRationalValue(storm::storage::sparse::state_type const& stateIndex, storm::expressions::Variable const& rationalVariable) const;
                /// Returns true, if this valuation does not contain any value.
                bool isEmpty(storm::storage::sparse::state_type const& stateIndex) const;
                
                /*!
                 * Returns a string representation of the valuation.
                 *
                 * @param selectedVariables If given, only the informations for the variables in this set are processed.
                 * @return The string representation.
                 */
                std::string toString(storm::storage::sparse::state_type const& stateIndex, bool pretty = true, boost::optional<std::set<storm::expressions::Variable>> const& selectedVariables = boost::none) const;
                
                /*!
                 * Returns a JSON representation of this valuation
                 * @param selectedVariables If given, only the informations for the variables in this set are processed.
                 * @return
                 */
                Json toJson(storm::storage::sparse::state_type const& stateIndex, boost::optional<std::set<storm::expressions::Variable>> const& selectedVariables = boost::none) const;

                
                // Returns the (current) number of states that this object describes.
                uint_fast64_t getNumberOfStates() const;
                
                /*
                 * Derive new state valuations from this by selecting the given states.
                 */
                StateValuations selectStates(storm::storage::BitVector const& selectedStates) const;
                
                /*
                 * Derive new state valuations from this by selecting the given states.
                 * If an invalid state index is selected, the corresponding valuation will be empty.
                 */
                StateValuations selectStates(std::vector<storm::storage::sparse::state_type> const& selectedStates) const;

                virtual std::size_t hash() const;
                
            private:
                StateValuations(std::map<storm::expressions::Variable, uint64_t> const& variableToIndexMap, std::vector<StateValuation>&& valuations);
                bool assertValuation(StateValuation const& valuation) const;
                StateValuation const& getValuation(storm::storage::sparse::state_type const& stateIndex) const;
                
                std::map<storm::expressions::Variable, uint64_t> variableToIndexMap;
                // A mapping from state indices to their variable valuations.
                std::vector<StateValuation> valuations;
                
            };
            
            class StateValuationsBuilder {
            public:
                StateValuationsBuilder();
                
                /*! Adds a new variable to keep track of for the state valuations.
                 *! All variables need to be added before adding new states.
                 */
                void addVariable(storm::expressions::Variable const& variable);
                
                /*!
                 * Adds a new state.
                 * The variable values have to be given in the same order as the variables have been added.
                 * The number of given variable values for each type needs to match the number of added variables.
                 * After calling this method, no more variables should be added.
                 */
                 void addState(storm::storage::sparse::state_type const& state, std::vector<bool>&& booleanValues = {}, std::vector<int64_t>&& integerValues = {}, std::vector<storm::RationalNumber>&& rationalValues = {});
                 
                 /*!
                  * Creates the finalized state valuations object.
                  */
                 StateValuations build(std::size_t totalStateCount);

            private:
                StateValuations currentStateValuations;
                uint64_t booleanVarCount;
                uint64_t integerVarCount;
                uint64_t rationalVarCount;
            };
        }
    }
}
