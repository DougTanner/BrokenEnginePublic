#version 460

// Input
layout (location = 0) in flat vec4 f4InColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = f4InColor;
}
