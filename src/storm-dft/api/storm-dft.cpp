#include "storm-dft/api/storm-dft.h"

#include "storm-dft/settings/modules/FaultTreeSettings.h"
#include "storm-dft/settings/modules/DftGspnSettings.h"

namespace storm {
    namespace api {

        template<>
        void exportDFTToJsonFile(storm::storage::DFT<double> const& dft, std::string const& file) {
            storm::storage::DftJsonExporter<double>::toFile(dft, file);
        }

        template<>
        void exportDFTToJsonFile(storm::storage::DFT<storm::RationalFunction> const& dft, std::string const& file) {
            STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "Export to JSON not supported for this data type.");
        }

        template<>
        std::string exportDFTToJsonString(storm::storage::DFT<double> const& dft) {
            std::stringstream stream;
            storm::storage::DftJsonExporter<double>::toStream(dft, stream);
            return stream.str();
        }

        template<>
        std::string exportDFTToJsonString(storm::storage::DFT<storm::RationalFunction> const& dft) {
            STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "Export to JSON not supported for this data type.");
        }

        template<>
        void exportDFTToSMT(storm::storage::DFT<double> const& dft, std::string const& file) {
            storm::modelchecker::DFTASFChecker asfChecker(dft);
            asfChecker.convert();
            asfChecker.toFile(file);
        }

        template<>
        void exportDFTToSMT(storm::storage::DFT<storm::RationalFunction> const& dft, std::string const& file) {
            STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "Export to SMT does not support this data type.");
        }

        template<>
        std::pair<std::shared_ptr<storm::gspn::GSPN>, uint64_t> transformToGSPN(storm::storage::DFT<double> const& dft) {
            storm::settings::modules::FaultTreeSettings const& ftSettings = storm::settings::getModule<storm::settings::modules::FaultTreeSettings>();
            storm::settings::modules::DftGspnSettings const& dftGspnSettings = storm::settings::getModule<storm::settings::modules::DftGspnSettings>();

            // Set Don't Care elements
            std::set<uint64_t> dontCareElements;
            if (!ftSettings.isDisableDC()) {
                // Insert all elements as Don't Care elements
                for (std::size_t i = 0; i < dft.nrElements(); i++) {
                    dontCareElements.insert(dft.getElement(i)->id());
                }
            }

            // Transform to GSPN
            storm::transformations::dft::DftToGspnTransformator<double> gspnTransformator(dft);
            auto priorities = gspnTransformator.computePriorities(dftGspnSettings.isExtendPriorities());
            gspnTransformator.transform(priorities, dontCareElements, !dftGspnSettings.isDisableSmartTransformation(),
                                        dftGspnSettings.isMergeDCFailed(), dftGspnSettings.isExtendPriorities());
            std::shared_ptr<storm::gspn::GSPN> gspn(gspnTransformator.obtainGSPN());
            return std::make_pair(gspn, gspnTransformator.toplevelFailedPlaceId());
        }

        std::shared_ptr<storm::jani::Model> transformToJani(storm::gspn::GSPN const& gspn, uint64_t toplevelFailedPlace) {
            storm::builder::JaniGSPNBuilder builder(gspn);
            std::shared_ptr<storm::jani::Model> model(builder.build());

            std::shared_ptr<storm::expressions::ExpressionManager> const& exprManager = gspn.getExpressionManager();
            storm::jani::Variable const& topfailedVar = builder.getPlaceVariable(toplevelFailedPlace);
            storm::expressions::Expression targetExpression = exprManager->integer(1) == topfailedVar.getExpressionVariable().getExpression();
            STORM_LOG_TRACE("Target expression: " << targetExpression);

            auto evtlFormula = std::make_shared<storm::logic::AtomicExpressionFormula>(targetExpression);
            auto tbFormula = std::make_shared<storm::logic::BoundedUntilFormula>(std::make_shared<storm::logic::BooleanLiteralFormula>(true), evtlFormula, storm::logic::TimeBound(false, exprManager->integer(0)), storm::logic::TimeBound(false, exprManager->integer(10)), storm::logic::TimeBoundReference(storm::logic::TimeBoundType::Time));
            auto tbUntil = std::make_shared<storm::logic::ProbabilityOperatorFormula>(tbFormula);
            auto evFormula = std::make_shared<storm::logic::EventuallyFormula>(evtlFormula, storm::logic::FormulaContext::Time);
            auto rewFormula = std::make_shared<storm::logic::TimeOperatorFormula>(evFormula, storm::logic::OperatorInformation(), storm::logic::RewardMeasureType::Expectation);

            auto timeBoundedProperty = storm::jani::Property("time-bounded", tbUntil);
            auto mttfProperty = storm::jani::Property("mttf", rewFormula);
            storm::settings::modules::JaniExportSettings const& janiSettings = storm::settings::getModule<storm::settings::modules::JaniExportSettings>();
            if (janiSettings.isJaniFileSet()) {
                // Currently no properties are set
                storm::api::exportJaniModel(*model, {}, janiSettings.getJaniFilename());
            }

            return model;
        }

        template<>
        std::pair<std::shared_ptr<storm::gspn::GSPN>, uint64_t> transformToGSPN(storm::storage::DFT<storm::RationalFunction> const& dft) {
            STORM_LOG_THROW(false, storm::exceptions::NotSupportedException, "Transformation to GSPN not supported for this data type.");
        }

    }
}
