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

type BlendValue uint32

const (
	BlendZero BlendValue = iota + 0x1000
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
	x := srcRGB | (dstRGB << 4)
	x |= (srcA | (dstA << 4)) << 8
	return State(x)
}

func BlendFunc(src, dst BlendValue) State {
	return BlendFuncSeparate(src, dst, src, dst)
}

func StateBlendAlpha() State {
	return BlendFunc(BlendSrcAlpha, BlendInvSrcAlpha)
}
