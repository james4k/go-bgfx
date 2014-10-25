package bgfx

import "testing"

func TestResetFlagConstants(t *testing.T) {
	for _, d := range resetFlagsTable {
		if d.a != ResetFlags(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestRendererTypeConstants(t *testing.T) {
	for _, d := range rendererTypeTable {
		if d.a != RendererType(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestTextureFormatConstants(t *testing.T) {
	for _, d := range texFormatTable {
		if d.a != TextureFormat(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestStateConstants(t *testing.T) {
	for _, d := range stateTable {
		if d.a != State(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}

func TestBlendConstants(t *testing.T) {
	for _, d := range blendTable {
		if d.a != BlendValue(d.b) {
			t.Errorf("%d != %d", d.a, d.b)
		}
	}
}
