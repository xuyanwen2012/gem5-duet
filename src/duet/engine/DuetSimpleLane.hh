#ifndef __DUET_SIMPLE_LANE_HH
#define __DUET_SIMPLE_LANE_HH

#include "params/DuetSimpleLane.hh"
#include "duet/engine/DuetLane.hh"

namespace gem5 {
namespace duet {

class DuetSimpleLane : public DuetLane {
protected:
    std::unique_ptr <DuetFunctor>   _functor;
    Cycles                          _remaining;

// ===========================================================================
// == API for subclesses =====================================================
// ===========================================================================
public:
    DuetSimpleLane ( const DuetSimpleLaneParams & p );

// ===========================================================================
// == Implementing virtual methods ===========================================
// ===========================================================================
public:
    void pull_phase () override final;
    void push_phase () override final;
    bool has_work () override final;

// ===========================================================================
// == Internal ===============================================================
// ===========================================================================
private:
    void _advance ();
};

}   // namespace gem5
}   // namespace duet

#endif /* #ifndef __DUET_SIMPLE_LANE_HH */
