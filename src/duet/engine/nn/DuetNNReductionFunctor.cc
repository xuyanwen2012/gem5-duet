#include "duet/engine/nn/DuetNNReductionFunctor.hh"

#include "duet/engine/DuetEngine.hh"
#include "duet/engine/DuetLane.hh"

#include <iostream>

namespace gem5 {
namespace duet {

void DuetNNReductionFunctor::setup() {
  chan_id_t id = {chan_id_t::PULL, 0};
  _chan_input = &get_chan_data(id);

  id.tag = chan_id_t::REQ;
  _chan_req   = &get_chan_req ( id );

  id.tag = chan_id_t::WDATA;
  _chan_wdata = &get_chan_data ( id );

  _out_addr =
      lane->get_engine()->template get_constant<addr_t>(caller_id, "out_addr");
}

void DuetNNReductionFunctor::run() {
  std::cout << "void DuetNNReductionFunctor::run()" << std::endl;


    // // load argument
    // addr_t addr;
    // dequeue_data ( *chan_arg, addr );

    // // send load request
    // enqueue_req ( *chan_req, REQTYPE_LD, sizeof (uint64_t), addr );

    // // get response data
    // uint64_t data;
    // dequeue_data ( *chan_rdata, data );

    // do some computation

    uint64_t data = 666666666;

    // send store request
    enqueue_data ( *_chan_wdata, data ); // 0 
    enqueue_req ( *_chan_req, REQTYPE_ST, sizeof (uint64_t), _out_addr ); // 1


  std::cout << " ---------- _out_addr: " << _out_addr << std::endl;

  // Double tmp[1];
  // kernel(*_chan_input, _result, tmp[0]);
  // _result = tmp[0];
}

void DuetNNReductionFunctor::finishup() {
  // lane->get_engine()->template set_constant<Double>(caller_id, "result",
  //                                                   _result);

  uint64_t cnt =
      lane->get_engine()->template get_constant<uint64_t>(caller_id, "cnt");
  lane->get_engine()->template set_constant<uint64_t>(caller_id, "cnt", ++cnt);

  lane->get_engine()->stats_exec_done(caller_id);
}

}  // namespace duet
}  // namespace gem5
