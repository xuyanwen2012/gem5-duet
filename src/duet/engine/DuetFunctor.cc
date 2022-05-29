#include "duet/engine/DuetFunctor.hh"
#include "duet/engine/DuetLane.hh"

namespace gem5 {
namespace duet {

DuetFunctor::DuetFunctor ( DuetLane * lane, caller_id_t caller_id )
    : _lane                 ( lane )
    , _caller_id            ( caller_id )
    , _blocking_chan_id     ()
    , _stage                ( 0 )
    , _is_functors_turn     ( false )
    , _is_done              ( false )
{
    // create the locks
    std::unique_lock main_lock ( _mutex );
    std::swap ( main_lock, _main_lock );

    std::unique_lock functor_lock ( _mutex, std::defer_lock );
    std::swap ( functor_lock, _functor_lock );

    // get or create necessary channels
    setup ();

    // create the functor thread
    std::thread t ( [&] {
            // wait until awaken by the main thread
            //
            // thread ID might still be unavailable at this moment, so we
            // explicitly lock and wait
            _functor_lock.lock ();
            _cv.wait ( _functor_lock, [&]{ return _is_functors_turn; } );

            // run the kernel
            run ();
            _is_done = true;

            // notify the main thread
            _yield ( false );
            });

    // save the thread handle
    std::swap ( t, _thread );
}

bool DuetFunctor::advance () {
    // transfer control to the functor thread
    _yield ();

    // check if the functor exits
    if ( _is_done ) {
        _thread.join ();
        return true;
    } else {
        return false;
    }
}

DuetFunctor::chan_req_t & DuetFunctor::get_chan_req (
        DuetFunctor::chan_id_t      id
        )
{
    assert ( id.tag == chan_id_t::REQ );

    auto it = _chan_by_id.find ( id );
    if ( _chan_by_id.end() != it ) {
        auto pchan = reinterpret_cast <chan_req_t *> (it->second);
        return *pchan;
    } else {
        auto & chan = _lane->get_chan_req ( _caller_id, id );
        auto pchan = reinterpret_cast <void *> (&chan);
        _chan_by_id [id] = pchan;
        _id_by_chan [pchan] = id;
        return chan;
    }
}

DuetFunctor::chan_data_t & DuetFunctor::get_chan_data (
        DuetFunctor::chan_id_t      id
        )
{
    assert ( id.tag != chan_id_t::REQ
            && id.tag != chan_id_t::INVALID );

    auto it = _chan_by_id.find ( id );
    if ( _chan_by_id.end() != it ) {
        auto pchan = reinterpret_cast <chan_data_t *> (it->second);
        return *pchan;
    } else {
        auto & chan = _lane->get_chan_data ( _caller_id, id );
        auto pchan = reinterpret_cast <void *> (&chan);
        _chan_by_id [id] = pchan;
        _id_by_chan [pchan] = id;
        return chan;
    }
}

void DuetFunctor::enqueue_req (
        DuetFunctor::stage_t                stage
        , DuetFunctor::chan_req_t &         chan
        , const DuetFunctor::mem_req_t &    req
        )
{
    // update state
    _stage              = stage;
    _blocking_chan_id   = _id_by_chan [
        reinterpret_cast <void *> (&chan) ];

    // transfer control back to the main thread
    _yield ();

    // resume execution
    chan.push_back ( req );
}

template <typename T>
void DuetFunctor::enqueue_data (
        DuetFunctor::stage_t                stage
        , DuetFunctor::chan_data_t &        chan
        , const T &                         data
        )
{
    // update state
    _stage              = stage;
    _blocking_chan_id   = _id_by_chan [
        reinterpret_cast <void *> (&chan) ];

    // transfer control back to the main thread
    _yield ();

    // resume execution
    raw_data_t packed ( new uint8_t [sizeof(data)] );
    memcpy ( packed.get(), &data, sizeof(data) );
    chan.push_back ( packed );
}

template <typename T>
void DuetFunctor::dequeue_data (
        DuetFunctor::stage_t                stage
        , DuetFunctor::chan_data_t &        chan
        , T &                               data
        )
{
    // update state
    _stage              = stage;
    _blocking_chan_id   = _id_by_chan [
        reinterpret_cast <void *> (&chan) ];

    // transfer control back to the main thread
    _yield ();

    // resume execution
    raw_data_t data_ = chan.front ();
    chan.pop_front ();
    memcpy ( &data, data_.get(), sizeof(T) );
}

void DuetFunctor::_yield ( bool wait ) {
    if ( std::this_thread::get_id () == _thread.get_id () ) {
        // we are in the functor thread
        // transfer control to the main thread
        _is_functors_turn = false;
        _functor_lock.unlock ();
        _cv.notify_one ();

        // if asked to wait, wait until awaken
        if ( wait ) {
            _functor_lock.lock ();
            _cv.wait ( _functor_lock, [&]{ return _is_functors_turn; } );
        }
    } else {
        // we are in the main thread
        // transfer control to the functor thread
        _is_functors_turn = true;
        _main_lock.unlock ();
        _cv.notify_one ();

        // if asked to wait, wait until awaken
        if ( wait ) {
            _main_lock.lock ();
            _cv.wait ( _main_lock, [&]{ return !_is_functors_turn; } );
        }
    }
}

}   // namespace gem5
}   // namespace duet
