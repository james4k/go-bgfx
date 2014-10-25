package bgfx

type State uint64

const StateDefault = StateRGBWrite | StateAlphaWrite | StateDepthWrite | StateDepthTestLess | StateCullCW | StateMSAA

const (
	StateRGBWrite State = 1 << iota
	StateAlphaWrite
	StateDepthWrite
)

const (
	StateDepthTestLess State = 0x10 << iota
	StateDepthTestLessEqual
)

const (
	StateCullCW State = 0x0000001000000000 << iota
	StateCullCCW
	StateCullMask = StateCullCW | StateCullCCW
)

const (
	StateMSAA State = 0x1000000000000000
)

const (
	StateBlendMask State = 0x000000000ffff000
)

type BlendValue uint32

const BlendShift BlendValue = 12
const (
	BlendZero BlendValue = 0x1000 + iota<<BlendShift
	BlendOne
	BlendSrcColor
	BlendInvSrcColor
	BlendSrcAlpha
	BlendInvSrcAlpha
	BlendDstAlpha
	BlendInvDstAlpha
	BlendDstColor
	BlendInvDstColor
	BlendSrcAlphaSat
	BlendFactor
	BlendInvFactor
)

func BlendFuncSeparate(srcRGB, dstRGB, srcA, dstA BlendValue) State {
	return (State(srcRGB) | (State(dstRGB) << 4)) |
		((State(srcA) | (State(dstA) << 4)) << 8)
}

func BlendFunc(src, dst BlendValue) State {
	return BlendFuncSeparate(src, dst, src, dst)
}

func StateBlendAlpha() State {
	return BlendFunc(BlendSrcAlpha, BlendInvSrcAlpha)
}
