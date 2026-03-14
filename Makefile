CXX = g++
CXXFLAGS = -std=c++17 -O3
LDFLAGS = -lvulkan

GLSLC = glslc

TARGET = raytracer
SHADER = raytracer.slang
SHADER_SPV = raytracer.spv

SLANGC = slangc

all: $(TARGET) $(SHADER_SPV)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET) $(LDFLAGS)

$(SHADER_SPV): $(SHADER)
	$(SLANGC) $(SHADER) -target spirv -o $(SHADER_SPV)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(SHADER_SPV) output.ppm output.png

.PHONY: all run clean
