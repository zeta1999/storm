#ifndef MRMC_PARSER_TRAPARSER_H_
#define MRMC_PARSER_TRAPARSER_H_

#include "src/storage/SquareSparseMatrix.h"

#include "src/parser/Parser.h"
#include "src/utility/OsDetection.h"

#include <memory>

namespace mrmc {
namespace parser {
	
/*!
 *	@brief	Load transition system from file and return initialized
 *	StaticSparseMatrix object.
 *
 *	Note that this class creates a new StaticSparseMatrix object that can be
 *	accessed via getMatrix(). However, it does not delete this object!
 */
class TraParser : Parser {
	public:
		TraParser(const char* filename);
		
		std::shared_ptr<mrmc::storage::SquareSparseMatrix<double>> getMatrix() {
			return this->matrix;
		}
	
	private:
		std::shared_ptr<mrmc::storage::SquareSparseMatrix<double>> matrix;
		
		uint_fast64_t firstPass(char* buf, uint_fast64_t &maxnode);
	
};
		
} // namespace parser
} // namespace mrmc

#endif /* MRMC_PARSER_TRAPARSER_H_ */
