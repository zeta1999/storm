#pragma once

#include "storm/models/sparse/Mdp.h"

#include "storm/models/sparse/StandardRewardModel.h"

namespace storm {
    namespace models {
        namespace sparse {

            /*!
             * This class represents a partially observable Markov decision process.
             */
            template<class ValueType, typename RewardModelType = StandardRewardModel <ValueType>>
            class Pomdp : public Mdp<ValueType, RewardModelType> {
            public:
                /*!
                 * Constructs a model from the given data.
                 *
                 * @param transitionMatrix The matrix representing the transitions in the model.
                 * @param stateLabeling The labeling of the states.
                 * @param rewardModels A mapping of reward model names to reward models.
                 */
                Pomdp(storm::storage::SparseMatrix<ValueType> const &transitionMatrix,
                      storm::models::sparse::StateLabeling const &stateLabeling,
                      std::unordered_map <std::string, RewardModelType> const &rewardModels = std::unordered_map<std::string, RewardModelType>());

                /*!
                 * Constructs a model by moving the given data.
                 *
                 * @param transitionMatrix The matrix representing the transitions in the model.
                 * @param stateLabeling The labeling of the states.
                 * @param rewardModels A mapping of reward model names to reward models.
                 */
                Pomdp(storm::storage::SparseMatrix<ValueType> &&transitionMatrix,
                      storm::models::sparse::StateLabeling &&stateLabeling,
                      std::unordered_map <std::string, RewardModelType> &&rewardModels = std::unordered_map<std::string, RewardModelType>());

                /*!
                 * Constructs a model from the given data.
                 *
                 * @param components The components for this model.
                 */
                Pomdp(storm::storage::sparse::ModelComponents<ValueType, RewardModelType> const &components, bool canonicFlag = false);

                Pomdp(storm::storage::sparse::ModelComponents<ValueType, RewardModelType> &&components, bool canonicFlag = false );

                Pomdp(Pomdp <ValueType, RewardModelType> const &other) = default;

                Pomdp &operator=(Pomdp <ValueType, RewardModelType> const &other) = default;

                Pomdp(Pomdp <ValueType, RewardModelType> &&other) = default;

                Pomdp &operator=(Pomdp <ValueType, RewardModelType> &&other) = default;

                virtual void printModelInformationToStream(std::ostream& out) const override;

                uint32_t getObservation(uint64_t state) const;

                uint64_t getNrObservations() const;

                std::vector<uint32_t> const& getObservations() const;

                bool isCanonic() const;



            protected:
                // TODO: consider a bitvector based presentation (depending on our needs).
                std::vector<uint32_t> observations;

                uint64_t nrObservations;

                bool canonicFlag = false;

                void computeNrObservations();
            };
        }
    }
}
