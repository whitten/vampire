
/*
 * File AWPassiveClauseContainer.cpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 *
 * In summary, you are allowed to use Vampire for non-commercial
 * purposes but not allowed to distribute, modify, copy, create derivatives,
 * or use in competitions. 
 * For other uses of Vampire please contact developers for a different
 * licence, which we will make an effort to provide. 
 */
/**
 * @file AWPassiveClauseContainer.cpp
 * Implements class AWPassiveClauseContainer for the queue of passive clauses.
 * @since 30/12/2007 Manchester
 */

#include <math.h>

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Timer.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/TermIterators.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/Options.hpp"

#include "SaturationAlgorithm.hpp"

#if VDEBUG
#include <iostream>
#endif

#include "AWPassiveClauseContainer.hpp"

namespace Saturation
{
using namespace Lib;
using namespace Kernel;


AWPassiveClauseContainer::AWPassiveClauseContainer(const Options& opt)
:  _ageQueue(opt), _weightQueue(opt), _balance(0), _size(0), _opt(opt)
{
  CALL("AWPassiveClauseContainer::AWPassiveClauseContainer");

  if(_opt.ageWeightRatioShape() == Options::AgeWeightRatioShape::CONVERGE) {
    _ageRatio = 1;
    _weightRatio = 1;
  }
  else {
    _ageRatio = _opt.ageRatio();
    _weightRatio = _opt.weightRatio();
  }
  ASS_GE(_ageRatio, 0);
  ASS_GE(_weightRatio, 0);
  ASS(_ageRatio > 0 || _weightRatio > 0);
}

AWPassiveClauseContainer::~AWPassiveClauseContainer()
{
  ClauseQueue::Iterator cit(_ageQueue);
  while (cit.hasNext()) {
    Clause* cl=cit.next();
    ASS(cl->store()==Clause::PASSIVE);
    cl->setStore(Clause::NONE);
  }
}

ClauseIterator AWPassiveClauseContainer::iterator()
{
  return pvi( ClauseQueue::Iterator(_weightQueue) );
}

/**
 * Weight comparison of clauses.
 * @return the result of comparison (LESS, EQUAL or GREATER)
 * @warning if the option increased_numeral_weight is on, then each comparison
 *          recomputes the numeral weight of clauses, see Clause::getNumeralWeight(), so it
 *          it can be expensive
 */
Comparison AWPassiveClauseContainer::compareWeight(Clause* cl1, Clause* cl2, const Options& opt)
{
  CALL("AWPassiveClauseContainer::compareWeight");
  
  return Int::compare(cl1->weightForClauseSelection(opt), cl2->weightForClauseSelection(opt));
}

/**
 * Comparison of clauses. The comparison uses four orders in the
 * following order:
 * <ol><li>by weight;</li>
 *     <li>by age;</li>
 *     <li>by input type;</li>
 *     <li>by number.</li>
 * </ol>
 * @since 30/12/2007 Manchester
 */
bool WeightQueue::lessThan(Clause* c1,Clause* c2)
{
  CALL("WeightQueue::lessThan");

  Comparison weightCmp=AWPassiveClauseContainer::compareWeight(c1, c2, _opt);
  if (weightCmp!=EQUAL) {
    return weightCmp==LESS;
  }

  if (c1->age() < c2->age()) {
    return true;
  }
  if (c2->age() < c1->age()) {
    return false;
  }
  if (c1->inputType() < c2->inputType()) {
    return false;
  }
  if (c2->inputType() < c1->inputType()) {
    return true;
  }
  return c1->number() < c2->number();
} // WeightQueue::lessThan


/**
 * Comparison of clauses. The comparison uses four orders in the
 * following order:
 * <ol><li>by age;</li>
 *     <li>by weight;</li>
 *     <li>by input type;</li>
 *     <li>by number.</li>
 * </ol>
 * @since 30/12/2007 Manchester
 */
bool AgeQueue::lessThan(Clause* c1,Clause* c2)
{
  CALL("AgeQueue::lessThan");

  if (c1->age() < c2->age()) {
    return true;
  }
  if (c2->age() < c1->age()) {
    return false;
  }

  Comparison weightCmp=AWPassiveClauseContainer::compareWeight(c1, c2, _opt);
  if (weightCmp!=EQUAL) {
    return weightCmp==LESS;
  }

  if (c1->inputType() < c2->inputType()) {
    return false;
  }
  if (c2->inputType() < c1->inputType()) {
    return true;
  }

  return c1->number() < c2->number();
} // WeightQueue::lessThan

/**
 * Add @b c clause in the queue.
 * @since 31/12/2007 Manchester
 */
void AWPassiveClauseContainer::add(Clause* cl)
{
  CALL("AWPassiveClauseContainer::add");
  ASS(_ageRatio > 0 || _weightRatio > 0);

  if (_ageRatio) {
    _ageQueue.insert(cl);
  }
  if (_weightRatio) {
    _weightQueue.insert(cl);
  }
  _size++;
  addedEvent.fire(cl);
} // AWPassiveClauseContainer::add

/**
 * Remove Clause from the Passive store. Should be called only
 * when the Clause is no longer needed by the inference process
 * (i.e. was backward subsumed/simplified), as it can result in
 * deletion of the clause.
 */
void AWPassiveClauseContainer::remove(Clause* cl)
{
  CALL("AWPassiveClauseContainer::remove");
  ASS(cl->store()==Clause::PASSIVE);

  if (_ageRatio) {
    ALWAYS(_ageQueue.remove(cl));
  }
  if (_weightRatio) {
    ALWAYS(_weightQueue.remove(cl));
  }
  _size--;

  removedEvent.fire(cl);

  ASS(cl->store()!=Clause::PASSIVE);
}


/**
 * Return the next selected clause and remove it from the queue.
 * @since 31/12/2007 Manchester
 */
Clause* AWPassiveClauseContainer::popSelected()
{
  CALL("AWPassiveClauseContainer::popSelected");
  ASS( ! isEmpty());

  auto shape = _opt.ageWeightRatioShape();
  unsigned frequency = _opt.ageWeightRatioShapeFrequency();
  static unsigned count = 0;
  count++;

  bool is_converging = shape == Options::AgeWeightRatioShape::CONVERGE;
  int targetAgeRatio = is_converging ? _opt.ageRatio() : 1;
  int targetWeightRatio = is_converging ? _opt.weightRatio() : 1;

  if(count % frequency == 0) {
    switch(shape) {
    case Options::AgeWeightRatioShape::CONSTANT:
      break;
    case Options::AgeWeightRatioShape::DECAY:
    case Options::AgeWeightRatioShape::CONVERGE:
      int ageDifference = targetAgeRatio - _ageRatio;
      int weightDifference = targetWeightRatio - _weightRatio;
      int bonus = is_converging ? 1 : -1;
      int ageUpdate = (ageDifference + bonus) / 2;
      int weightUpdate = (weightDifference + bonus) / 2;

      _ageRatio += ageUpdate;
      _weightRatio += weightUpdate;
   }
  }
  //std::cerr << _ageRatio << "\t" << _weightRatio << std::endl;
  _size--;

  bool byWeight;
  if (! _ageRatio) {
    byWeight = true;
  }
  else if (! _weightRatio) {
    byWeight = false;
  }
  else if (_balance > 0) {
    byWeight = true;
  }
  else if (_balance < 0) {
    byWeight = false;
  }
  else {
    byWeight = (_ageRatio <= _weightRatio);
  }

  if (byWeight) {
    _balance -= _ageRatio;
    Clause* cl = _weightQueue.pop();
    _ageQueue.remove(cl);
    selectedEvent.fire(cl);
    return cl;
  }
  _balance += _weightRatio;
  Clause* cl = _ageQueue.pop();
  _weightQueue.remove(cl);
  selectedEvent.fire(cl);
  return cl;
} // AWPassiveClauseContainer::popSelected



void AWPassiveClauseContainer::updateLimits(long long estReachableCnt)
{
  CALL("AWPassiveClauseContainer::updateLimits");
  ASS_GE(estReachableCnt,0);

  unsigned maxAgeQueueAge;
  unsigned maxAgeQueueWeight;
  unsigned maxWeightQueueWeight;
  unsigned maxWeightQueueAge;

  if (estReachableCnt > static_cast<long long>(_size)) {
    maxAgeQueueAge = UINT_MAX;
    maxAgeQueueWeight = UINT_MAX;
    maxWeightQueueWeight = UINT_MAX;
    maxWeightQueueAge = UINT_MAX;
    goto fin;
  }

  {
    ClauseQueue::Iterator wit(_weightQueue);
    ClauseQueue::Iterator ait(_ageQueue);

    if (!wit.hasNext() && !ait.hasNext()) {
      //passive container is empty
      return;
    }

    long long remains=estReachableCnt;
    Clause* wcl=0;
    Clause* acl=0;
    if (_ageRatio==0 || (_opt.lrsWeightLimitOnly() && _weightRatio!=0) ) {
      ASS(wit.hasNext());
      while ( remains && wit.hasNext() ) {
        wcl=wit.next();
        remains--;
      }
    } else if (_weightRatio==0) {
      ASS(ait.hasNext());
      while ( remains && ait.hasNext() ) {
        acl=ait.next();
        remains--;
      }
    } else {
      ASS(wit.hasNext()&&ait.hasNext());

      int balance=(_ageRatio<=_weightRatio)?1:0;
      while (remains) {
        ASS_G(remains,0);
        if ( (balance>0 || !ait.hasNext()) && wit.hasNext()) {
          wcl=wit.next();
          if (!acl || _ageQueue.lessThan(acl, wcl)) {
            balance-=_ageRatio;
            remains--;
          }
        } else if (ait.hasNext()){
          acl=ait.next();
          if (!wcl || _weightQueue.lessThan(wcl, acl)) {
            balance+=_weightRatio;
            remains--;
          }
        } else {
          break;
        }
      }
    }

    //when _ageRatio==0, the age limit can be set to zero, as age doesn't matter
    maxAgeQueueAge = (_ageRatio && acl!=0) ? UINT_MAX : 0;
    maxAgeQueueWeight = (_ageRatio && acl!=0) ? UINT_MAX : 0;
    maxWeightQueueWeight = (_weightRatio && wcl!=0) ? UINT_MAX : 0;
    maxWeightQueueAge = (_weightRatio && wcl!=0) ? UINT_MAX : 0;
    if (acl!=0 && ait.hasNext()) {
      maxAgeQueueAge = acl->age();
      maxAgeQueueWeight = static_cast<int>(ceil(acl->weightForClauseSelection(_opt)));
    }
    if (wcl!=0 && wit.hasNext()) {
      maxWeightQueueWeight = static_cast<int>(ceil(wcl->weightForClauseSelection(_opt)));
      maxWeightQueueAge = wcl->age();
    }
  }

fin:
#if OUTPUT_LRS_DETAILS
  cout<<env.timer->elapsedDeciseconds()<<"\tLimits to "<<maxAge<<"\t"<<maxWeight<<"\t by est "<<estReachableCnt<<"\n";
#endif

  bool atLeastOneLimitTightened = getSaturationAlgorithm()->getLimits()->setLimits(maxAgeQueueAge, maxAgeQueueWeight,maxWeightQueueWeight, maxWeightQueueAge);
  if (atLeastOneLimitTightened) {
    onLimitsUpdated();
    getSaturationAlgorithm()->getLimits()->changedEvent.fire();
  }
}

void AWPassiveClauseContainer::onLimitsUpdated()
{
  CALL("AWPassiveClauseContainer::onLimitsUpdated");

  Limits* limits=getSaturationAlgorithm()->getLimits();
  if ( (!limits->ageLimited() && _ageRatio) || (!limits->weightLimited() && _weightRatio) ) {
    return;
  }

  //Here we rely on (and maintain) the invariant, that
  //_weightQueue and _ageQueue contain the same set
  //of clauses, differing only in their order.
  //(unless one of _ageRation or _weightRatio is equal to 0)

  static Stack<Clause*> toRemove(256);
  ClauseQueue::Iterator wit(_weightQueue);
  while (wit.hasNext()) {
    Clause* cl=wit.next();
    if (!limits->fulfilsAgeLimit(cl) && !limits->fulfilsWeightLimit(cl)) {
      toRemove.push(cl);
    } else if (!limits->childrenPotentiallyFulfilLimits(cl, cl->length())) {
      toRemove.push(cl);
    }
  }

#if OUTPUT_LRS_DETAILS
  if (toRemove.isNonEmpty()) {
    cout<<toRemove.size()<<" passive deleted, "<< (size()-toRemove.size()) <<" remains\n";
  }
#endif

  while (toRemove.isNonEmpty()) {
    Clause* removed=toRemove.pop();
    RSTAT_CTR_INC("clauses discarded from passive on weight limit update");
    env.statistics->discardedNonRedundantClauses++;
    remove(removed);
  }
}

AWClauseContainer::AWClauseContainer(const Options& opt)
: _ageQueue(opt), _weightQueue(opt), _ageRatio(1), _weightRatio(1), _balance(0), _size(0)
{
}

bool AWClauseContainer::isEmpty() const
{
  CALL("AWClauseContainer::isEmpty");

  ASS(!_ageRatio || !_weightRatio || _ageQueue.isEmpty()==_weightQueue.isEmpty());
  return _ageQueue.isEmpty() && _weightQueue.isEmpty();
}

/**
 * Add @b c clause in the queue.
 * @since 31/12/2007 Manchester
 */
void AWClauseContainer::add(Clause* cl)
{
  CALL("AWClauseContainer::add");
  ASS(_ageRatio > 0 || _weightRatio > 0);

  if (_ageRatio) {
    _ageQueue.insert(cl);
  }
  if (_weightRatio) {
    _weightQueue.insert(cl);
  }
  _size++;
  addedEvent.fire(cl);
}

/**
 * Remove Clause from the container.
 */
bool AWClauseContainer::remove(Clause* cl)
{
  CALL("AWClauseContainer::remove");

  bool removed;
  if (_ageRatio) {
    removed = _ageQueue.remove(cl);
    if (_weightRatio) {
      ALWAYS(_weightQueue.remove(cl)==removed);
    }
  }
  else {
    ASS(_weightRatio);
    removed = _weightQueue.remove(cl);
  }

  if (removed) {
    _size--;
    removedEvent.fire(cl);
  }
  return removed;
}


/**
 * Return the next selected clause and remove it from the queue.
 */
Clause* AWClauseContainer::popSelected()
{
  CALL("AWClauseContainer::popSelected");
  ASS( ! isEmpty());

  _size--;

  bool byWeight;
  if (! _ageRatio) {
    byWeight = true;
  }
  else if (! _weightRatio) {
    byWeight = false;
  }
  else if (_balance > 0) {
    byWeight = true;
  }
  else if (_balance < 0) {
    byWeight = false;
  }
  else {
    byWeight = (_ageRatio <= _weightRatio);
  }

  Clause* cl;
  if (byWeight) {
    _balance -= _ageRatio;
    cl = _weightQueue.pop();
    ALWAYS(_ageQueue.remove(cl));
  }
  else {
    _balance += _weightRatio;
    cl = _ageQueue.pop();
    ALWAYS(_weightQueue.remove(cl));
  }
  selectedEvent.fire(cl);
  return cl;
}



}
