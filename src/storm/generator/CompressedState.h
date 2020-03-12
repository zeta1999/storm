#ifndef STORM_GENERATOR_COMPRESSEDSTATE_H_
#define STORM_GENERATOR_COMPRESSEDSTATE_H_

#include "storm/storage/BitVector.h"
#include <unordered_map>

namespace storm {
    namespace expressions {
        template<typename ValueType> class ExpressionEvaluator;
        
        class ExpressionManager;
        class SimpleValuation;
    }
    
    namespace generator {
        typedef storm::storage::BitVector CompressedState;

        struct VariableInformation;
        
        /*!
         * Unpacks the compressed state into the evaluator.
         *
         * @param state The state to unpack.
         * @param variableInformation The information about how the variables are packed within the state.
         * @param evaluator The evaluator into which to load the state.
         */
        template<typename ValueType>
        void unpackStateIntoEvaluator(CompressedState const& state, VariableInformation const& variableInformation, storm::expressions::ExpressionEvaluator<ValueType>& evaluator);

        /*!
         * Converts the compressed state into an explicit representation in the form of a valuation.
         *
         * @param state The state to unpack.
         * @param variableInformation The information about how the variables are packed within the state.
         * @param manager The manager responsible for the variables.
         * @return A valuation that corresponds to the compressed state.
         */
        storm::expressions::SimpleValuation unpackStateIntoValuation(CompressedState const& state, VariableInformation const& variableInformation, storm::expressions::ExpressionManager const& manager);

        /*!
         *
         * @param variableInformation
         * @return
         */
        storm::storage::BitVector computeObservabilityMask(VariableInformation const& variableInformation);
        /*!
         *
         * @param state
         * @param observabilityMap
         * @param mask
         * @return
         */
        uint32_t unpackStateToObservabilityClass(CompressedState const& state,  storm::storage::BitVector const& observationVector, std::unordered_map<storm::storage::BitVector,uint32_t>& observabilityMap, storm::storage::BitVector const& mask);
        /*!
         *
         * @param varInfo
         * @param roundTo64Bit
         * @return
         */
        CompressedState createOutOfBoundsState(VariableInformation const& varInfo, bool roundTo64Bit = true);

    }
}

#endif /* STORM_GENERATOR_COMPRESSEDSTATE_H_ */

