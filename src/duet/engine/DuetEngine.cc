#include "duet/engine/DuetEngine.hh"
#include "duet/engine/DuetLane.hh"
#include "sim/system.hh"
#include "sim/process.hh"

namespace gem5 {
namespace duet {

AddrRangeList DuetEngine::SRIPort::getAddrRanges () const {
    AddrRangeList list;
    AddrRange range ( _owner->_baseaddr
            , _owner->_baseaddr + ( _owner->_get_num_softregs () << 3 )
            );
    list.push_back ( range );
    return list;
}

DuetEngine::DuetEngine ( const DuetEngineParams & p )
    : DuetClockedObject         ( p )
    , _system                   ( p.system )
    , _process                  ( p.process )
    , _fifo_capacity            ( p.fifo_capacity )
    , _num_callers              ( p.num_callers )
    , _baseaddr                 ( p.baseaddr )
    , _lanes                    ( p.lanes )
    , _sri_port                 ( p.name + ".sri_port", this )
{
    _requestorId = p.system->getRequestorId (this);

    for ( int i = 0; i < p.port_mem_ports_connection_count; ++i ) {
        std::string portName = csprintf ( "%s.mem_ports[%d]", name(), i );
        _mem_ports.emplace_back ( portName, this, i );
    }
}

void DuetEngine::_update () {
    // -- pull phase ---------------------------------------------------------
    //  1. if SRI request buffer contains a load, and SRI response buffer is
    //     unused: check if we can handle the access
    if ( nullptr != _sri_port._req_buf
            && nullptr == _sri_port._resp_buf
            && _sri_port._req_buf->isRead () )
    {
        auto pkt = _sri_port._req_buf;
        softreg_id_t id = ( pkt->getAddr () - _baseaddr ) >> 3;

        uint64_t value;
        if ( _handle_softreg_read ( id, value ) ) {
            pkt->setLE ( value );
            pkt->makeResponse ();
            _sri_port._req_buf = nullptr;
            _sri_port._resp_buf = pkt;
        }
    }

    //  2. send as many memory requests as we can
    _try_send_mem_req_all ();

    //  3. call pull_phase on all lanes
    for ( auto & lane : _lanes )
        lane->pull_phase ();

    // -- push phase ---------------------------------------------------------
    //  1. if SRI request buffer contains a store, and SRI response buffer is
    //     unused: check if we can handle the access
    if ( nullptr != _sri_port._req_buf
            && nullptr == _sri_port._resp_buf
            && _sri_port._req_buf->isWrite () )
    {
        auto pkt = _sri_port._req_buf;
        softreg_id_t id = ( pkt->getAddr () - _baseaddr ) >> 3;
        uint64_t value = pkt->getLE<uint64_t> ();

        if ( _handle_softreg_write ( id, value ) ) {
            pkt->makeResponse ();
            _sri_port._req_buf = nullptr;
            _sri_port._resp_buf = pkt;
        }
    }

    //  2. if memory response buffer contains a valid load response, see if we
    //     can receive it
    for ( auto & port : _mem_ports ) {
        auto pkt = port._resp_buf;

        if ( nullptr != pkt ) {

            if ( pkt->isRead () ) {
                uint16_t chan_id = pkt->req->getFlags() & Request::ARCH_BITS;
                DuetFunctor::raw_data_t data (
                        new uint8_t[ pkt->getSize() ]);
                std::memcpy ( data.get(), pkt->getPtr<uint8_t>(), pkt->getSize() );

                if ( _try_recv_mem_resp_one ( chan_id, data ) ) {
                    port._resp_buf = nullptr;
                    delete pkt;
                }
            } else {
                port._resp_buf = nullptr;
                delete pkt;
            }
        }
    }

    //  3. call push_phase on all lanes
    for ( auto & lane : _lanes )
        lane->push_phase ();
}

void DuetEngine::_exchange () {
    //  1. send out SRI response if possible, then send out retry notifications
    if ( nullptr != _sri_port._resp_buf
            && !_sri_port._is_this_waiting_for_retry )
        _sri_port._try_send_resp ();

    if ( nullptr == _sri_port._req_buf
            && _sri_port._is_peer_waiting_for_retry )
    {
        _sri_port._is_peer_waiting_for_retry = false;
        _sri_port.sendRetryReq ();
    }

    //  2. send out memory requests if possible, then send out retry
    //  notifications
    for ( auto & port : _mem_ports ) {
        if ( nullptr != port._req_buf
                && !port._is_this_waiting_for_retry )
            port._try_send_req ();

        if ( nullptr == port._resp_buf
                && port._is_peer_waiting_for_retry )
        {
            port._is_peer_waiting_for_retry = false;
            port.sendRetryResp ();
        }
    }
}

bool DuetEngine::_has_work () {
    if ( _sri_port._req_buf || _sri_port._resp_buf )
        return true;

    for ( auto & port : _mem_ports )
        if ( port._req_buf || port._resp_buf )
            return true;

    for ( auto & lane : _lanes )
        if ( lane->has_work () )
            return true;

    return false;
}

DuetFunctor::chan_req_t & DuetEngine::get_chan_req (
        DuetFunctor::caller_id_t    caller_id
        , DuetFunctor::chan_id_t    chan_id
        )
{
    assert ( DuetFunctor::chan_id_t::REQ == chan_id.tag );
    return *( _chan_req_by_id [chan_id.id] );
}

DuetFunctor::chan_data_t & DuetEngine::get_chan_data (
        DuetFunctor::caller_id_t    caller_id
        , DuetFunctor::chan_id_t    chan_id
        )
{
    switch ( chan_id.tag ) {
    case DuetFunctor::chan_id_t::WDATA:
        return *( _chan_wdata_by_id [chan_id.id] );

    case DuetFunctor::chan_id_t::RDATA:
        return *( _chan_rdata_by_id [chan_id.id] );

    case DuetFunctor::chan_id_t::ARG:
        return *( _chan_arg_by_id [caller_id] [chan_id.id] );

    case DuetFunctor::chan_id_t::RET:
        return *( _chan_ret_by_id [caller_id] [chan_id.id] );

    case DuetFunctor::chan_id_t::PULL:
    case DuetFunctor::chan_id_t::PUSH:
        return *( _chan_int_by_id [chan_id.id] );

    default:
        panic ( "Invalid data channel tag" );
    }
}

bool DuetEngine::can_push_to_chan (
        DuetFunctor::caller_id_t    caller_id
        , DuetFunctor::chan_id_t    chan_id
        )
{
    switch ( chan_id.tag ) {
    case DuetFunctor::chan_id_t::REQ:
        return (0 == _fifo_capacity
                || _chan_req_by_id [chan_id.id]->size() < _fifo_capacity);

    case DuetFunctor::chan_id_t::WDATA:
        return (0 == _fifo_capacity
                || _chan_wdata_by_id [chan_id.id]->size() < _fifo_capacity);

    case DuetFunctor::chan_id_t::RDATA:
        panic ( "Trying to push to RDATA channel" );

    case DuetFunctor::chan_id_t::ARG:
        panic ( "Trying to push to ARG channel" );

    case DuetFunctor::chan_id_t::RET:
        return (0 == _fifo_capacity
                || _chan_ret_by_id [caller_id] [chan_id.id]->size() < _fifo_capacity);

    case DuetFunctor::chan_id_t::PULL:
        panic ( "Trying to push to PULL channel" );

    case DuetFunctor::chan_id_t::PUSH:
        return (0 == _fifo_capacity
                || _chan_int_by_id [chan_id.id]->size() < _fifo_capacity);

    default:
        panic ( "Invalid channel tag" );
    }

    return false;
}

bool DuetEngine::can_pull_from_chan (
        DuetFunctor::caller_id_t    caller_id
        , DuetFunctor::chan_id_t    chan_id
        )
{
    switch ( chan_id.tag ) {
    case DuetFunctor::chan_id_t::REQ:
        panic ( "Trying to pull from REQ channel" );

    case DuetFunctor::chan_id_t::WDATA:
        panic ( "Trying to pull from WDATA channel" );

    case DuetFunctor::chan_id_t::RDATA:
        return !_chan_rdata_by_id [chan_id.id]->empty ();

    case DuetFunctor::chan_id_t::ARG:
        return !_chan_arg_by_id [caller_id] [chan_id.id]->empty ();

    case DuetFunctor::chan_id_t::RET:
        panic ( "Trying to pull from RET channel" );

    case DuetFunctor::chan_id_t::PULL:
        return !_chan_int_by_id [chan_id.id]->empty ();

    case DuetFunctor::chan_id_t::PUSH:
        panic ( "Trying to pull from PUSH channel" );

    default:
        panic ( "Invalid channel tag" );
    }

    return false;
}

bool DuetEngine::_try_send_mem_req_one (
        uint16_t                    chan_id
        , DuetFunctor::mem_req_t    req
        , DuetFunctor::raw_data_t   data
        )
{
    // find a memory port whose req_buf is empty
    for ( auto & port : _mem_ports ) {
        if ( nullptr == port._req_buf ) {
            // translate address
            Addr paddr;
            panic_if ( !_process->pTable->translate ( req.addr, paddr ),
                    "Memory translation failed" );

            // make a new request
            RequestPtr req_ = std::make_shared <Request> (
                    paddr,
                    req.size,
                    (Request::FlagsType) chan_id,
                    _requestorId
                    );

            // get data and construct packet
            PacketPtr pkt = nullptr;
            switch ( req.type ) {
            case DuetFunctor::REQTYPE_LD :
                pkt = new Packet ( req_, Packet::makeReadCmd ( req_ ) );
                pkt->allocate ();
                break;

            case DuetFunctor::REQTYPE_ST :
                pkt = new Packet ( req_, Packet::makeWriteCmd ( req_ ) );
                pkt->allocate ();
                pkt->setData ( data.get() );
                break;

#define _TMP_UNSUPPORTED_REQ_TYPE(_t_) \
            case DuetFunctor::_t_ :           \
                panic ( "Unsupported request type: _t_" );  \
                break;

            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_LR)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_SC)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_SWAP)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_ADD)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_AND)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_OR)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_XOR)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_MAX)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_MAXU)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_MIN)
            _TMP_UNSUPPORTED_REQ_TYPE (REQTYPE_MINU)

#undef _TMP_UNSUPPORTED_REQ_TYPE

            default :
                panic ( "Invalid request type" );
            }

            port._req_buf = pkt;
            return true;
        }
    }

    return false;
}

}   // namespace duet
}   // namespace gem5
