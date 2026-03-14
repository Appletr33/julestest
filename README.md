# Headless C++ Vulkan Compute Raytracer

A lightweight, purely compute-based raytracer that runs entirely inside a Vulkan compute pipeline, rendering 1,000 reflective spheres over a checkerboard ground plane from a top-down camera view.

Because it's a headless application, it does not rely on a windowing system (like X11, Wayland, or GLFW), making it perfect for running in server environments, virtual machines, or CI/CD pipelines.

## Prerequisites

To build and run the application, you'll need the following dependencies.

### On Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake g++ libvulkan-dev vulkan-tools glslc
```

### Running in a Headless Environment or VM
If you are running this code in an environment without a dedicated hardware GPU, you will also need a software driver (like `lavapipe`/`llvmpipe`) to emulate a Vulkan device on your CPU:
```bash
sudo apt-get install -y mesa-vulkan-drivers
```

You can verify that the CPU implementation is available by running:
```bash
vulkaninfo | grep "deviceType"
# You should see something like: deviceType = PHYSICAL_DEVICE_TYPE_CPU
```

## Build Instructions

A `Makefile` is provided to compile both the C++ source code into an executable and the GLSL compute shader (`raytracer.comp`) into a SPIR-V binary (`raytracer.spv`).

To compile the project, run:
```bash
make
```

## Running the Raytracer

Once built, simply execute the program to dispatch the compute shader:
```bash
./raytracer
```

Alternatively, you can compile and run in a single step using:
```bash
make run
```

### Output

The program will render an 800x600 image and save it directly to a standard PPM file named `output.ppm` in the working directory.

If you have ImageMagick installed, you can quickly convert it to a `.png` for easier viewing:
```bash
convert output.ppm output.png
```

## Project Structure
- `main.cpp`: The Vulkan host application. Initializes the instance, logical device, queues, memory buffers, and descriptor sets. It then loads the compute shader, pushes constants, dispatches the pipeline, waits for synchronization, and writes the image to disk.
- `raytracer.comp`: The GLSL compute shader. Implements a sphere intersection algorithm, simple path tracing for reflections, a skybox, a checkerboard ground plane, and top-down camera mappings.
- `Makefile`: Build scripts.
