/**
 * @file TermIndex.cpp
 * Implements class TermIndex.
 */

#include "../Lib/DHSet.hpp"

#include "../Kernel/Clause.hpp"
#include "../Kernel/EqHelper.hpp"
#include "../Kernel/Ordering.hpp"
#include "../Kernel/Term.hpp"
#include "../Kernel/TermIterators.hpp"


#include "TermIndexingStructure.hpp"

#include "TermIndex.hpp"

using namespace Lib;
using namespace Kernel;
using namespace Inferences;
using namespace Indexing;

TermIndex::~TermIndex()
{
  delete _is;
}

TermQueryResultIterator TermIndex::getUnifications(TermList t,
	  bool retrieveSubstitutions)
{
  return _is->getUnifications(t, retrieveSubstitutions);
}

TermQueryResultIterator TermIndex::getGeneralizations(TermList t,
	  bool retrieveSubstitutions)
{
  return _is->getGeneralizations(t, retrieveSubstitutions);
}

TermQueryResultIterator TermIndex::getInstances(TermList t,
	  bool retrieveSubstitutions)
{
  return _is->getInstances(t, retrieveSubstitutions);
}


void SuperpositionSubtermIndex::handleClause(Clause* c, bool adding)
{
  CALL("SuperpositionSubtermIndex::handleClause");

  TimeCounter tc(TC_BACKWARD_SUPERPOSITION_INDEX_MAINTENANCE);

  unsigned selCnt=c->selected();
  for(unsigned i=0; i<selCnt; i++) {
    Literal* lit=(*c)[i];
    TermIterator rsti=EqHelper::getRewritableSubtermIterator(lit);
    while(rsti.hasNext()) {
      if(adding) {
	_is->insert(rsti.next(), lit, c);
      } else {
	_is->remove(rsti.next(), lit, c);
      }
    }
  }
}


void SuperpositionLHSIndex::handleClause(Clause* c, bool adding)
{
  CALL("SuperpositionLHSIndex::handleClause");

  TimeCounter tc(TC_FORWARD_SUPERPOSITION_INDEX_MAINTENANCE);

  unsigned selCnt=c->selected();
  for(unsigned i=0; i<selCnt; i++) {
    Literal* lit=(*c)[i];
    TermIterator lhsi=EqHelper::getSuperpositionLHSIterator(lit);
    while(lhsi.hasNext()) {
      TermList lhs=lhsi.next();
      if(adding) {
	_is->insert(lhs, lit, c);
      } else {
	_is->remove(lhs, lit, c);
      }
    }
  }
}

void DemodulationSubtermIndex::handleClause(Clause* c, bool adding)
{
  CALL("DemodulationSubtermIndex::handleClause");

  TimeCounter tc(TC_BACKWARD_DEMODULATION_INDEX_MAINTENANCE);

  static DHSet<TermList> inserted;

  unsigned cLen=c->length();
  for(unsigned i=0; i<cLen; i++) {
    inserted.reset();
    Literal* lit=(*c)[i];
    NonVariableIterator nvi(lit);
    while(nvi.hasNext()) {
      TermList t=nvi.next();
      if(!inserted.insert(t)) {
	//It is enough to insert a term only once per clause.
	//Also, once we know term was inserted, we know that all its
	//subterms were inserted as well, so we can skip them.
	nvi.right();
	continue;
      }
      if(adding) {
	_is->insert(t, lit, c);
      } else {
	_is->remove(t, lit, c);
      }
    }
  }
}


void DemodulationLHSIndex::handleClause(Clause* c, bool adding)
{
  CALL("DemodulationLHSIndex::handleClause");

  if(c->length()!=1) {
    return;
  }

  TimeCounter tc(TC_FORWARD_DEMODULATION_INDEX_MAINTENANCE);

  Literal* lit=(*c)[0];
  TermIterator lhsi=EqHelper::getDemodulationLHSIterator(lit);
  while(lhsi.hasNext()) {
    if(adding) {
      _is->insert(lhsi.next(), lit, c);
    } else {
      _is->remove(lhsi.next(), lit, c);
    }
  }
}
