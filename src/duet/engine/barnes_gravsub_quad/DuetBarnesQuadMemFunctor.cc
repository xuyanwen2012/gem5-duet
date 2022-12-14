#include "duet/engine/barnes_gravsub_quad/DuetBarnesQuadMemFunctor.hh"
#include "duet/engine/DuetLane.hh"
#include "duet/engine/DuetEngine.hh"

namespace gem5 {
namespace duet {

void DuetBarnesQuadMemFunctor::setup () {
    chan_id_t id = { chan_id_t::ARG, 0 };
    _chan_arg = &get_chan_data ( id );

    id.tag = chan_id_t::REQ;
    _chan_req = &get_chan_req ( id );
}

void DuetBarnesQuadMemFunctor::run () {
    kernel ( *_chan_arg, *_chan_req );
}

}   // namespace duet
}   // namespace gem5
