from migen.fhdl.structure import *

class CommandRequest:
	def __init__(self, a, ba):
		self.a = Signal(BV(a))
		self.ba = Signal(BV(ba))
		self.cas_n = Signal(reset=1)
		self.ras_n = Signal(reset=1)
		self.we_n = Signal(reset=1)

class CommandRequestRW(CommandRequest):
	def __init__(self, a, ba, tagbits):
		CommandRequest.__init__(self, a, ba)
		self.stb = Signal()
		self.ack = Signal()
		self.is_read = Signal()
		self.is_write = Signal()
		self.tag = Signal(BV(tagbits))

class Multiplexer:
	def __init__(self, phy_settings, geom_settings, timing_settings, bank_machines, refresher, dfi, hub):
		pass
	
	def get_fragment(self):
		return Fragment()
