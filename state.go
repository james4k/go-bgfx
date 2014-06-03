package bgfx

type State uint64

const StateDefault = StateRGBWrite | StateAlphaWrite | StateDepthWrite | StateDepthTestLess | StateCullCW | StateMSAA

const (
	StateRGBWrite State = 1 << iota
	StateAlphaWrite
	StateDepthWrite
)

const (
	StateDepthTestLess State = 0x10 << (iota >> 1)
	StateDepthTestLessEqual
)

const (
	StateCullCW State = 0x0000001000000000 << (iota >> 1)
	StateCullCCW
)

const (
	StateMSAA State = 0x1000000000000000
)
