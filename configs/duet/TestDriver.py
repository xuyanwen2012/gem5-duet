import m5, os
from m5.objects import *

range_      = AddrRange('512MB')
nc_range    = AddrRange(0xE102980000, size='4kB')

system = System(
        mem_mode = 'timing',
        mem_ranges = [range_, nc_range],
        )

system.clk_domain = SrcClockDomain( clock = '1GHz', voltage_domain = VoltageDomain() )
system.cpu = TimingSimpleCPU()
system.mem_ctrl = MemCtrl(
        clk_domain = DerivedClockDomain(
            clk_domain = system.clk_domain,
            clk_divider = 4
            ),
        dram = DDR3_1600_8x8( range = range_ ),
        )
system.sri_fe = DuetSRIFE( range = nc_range )
system.membus = SystemXBar()
afifo = DuetAsyncFIFO(
        stage = 10,
        capacity = 2,
        upstream_clk_domain = system.clk_domain,
        downstream_clk_domain = system.mem_ctrl.clk_domain
        )
system.afifo = afifo

system.cpu.createInterruptController()
system.cpu.icache_port  = system.membus.cpu_side_ports
system.cpu.dcache_port  = system.membus.cpu_side_ports
system.system_port      = system.membus.cpu_side_ports
# system.afifo.upstream_clk_domain    = system.clk_domain
system.afifo.upstream_port          = system.membus.mem_side_ports
# system.afifo.downstream_clk_domain  = system.mem_ctrl.clk_domain
system.afifo.downstream_port        = system.mem_ctrl.port
# system.mem_ctrl.port    = system.membus.mem_side_ports
system.sri_fe.inport    = system.membus.mem_side_ports

binary = os.path.join (os.path.dirname (os.path.abspath(__file__)),
        "../../tests/test-progs/duet/bin/riscv/linux/test_driver")

process = Process(
        cmd = [binary],
        drivers = DuetDriver(
            filename = "duet",
            dev = system.sri_fe
            )
        )
system.cpu.workload = process
system.cpu.createThreads()

system.workload = SEWorkload.init_compatible (binary)

root = Root (full_system = False, system = system)
m5.instantiate ()

print("Beginning simulation!")
exit_event = m5.simulate()
print('Exiting @ tick {} because {}'
      .format(m5.curTick(), exit_event.getCause()))
