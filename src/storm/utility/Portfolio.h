#pragma once

#include "storm/utility/Engine.h"

namespace storm {
    
    namespace jani {
        class Property;
    }
    
    namespace storage {
        class SymbolicModelDescription;
    }
    
    namespace utility {
        class Portfolio {
        public:
            Portfolio();
            
            /*!
             * Predicts "good" settings for the provided model checking query
             */
            void predict(storm::storage::SymbolicModelDescription const& modelDescription, storm::jani::Property const& property);
            
            /*!
             * Predicts "good" settings for the provided model checking query
             * @param stateEstimate A hint that gives a (rough) estimate for the number of states.
             */
            void predict(storm::storage::SymbolicModelDescription const& modelDescription, storm::jani::Property const& property, uint64_t stateEstimate);

            /// Retrieve "good" settings after calling predict.
            storm::utility::Engine getEngine() const;
            bool enableBisimulation() const;
            bool enableExact() const;
            
        private:
            storm::utility::Engine engine;
            bool bisimulation;
            bool exact;
            
            
            
        };
        
    }
}