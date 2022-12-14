#ifndef __DUET_ASYNC_FIFO_HH
#define __DUET_ASYNC_FIFO_HH

#include <list>

#include "params/DuetAsyncFIFO.hh"
#include "sim/sim_object.hh"
#include "mem/port.hh"

namespace gem5 {
namespace duet {

class DuetAsyncFIFOCtrl;
class DuetAsyncFIFO : public SimObject {
private:

    // befriend my intended subclass
    friend class DuetAsyncFIFOCtrl;

    /* Port to Upstream */
    class UpstreamPort : public ResponsePort {
    private:
        DuetAsyncFIFO * _owner;

    public:
        UpstreamPort ( const std::string & name
                , DuetAsyncFIFO * owner
                , PortID id = InvalidPortID
                )
            : ResponsePort ( name, owner, id )
            , _owner ( owner )
        {}

        AddrRangeList getAddrRanges() const override {
            return _owner->_downstream_port.getAddrRanges ();
        }

        Tick recvAtomic ( PacketPtr pkt ) override {
            panic ( "recvAtomic unimpl." );
        }

    public:
        void recvFunctional      ( PacketPtr pkt ) override;
        bool recvTimingReq       ( PacketPtr pkt ) override;
        void recvRespRetry       () override;
        bool recvTimingSnoopResp ( PacketPtr pkt ) override;
    };

    /* Port to Downstream */
    class DownstreamPort : public RequestPort {
    private:
        DuetAsyncFIFO * _owner;

    public:
        DownstreamPort ( const std::string & name
                , DuetAsyncFIFO * owner
                , PortID id = InvalidPortID
                )
            : RequestPort ( name, owner, id )
            , _owner ( owner )
        {}

        void recvRangeChange () override {
            _owner->_upstream_port.sendRangeChange ();
        }

        bool isSnooping () const override {
            return _owner->_is_snooping;
        }

        Tick recvAtomicSnoop ( PacketPtr pkt ) override {
            panic ( "recvAtomicSnoop unimpl." );
        }

        void recvFunctionalSnoop ( PacketPtr pkt ) override {
            _owner->_upstream_port.sendFunctionalSnoop ( pkt );
        }

    public:
        void recvTimingSnoopReq ( PacketPtr pkt ) override;
        bool recvTimingResp     ( PacketPtr pkt ) override;
        void recvRetrySnoopResp () override;
        void recvReqRetry       () override;
    };

private:
    Cycles          _stage;         // synchronizer stage
    unsigned        _capacity;      // fifo capacity
    bool            _is_snooping;   // am I snooping?

    UpstreamPort    _upstream_port;
    DownstreamPort  _downstream_port;

    DuetAsyncFIFOCtrl * _upstream_ctrl;
    DuetAsyncFIFOCtrl * _downstream_ctrl;

    std::list<PacketPtr>    _downward_fifo;
    std::list<PacketPtr>    _upward_fifo;

public:
    DuetAsyncFIFO ( const DuetAsyncFIFOParams & p );
    Port & getPort ( const std::string & if_name
            , PortID idx = InvalidPortID ) override;
};

}   // namespace duet
}   // namespace gem5

#endif /* #ifndef __DUET_ASYNC_FIFO_HH */
