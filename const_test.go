package bgfx

import "testing"

func TestConstRendererType(t *testing.T) {
	for _, d := range rendererTypeTable {
		if d.a != RendererType(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstResetFlag(t *testing.T) {
	for _, d := range resetFlagsTable {
		if d.a != ResetFlags(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstDebugOptions(t *testing.T) {
	for _, d := range debugOptionsTable {
		if d.a != DebugOptions(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstClearOptions(t *testing.T) {
	for _, d := range clearOptionsTable {
		if d.a != ClearOptions(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstCapFlags(t *testing.T) {
	for _, d := range capFlagsTable {
		if d.a != CapFlags(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstUniformType(t *testing.T) {
	for _, d := range uniformTypeTable {
		if d.a != UniformType(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstAttrib(t *testing.T) {
	for _, d := range attribTable {
		if d.a != Attrib(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstAttribType(t *testing.T) {
	for _, d := range attribTypeTable {
		if d.a != AttribType(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstTextureFormat(t *testing.T) {
	for _, d := range texFormatTable {
		if d.a != TextureFormat(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstTextureFlags(t *testing.T) {
	for _, d := range texFlagsTable {
		if d.a != TextureFlags(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstState(t *testing.T) {
	for _, d := range stateTable {
		if d.a != State(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestConstBlend(t *testing.T) {
	for _, d := range blendTable {
		if d.a != BlendValue(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}
