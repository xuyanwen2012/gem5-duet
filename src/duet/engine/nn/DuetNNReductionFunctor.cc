#include "duet/engine/nn/DuetNNReductionFunctor.hh"

#include "duet/engine/DuetEngine.hh"
#include "duet/engine/DuetLane.hh"

#include <iostream>
#include <algorithm>
#include <array>

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
      lane->get_engine()->template get_constant<uint64_t>(caller_id, "out_addr");
}

void DuetNNReductionFunctor::run() {
  std::cout << "void DuetNNReductionFunctor::run()" << std::endl;

  // Double sorted[32];
  std::array<Double, 32> sorted; 

  for (int i = 0; i < 16; ++i ) {
        Block<16> tmp;
        dequeue_data ( *_chan_input, tmp ); // 0

        #pragma unroll yes
        for ( int j = 0; j < 2; ++j ) {
            Double din;
            unpack ( tmp, j, din );
            sorted[i*2+j] = din;
        }
  }
  
  std::sort(sorted.begin(), sorted.end());

  for(int i = 0; i < 32; ++i){
    std::cout << i << ": " << sorted[i] << std::endl;
  }

  for (int j = 0; j < 4; ++j)
  {
    Block<64> tmp;
    for(int i = 0; i < 8; ++ i)
      {
        pack ( tmp, i,  sorted[j * 4 + i]);
      }
    enqueue_data ( *_chan_wdata, tmp );                    // 1
    enqueue_req ( *_chan_req, REQTYPE_ST, 64, _out_addr + j * 64 ); // 2
  }

  // // send store request
  // enqueue_data ( *_chan_wdata, tmp ); // 0 
  // enqueue_req ( *_chan_req, REQTYPE_ST, 64, _out_addr ); // 1

  std::cout << " ---------- _out_addr: " << _out_addr << std::endl;
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
