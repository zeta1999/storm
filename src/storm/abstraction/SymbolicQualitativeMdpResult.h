#pragma once

#include "storm/abstraction/SymbolicQualitativeResult.h"

#include "storm/storage/dd/Bdd.h"

namespace storm {
    namespace abstraction {
        
        template <storm::dd::DdType Type>
        class SymbolicQualitativeMdpResult : public SymbolicQualitativeResult<Type> {
        public:
            SymbolicQualitativeMdpResult() = default;
            
            SymbolicQualitativeMdpResult(storm::dd::Bdd<Type> const& states);
            
            virtual storm::dd::Bdd<Type> const& getStates() const override;
            
            storm::dd::Bdd<Type> states;
        };
        
    }
}

