from m5.params import *
from m5.proxy import *
from m5.objects import *

class DuetEngine (DuetClockedObject):
    type                = "DuetEngine"
    cxx_class           = "gem5::duet::DuetEngine"
    cxx_header          = "duet/engine/DuetEngine.hh"
    abstract            = True

    system              = Param.System ( Parent.any, "System object" )
    process             = Param.Process ( "Process running on the host processors" )
    fifo_capacity       = Param.Unsigned ( 64, "FIFO capacity" )
    baseaddr            = Param.Addr ( "Base address of softreg space" )
    sri_port            = ResponsePort ( "SRI response port" )
    mem_ports           = VectorRequestPort ( "memory ports" )
